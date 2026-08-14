#include "kernel/virtio_blk.hpp"
#include "kernel/dma.hpp"
#include "kernel/fdt.hpp"
#include "kernel/kprint.hpp"
#include "kernel/storage.hpp"
#if defined(__x86_64__)
#include "arch/pci.hpp"
#endif

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
static constexpr uint32_t GUEST_PAGE_SIZE = 0x028;
static constexpr uint32_t QUEUE_SEL = 0x030;
static constexpr uint32_t QUEUE_NUM_MAX = 0x034;
static constexpr uint32_t QUEUE_NUM = 0x038;
static constexpr uint32_t QUEUE_ALIGN = 0x03C;
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
static constexpr uint32_t FEATURE_FLUSH = 9;
static constexpr uint32_t FEATURE_FUA = 11;
// Keep enough storage for the largest queue exposed by QEMU virtio-mmio on
// the virt machine. Legacy transports use the device's complete advertised
// queue; the actual ring offsets are calculated from the selected queue size.
static constexpr uint32_t QUEUE_CAPACITY = 1024;

struct Descriptor { uint64_t address; uint32_t length; uint16_t flags; uint16_t next; };
struct UsedElement { uint32_t id; uint32_t length; };
struct RequestHeader { uint32_t type; uint32_t reserved; uint64_t sector; };
struct Ring {
    Descriptor* descriptors;
    volatile uint16_t* avail_index;
    volatile uint16_t* avail_ring;
    volatile uint16_t* used_index;
    UsedElement* used_ring;
    uintptr_t descriptors_address;
    uintptr_t driver_address;
    uintptr_t device_address;
};

static constexpr size_t queue_avail_bytes(uint32_t entries) {
    return sizeof(uint16_t) * (2u + entries + 1u);
}
static constexpr size_t queue_used_bytes(uint32_t entries) {
    return sizeof(uint16_t) * 2u + sizeof(UsedElement) * entries + sizeof(uint16_t);
}
static constexpr size_t queue_used_offset(uint32_t entries) {
    const size_t end = sizeof(Descriptor) * entries + queue_avail_bytes(entries);
    return (end + 4095u) & ~static_cast<size_t>(4095u);
}
static constexpr size_t QUEUE_STORAGE_SIZE = queue_used_offset(QUEUE_CAPACITY) + queue_used_bytes(QUEUE_CAPACITY);

struct Context {
    uintptr_t base;
    bool legacy_pci;
    uint32_t version;
    uint32_t queue_size;
    alignas(4096) uint8_t queue_storage[QUEUE_STORAGE_SIZE];
    Ring ring;
    uint64_t capacity;
    uint32_t block_size;
    bool active;
};

alignas(4096) static Context context{};
static volatile uint8_t request_status;
static RequestHeader request_header;

static volatile uint32_t* reg(uint32_t offset) { return reinterpret_cast<volatile uint32_t*>(context.base + offset); }
static uint32_t transport_offset(uint32_t offset);
static uint32_t read_reg(uint32_t offset) {
#if defined(__x86_64__)
    if (context.legacy_pci) {
        const uint16_t port = static_cast<uint16_t>(context.base + transport_offset(offset));
        if (offset == QUEUE_NUM_MAX || offset == QUEUE_NUM || offset == QUEUE_SEL) {
            uint16_t value;
            asm volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
            return value;
        }
        uint32_t value;
        asm volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }
#endif
    return *reg(offset);
}
static void write_reg(uint32_t offset, uint32_t value) {
#if defined(__x86_64__)
    if (context.legacy_pci) {
        const uint16_t port = static_cast<uint16_t>(context.base + transport_offset(offset));
        if (offset == QUEUE_NUM_MAX || offset == QUEUE_NUM || offset == QUEUE_SEL || offset == QUEUE_NOTIFY) {
            asm volatile ("outw %0, %1" : : "a"(static_cast<uint16_t>(value)), "Nd"(port));
        } else {
            asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
        }
        return;
    }
#endif
    *reg(offset) = value;
}
static uint8_t read_status() {
#if defined(__x86_64__)
    if (context.legacy_pci) {
        uint8_t value;
        asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(static_cast<uint16_t>(context.base + 0x12)));
        return value;
    }
#endif
    return static_cast<uint8_t>(*reinterpret_cast<volatile uint32_t*>(context.base + STATUS));
}
[[maybe_unused]] static uint32_t transport_offset(uint32_t offset) {
#if defined(__x86_64__)
    if (context.legacy_pci) {
        if (offset == QUEUE_SEL) return 0x0E;
        if (offset == QUEUE_NUM_MAX || offset == QUEUE_NUM) return 0x0C;
        if (offset == QUEUE_PFN) return 0x08;
        if (offset == QUEUE_NOTIFY) return 0x10;
        if (offset == DRIVER_FEATURES) return 0x04;
        if (offset >= CONFIG) return 0x14 + (offset - CONFIG);
    }
#endif
    return offset;
}
static void write_status(uint8_t value) {
#if defined(__x86_64__)
    if (context.legacy_pci) {
        asm volatile ("outb %0, %1" : : "a"(value), "Nd"(static_cast<uint16_t>(context.base + 0x12)));
        return;
    }
#endif
    *reinterpret_cast<volatile uint32_t*>(context.base + STATUS) = value;
}
static uint16_t used_index() {
    return *context.ring.used_index;
}
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
    if (context.version == 1 && maximum > QUEUE_CAPACITY) return false;
    context.queue_size = context.version == 1 ? maximum : (maximum < QUEUE_CAPACITY ? maximum : QUEUE_CAPACITY);
    for (size_t i = 0; i < sizeof(context.queue_storage); ++i) context.queue_storage[i] = 0;
    const uintptr_t queue_address = reinterpret_cast<uintptr_t>(context.queue_storage);
    const uintptr_t avail_address = queue_address + sizeof(Descriptor) * context.queue_size;
    const uintptr_t used_address = queue_address + queue_used_offset(context.queue_size);
    context.ring.descriptors = reinterpret_cast<Descriptor*>(queue_address);
    context.ring.avail_index = reinterpret_cast<volatile uint16_t*>(avail_address + sizeof(uint16_t));
    context.ring.avail_ring = reinterpret_cast<volatile uint16_t*>(avail_address + sizeof(uint16_t) * 2u);
    context.ring.descriptors_address = queue_address;
    context.ring.driver_address = avail_address;
    context.ring.device_address = context.version == 1 ? used_address : used_address;
    context.ring.used_index = reinterpret_cast<volatile uint16_t*>(used_address + sizeof(uint16_t));
    context.ring.used_ring = reinterpret_cast<UsedElement*>(used_address + sizeof(uint16_t) * 2u);
    // Transitional PCI exposes queue size as read-only. VirtIO-MMIO legacy
    // devices require QueueNum to be written before QueuePFN is accepted.
    if (!context.legacy_pci) write_reg(QUEUE_NUM, context.queue_size);
    if (context.version == 1) {
        if (!context.legacy_pci) {
            write_reg(GUEST_PAGE_SIZE, 4096);
            write_reg(QUEUE_ALIGN, 4096);
        }
        write_reg(QUEUE_PFN, static_cast<uint32_t>(queue_address >> 12));
    } else {
        write_reg(QUEUE_DESC_LOW, static_cast<uint32_t>(context.ring.descriptors_address));
        write_reg(QUEUE_DESC_HIGH, static_cast<uint32_t>(context.ring.descriptors_address >> 32));
        write_reg(QUEUE_DRIVER_LOW, static_cast<uint32_t>(context.ring.driver_address));
        write_reg(QUEUE_DRIVER_HIGH, static_cast<uint32_t>(context.ring.driver_address >> 32));
        write_reg(QUEUE_DEVICE_LOW, static_cast<uint32_t>(context.ring.device_address));
        write_reg(QUEUE_DEVICE_HIGH, static_cast<uint32_t>(context.ring.device_address >> 32));
    }
    if (context.version != 1) write_reg(QUEUE_READY, 1);
    return true;
}

static bool submit(RequestHeader* header, void* data, uint32_t data_length, bool device_writes_data) {
    dma::Buffer header_dma{}, data_dma{}, status_dma{};
    if (!dma::map(header, sizeof(*header), &header_dma, dma::DMA_READ) ||
        !dma::map(const_cast<uint8_t*>(&request_status), sizeof(request_status), &status_dma, dma::DMA_WRITE)) return false;
    if (data_length && !dma::map(data, data_length, &data_dma, device_writes_data ? dma::DMA_WRITE : dma::DMA_READ)) return false;
    const uint16_t slot = *context.ring.avail_index % context.queue_size;
    const uint16_t expected = static_cast<uint16_t>(used_index() + 1);
    Descriptor* descriptors = context.ring.descriptors;
    descriptors[0] = {header_dma.physical_address, sizeof(*header), 1, 1};
    uint16_t last = 1;
    if (data_length) {
        descriptors[1] = {data_dma.physical_address, data_length, static_cast<uint16_t>((device_writes_data ? 2 : 0) | 1), 2};
        last = 2;
    }
    descriptors[last] = {status_dma.physical_address, sizeof(request_status), 2, 0};
    request_status = 0xFF;
    context.ring.avail_ring[slot] = 0;
    dma::sync_for_device(&header_dma);
    if (data_length) dma::sync_for_device(&data_dma);
    dma::sync_for_device(&status_dma);
    barrier();
    const uint16_t next_avail = static_cast<uint16_t>(*context.ring.avail_index + 1);
    *context.ring.avail_index = next_avail;
    barrier();
    write_reg(QUEUE_NOTIFY, 0);
    for (uint32_t spin = 0; spin < 10000000; ++spin) {
        // The x86 TCG path may complete file-backed I/O from QEMU's event
        // loop. A harmless status-port read gives the VMM a VM-exit point
        // while the bounded polling path remains usable before interrupts.
        if (context.legacy_pci) (void)read_status();
        else (void)read_reg(STATUS);
        barrier();
        if (used_index() == expected) {
            // QEMU updates the used ring and the request status through
            // separate guest-memory stores. Give the status byte a bounded
            // visibility window before classifying the completion.
            for (uint32_t status_spin = 0; request_status == 0xFF && status_spin < 1000; ++status_spin) barrier();
            dma::sync_for_cpu(&status_dma);
            if (data_length) dma::sync_for_cpu(&data_dma);
            if (request_status != 0) {
                kernel::kprintf("[TEST][FAIL] VirtIO-Block device status %u\n", request_status);
                return false;
            }
            return true;
        }
    }
    kernel::kprintf("[TEST][FAIL] VirtIO-Block completion timeout\n");
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

[[maybe_unused]] static bool probe(uintptr_t base, bool legacy_pci = false) {
    for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t*>(&context)[i] = 0;
    context.base = base;
    context.legacy_pci = legacy_pci;
    if (legacy_pci) {
        context.version = 1;
    } else {
        const uint32_t magic = read_reg(MAGIC);
        const uint32_t device_id = read_reg(DEVICE_ID);
        if (magic != 0x74726976u || device_id != DEVICE_ID_BLOCK) {
            return false;
        }
        context.version = read_reg(VERSION);
    }
    write_status(0);
    write_status(STATUS_ACK | STATUS_DRIVER);
    bool flush_supported = false;
    bool fua_supported = false;
    if (legacy_pci) {
        const uint32_t features = read_reg(DEVICE_FEATURES);
        const uint32_t negotiated = features & ((1u << FEATURE_FLUSH) | (1u << FEATURE_FUA));
        flush_supported = (negotiated & (1u << FEATURE_FLUSH)) != 0;
        fua_supported = (negotiated & (1u << FEATURE_FUA)) != 0;
        write_reg(DRIVER_FEATURES, negotiated);
    }
    if (context.version != 1) {
        write_reg(DEVICE_FEATURES_SEL, 0);
        const uint32_t low_features = read_reg(DEVICE_FEATURES);
        const uint32_t low_negotiated = low_features & ((1u << FEATURE_FLUSH) | (1u << FEATURE_FUA));
        flush_supported = (low_negotiated & (1u << FEATURE_FLUSH)) != 0;
        fua_supported = (low_negotiated & (1u << FEATURE_FUA)) != 0;
        write_reg(DRIVER_FEATURES_SEL, 0);
        write_reg(DRIVER_FEATURES, low_negotiated);
        write_reg(DEVICE_FEATURES_SEL, 1);
        if (!(read_reg(DEVICE_FEATURES) & (1u << VERSION_1_BIT))) {
            return false;
        }
        write_reg(DRIVER_FEATURES_SEL, 1); write_reg(DRIVER_FEATURES, 1u << VERSION_1_BIT);
        write_status(STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
        if (!(read_status() & STATUS_FEATURES_OK)) {
            return false;
        }
    }
    bool read_only = false;
    if (context.version != 1) {
        write_reg(DEVICE_FEATURES_SEL, 0);
        read_only = (read_reg(DEVICE_FEATURES) & (1u << FEATURE_RO)) != 0;
    }
    context.block_size = 512;
    const uint32_t low = read_reg(CONFIG + 0);
    const uint32_t high = read_reg(CONFIG + 4);
    context.capacity = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
    if (!context.capacity) {
        return false;
    }
    if (!setup_queue()) {
        return false;
    }
    write_status(STATUS_ACK | STATUS_DRIVER | (context.version == 1 ? 0 : STATUS_FEATURES_OK) | STATUS_DRIVER_OK);
    context.active = true;
    (void)fua_supported;
    device.flags = (flush_supported ? DEVICE_FLUSH : 0) | (read_only ? DEVICE_READ_ONLY : DEVICE_WRITABLE | DEVICE_BARRIER);
    device.geometry = {context.capacity, context.block_size, context.block_size, context.queue_size};
    const Status registered = storage::Manager::register_device(&device);
    return registered == Status::Success;
}

#if defined(__x86_64__)
[[maybe_unused]] static bool probe_legacy_pci() {
    // Standard QEMU PC/Q35 places the transitional device on bus 0. Keep the
    // early-boot scan bounded; secondary-bus enumeration belongs in the
    // general PCI resource manager once it is available.
    for (uint16_t bus = 0; bus < 32; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            const uint8_t pci_bus = static_cast<uint8_t>(bus);
            const uint16_t vendor = hal::PciBus::read_config_16(pci_bus, slot, 0, 0x00);
            const uint16_t device_id = hal::PciBus::read_config_16(pci_bus, slot, 0, 0x02);
            // 0x1001 is the transitional/legacy VirtIO block PCI device. The
            // x86 path intentionally uses this stable I/O transport while
            // modern PCI common configuration is implemented separately.
            if (vendor != 0x1AF4 || device_id != 0x1001) continue;
            const uint32_t bar0 = hal::PciBus::read_config_32(pci_bus, slot, 0, 0x10);
            if (!(bar0 & 1u)) continue;
            kernel::kprintf("[+] Found transitional VirtIO-Block PCI device at BAR 0x%x.\n", bar0 & ~0x3u);
            const uint16_t command = hal::PciBus::read_config_16(pci_bus, slot, 0, 0x04);
            // Enable I/O-space access and PCI bus mastering before the device
            // can DMA descriptor rings or request buffers.
            hal::PciBus::write_config_16(pci_bus, slot, 0, 0x04, command | 0x0005u);
            kernel::kprintf("[+] VirtIO-Block PCI command 0x%x -> 0x%x.\n", command, hal::PciBus::read_config_16(pci_bus, slot, 0, 0x04));
            if (probe(static_cast<uintptr_t>(bar0 & ~0x3u), true)) return true;
        }
    }
    return false;
}
#endif

[[maybe_unused]] static bool runtime_self_test() {
    uint8_t write_buffer[512] __attribute__((aligned(512))){};
    uint8_t read_buffer[512] __attribute__((aligned(512))){};
    for (uint32_t i = 0; i < 512; ++i) write_buffer[i] = static_cast<uint8_t>(0xC3u ^ i);
    storage::Request read_request{storage::RequestType::Read, 0, 1, read_buffer, 0, nullptr, nullptr};
    if (storage::Manager::submit_sync(&device, &read_request) != storage::Status::Success) return false;
    kernel::kprintf("[TEST][PASS] VirtIO-Block read completion\n");
    if (!(device.flags & storage::DEVICE_WRITABLE)) return true;
    // Keep the baseline runtime test independent of optional FUA support;
    // flush completion is verified explicitly below when advertised.
    storage::Request write_request{storage::RequestType::Write, 0, 1, write_buffer, 0, nullptr, nullptr};
    if (storage::Manager::submit_sync(&device, &write_request) != storage::Status::Success) return false;
    if (storage::Manager::submit_sync(&device, &read_request) != storage::Status::Success) return false;
    for (uint32_t i = 0; i < 512; ++i) if (read_buffer[i] != write_buffer[i]) return false;
    kernel::kprintf("[TEST][PASS] VirtIO-Block write/read completion\n");
    if (device.flags & storage::DEVICE_FLUSH) {
        storage::Request flush_request{storage::RequestType::Flush, 0, 0, nullptr, 0, nullptr, nullptr};
        if (storage::Manager::submit_sync(&device, &flush_request) != storage::Status::Success) return false;
        kernel::kprintf("[TEST][PASS] VirtIO-Block flush completion\n");
    } else {
        kernel::kprintf("[TEST][SKIP] VirtIO-Block flush feature unavailable\n");
    }
    return true;
}
}

bool init() {
#if defined(__x86_64__)
    if (probe_legacy_pci()) {
        kernel::kprintf("[+] VirtIO-Block PCI runtime completion enabled.\n");
        if (!runtime_self_test()) kernel::kprintf("[TEST][FAIL] VirtIO-Block runtime completion\n");
        return true;
    }
#endif
    bool fdt_device_seen = false;
    for (uint32_t ordinal = 0; ordinal < 16; ++ordinal) {
        fdt::VirtioMmioDevice found{};
        if (!fdt::find_virtio_mmio(&found, ordinal)) {
            break;
        }
        fdt_device_seen = true;
        if (probe(found.phys_addr)) {
            kernel::kprintf("[+] VirtIO-Block storage device initialized.\n");
            if (!runtime_self_test()) kernel::kprintf("[TEST][FAIL] VirtIO-Block runtime completion\n");
            return true;
        }
    }
    if (!fdt_device_seen) {
        const uintptr_t mmio_bases[] = {
            0x0A000000ull,
        };
        for (uintptr_t mmio_base : mmio_bases) {
            for (uint32_t slot = 0; slot < 32; ++slot) {
                const uintptr_t base = mmio_base + static_cast<uintptr_t>(slot) * 0x200ull;
                if (probe(base)) {
                    kernel::kprintf("[+] VirtIO-Block storage device initialized.\n");
                    if (!runtime_self_test()) kernel::kprintf("[TEST][FAIL] VirtIO-Block runtime completion\n");
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace virtio_blk
