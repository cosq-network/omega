#include "kernel/virtio_blk.hpp"
#include "kernel/dma.hpp"
#include "kernel/fdt.hpp"
#include "kernel/kprint.hpp"
#include "kernel/storage.hpp"

namespace virtio_blk {
using namespace storage;
namespace {
static constexpr uint32_t MAGIC = 0x000;
static constexpr uint32_t VERSION = 0x004;
static constexpr uint32_t DEVICE_ID = 0x008;
static constexpr uint32_t DEVICE_FEATURES = 0x010;
static constexpr uint32_t DEVICE_FEATURES_SEL = 0x014;
static constexpr uint32_t DRIVER_FEATURES = 0x020;
static constexpr uint32_t DRIVER_FEATURES_SEL = 0x024;
static constexpr uint32_t QUEUE_SEL = 0x030;
static constexpr uint32_t QUEUE_NUM_MAX = 0x034;
static constexpr uint32_t QUEUE_NUM = 0x038;
static constexpr uint32_t QUEUE_PFN = 0x040;
static constexpr uint32_t QUEUE_READY = 0x044;
static constexpr uint32_t QUEUE_NOTIFY = 0x050;
static constexpr uint32_t STATUS = 0x070;
static constexpr uint32_t QUEUE_DESC_LOW = 0x080;
static constexpr uint32_t QUEUE_DESC_HIGH = 0x084;
static constexpr uint32_t QUEUE_DRIVER_LOW = 0x090;
static constexpr uint32_t QUEUE_DRIVER_HIGH = 0x094;
static constexpr uint32_t QUEUE_DEVICE_LOW = 0x0A0;
static constexpr uint32_t QUEUE_DEVICE_HIGH = 0x0A4;
static constexpr uint32_t CONFIG = 0x100;

static constexpr uint32_t STATUS_ACK = 1;
static constexpr uint32_t STATUS_DRIVER = 2;
static constexpr uint32_t STATUS_DRIVER_OK = 4;
static constexpr uint32_t STATUS_FEATURES_OK = 8;
static constexpr uint32_t VERSION_1_BIT = 0;
static constexpr uint32_t DEVICE_ID_BLOCK = 2;
static constexpr uint32_t TYPE_IN = 0;
static constexpr uint32_t TYPE_OUT = 1;
static constexpr uint32_t TYPE_FLUSH = 4;
static constexpr uint32_t FEATURE_RO = 5;
static constexpr uint32_t QUEUE_CAPACITY = 8;

struct Descriptor { uint64_t address; uint32_t length; uint16_t flags; uint16_t next; };
struct UsedElement { uint32_t id; uint32_t length; };
struct Queue {
    alignas(16) Descriptor descriptors[QUEUE_CAPACITY];
    uint16_t avail_flags;
    uint16_t avail_index;
    uint16_t avail_ring[QUEUE_CAPACITY];
    uint16_t avail_event;
    uint16_t used_flags;
    uint16_t used_index;
    UsedElement used_ring[QUEUE_CAPACITY];
    uint16_t used_event;
};
struct LegacyQueue {
    Queue modern;
    uint8_t padding[4096 - sizeof(Queue)];
    uint16_t legacy_used_flags;
    uint16_t legacy_used_index;
    UsedElement legacy_used_ring[QUEUE_CAPACITY];
    uint16_t legacy_used_event;
};
struct RequestHeader { uint32_t type; uint32_t reserved; uint64_t sector; };

struct Context {
    uintptr_t base;
    uint32_t version;
    uint32_t queue_size;
    alignas(4096) LegacyQueue queue;
    uint64_t capacity;
    uint32_t block_size;
    bool active;
};

alignas(4096) static Context context{};
static uint8_t request_status;
static RequestHeader request_header;

static volatile uint32_t* reg(uint32_t offset) { return reinterpret_cast<volatile uint32_t*>(context.base + offset); }
static uint32_t read_reg(uint32_t offset) { return *reg(offset); }
static void write_reg(uint32_t offset, uint32_t value) { *reg(offset) = value; }
static uint16_t used_index() { return context.version == 1 ? context.queue.legacy_used_index : context.queue.modern.used_index; }
static void barrier() {
#if defined(__aarch64__)
    asm volatile("dsb sy" ::: "memory");
#elif defined(__riscv)
    asm volatile("fence rw, rw" ::: "memory");
#else
    asm volatile("mfence" ::: "memory");
#endif
}

static bool setup_queue() {
    write_reg(QUEUE_SEL, 0);
    const uint32_t maximum = read_reg(QUEUE_NUM_MAX);
    if (maximum < 3) return false;
    context.queue_size = maximum < QUEUE_CAPACITY ? maximum : QUEUE_CAPACITY;
    for (size_t i = 0; i < sizeof(context.queue); ++i) reinterpret_cast<uint8_t*>(&context.queue)[i] = 0;
    write_reg(QUEUE_NUM, context.queue_size);
    const uintptr_t queue_address = reinterpret_cast<uintptr_t>(&context.queue);
    if (context.version == 1) {
        write_reg(QUEUE_PFN, static_cast<uint32_t>(queue_address >> 12));
    } else {
        write_reg(QUEUE_DESC_LOW, static_cast<uint32_t>(queue_address));
        write_reg(QUEUE_DESC_HIGH, static_cast<uint32_t>(queue_address >> 32));
        const uintptr_t driver = reinterpret_cast<uintptr_t>(&context.queue.modern.avail_flags);
        const uintptr_t device = reinterpret_cast<uintptr_t>(&context.queue.modern.used_flags);
        write_reg(QUEUE_DRIVER_LOW, static_cast<uint32_t>(driver));
        write_reg(QUEUE_DRIVER_HIGH, static_cast<uint32_t>(driver >> 32));
        write_reg(QUEUE_DEVICE_LOW, static_cast<uint32_t>(device));
        write_reg(QUEUE_DEVICE_HIGH, static_cast<uint32_t>(device >> 32));
    }
    if (context.version != 1) write_reg(QUEUE_READY, 1);
    return true;
}

static bool submit(RequestHeader* header, void* data, uint32_t data_length, bool device_writes_data) {
    dma::Buffer header_dma{}, data_dma{}, status_dma{};
    if (!dma::map(header, sizeof(*header), &header_dma, dma::DMA_READ) ||
        !dma::map(&request_status, sizeof(request_status), &status_dma, dma::DMA_WRITE)) return false;
    if (data_length && !dma::map(data, data_length, &data_dma, device_writes_data ? dma::DMA_WRITE : dma::DMA_READ)) return false;
    const uint16_t slot = context.queue.modern.avail_index % context.queue_size;
    const uint16_t expected = static_cast<uint16_t>(used_index() + 1);
    Descriptor* descriptors = context.queue.modern.descriptors;
    descriptors[0] = {header_dma.physical_address, sizeof(*header), 1, 1};
    uint16_t last = 1;
    if (data_length) {
        descriptors[1] = {data_dma.physical_address, data_length, static_cast<uint16_t>(device_writes_data ? 2 : 1), 2};
        last = 2;
    }
    descriptors[last] = {status_dma.physical_address, sizeof(request_status), 2, 0};
    request_status = 0xFF;
    context.queue.modern.avail_ring[slot] = 0;
    barrier();
    ++context.queue.modern.avail_index;
    barrier();
    write_reg(QUEUE_NOTIFY, 0);
    for (uint32_t spin = 0; spin < 100000; ++spin) {
        barrier();
        if (used_index() == expected) {
            dma::sync_for_cpu(&status_dma);
            return request_status == 0;
        }
    }
    return false;
}

static Status submit_request(storage::Device*, storage::Request* request) {
    if (!context.active || !request) return Status::NotReady;
    if (request->type == RequestType::Flush) {
        request_header = {TYPE_FLUSH, 0, 0};
        if (!submit(&request_header, nullptr, 0, false)) return Status::Timeout;
    } else if (request->type == RequestType::Read || request->type == RequestType::Write) {
        request_header = {request->type == RequestType::Read ? TYPE_IN : TYPE_OUT, 0, request->lba};
        const uint32_t bytes = request->block_count * context.block_size;
        if (!submit(&request_header, request->buffer, bytes, request->type == RequestType::Read)) return Status::IoError;
    } else {
        return Status::Unsupported;
    }
    if (request->complete) request->complete(request, Status::Success, request->context);
    return Status::Success;
}
static Status cancel(storage::Device*, storage::Request*) { return Status::Unsupported; }
static Status flush(storage::Device* device) {
    storage::Request request{RequestType::Flush, 0, 0, nullptr, 0, nullptr, nullptr};
    return submit_request(device, &request);
}
static Status reset(storage::Device*) { return Status::Unsupported; }
static Status eject(storage::Device*) { return Status::Unsupported; }
static const storage::DeviceOps ops{submit_request, cancel, flush, reset, eject};
static storage::Device device{
    0, 0, 1, storage::DeviceType::Virtual, storage::Protocol::VirtioBlock,
    {0, 512, 512, 32}, 0, storage::State::Discovered, "virtio0", "VirtIO Block",
    &ops, &context
};

[[maybe_unused]] static bool probe(uintptr_t base) {
    context = {};
    context.base = base;
    const uint32_t magic = read_reg(MAGIC);
    const uint32_t device_id = read_reg(DEVICE_ID);
    if (magic != 0x74726976u || device_id != DEVICE_ID_BLOCK) return false;
    context.version = read_reg(VERSION);
    write_reg(STATUS, 0);
    write_reg(STATUS, STATUS_ACK | STATUS_DRIVER);
    if (context.version != 1) {
        write_reg(DEVICE_FEATURES_SEL, 1);
        if (!(read_reg(DEVICE_FEATURES) & (1u << VERSION_1_BIT))) return false;
        write_reg(DRIVER_FEATURES_SEL, 0); write_reg(DRIVER_FEATURES, 0);
        write_reg(DRIVER_FEATURES_SEL, 1); write_reg(DRIVER_FEATURES, 1u << VERSION_1_BIT);
        write_reg(STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
        if (!(read_reg(STATUS) & STATUS_FEATURES_OK)) return false;
    }
    bool read_only = false;
    if (context.version != 1) {
        write_reg(DEVICE_FEATURES_SEL, 0);
        read_only = (read_reg(DEVICE_FEATURES) & (1u << FEATURE_RO)) != 0;
    }
    context.block_size = 512;
    const uint32_t low = read_reg(CONFIG + 0);
    const uint32_t high = context.version == 1 ? 0 : read_reg(CONFIG + 4);
    context.capacity = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
    if (!context.capacity) return false;
    if (!setup_queue()) return false;
    write_reg(STATUS, STATUS_ACK | STATUS_DRIVER | (context.version == 1 ? 0 : STATUS_FEATURES_OK) | STATUS_DRIVER_OK);
    context.active = true;
    device.flags = DEVICE_FLUSH | (read_only ? DEVICE_READ_ONLY : DEVICE_WRITABLE | DEVICE_BARRIER);
    device.geometry = {context.capacity, context.block_size, context.block_size, context.queue_size};
    const Status registered = storage::Manager::register_device(&device);
    return registered == Status::Success;
}
}

bool init() {
#if !defined(OMEGA_ENABLE_EXPERIMENTAL_VIRTIO_BLOCK)
    return false;
#else
    bool fdt_device_seen = false;
    for (uint32_t ordinal = 0; ordinal < 16; ++ordinal) {
        fdt::VirtioMmioDevice found{};
        if (!fdt::find_virtio_mmio(&found, ordinal)) break;
        fdt_device_seen = true;
        if (probe(found.phys_addr)) {
            kernel::kprintf("[+] VirtIO-Block storage device initialized.\n");
            return true;
        }
    }
    if (!fdt_device_seen) {
        for (uint32_t slot = 0; slot < 32; ++slot) {
            const uintptr_t base = 0x0A000000ull + static_cast<uintptr_t>(slot) * 0x200ull;
            if (probe(base)) {
                kernel::kprintf("[+] VirtIO-Block storage device initialized.\n");
                return true;
            }
        }
    }
    return false;
#endif
}

} // namespace virtio_blk
