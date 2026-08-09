#include "kernel/virtio_gpu.hpp"
#include "kernel/fdt.hpp"
#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"
#include "kernel/vmm.hpp"

namespace virtio_gpu {
namespace {

static constexpr uint32_t MMIO_MAGIC = 0x000;
static constexpr uint32_t MMIO_VERSION = 0x004;
static constexpr uint32_t MMIO_DEVICE_ID = 0x008;
static constexpr uint32_t MMIO_STATUS = 0x070;
static constexpr uint32_t MMIO_QUEUE_SEL = 0x030;
static constexpr uint32_t MMIO_QUEUE_NUM_MAX = 0x034;
static constexpr uint32_t MMIO_QUEUE_NUM = 0x038;
static constexpr uint32_t MMIO_QUEUE_READY = 0x044;
static constexpr uint32_t MMIO_QUEUE_NOTIFY = 0x050;
static constexpr uint32_t MMIO_QUEUE_DESC_LOW = 0x080;
static constexpr uint32_t MMIO_QUEUE_DESC_HIGH = 0x084;
static constexpr uint32_t MMIO_QUEUE_DRIVER_LOW = 0x090;
static constexpr uint32_t MMIO_QUEUE_DRIVER_HIGH = 0x094;
static constexpr uint32_t MMIO_QUEUE_DEVICE_LOW = 0x0A0;
static constexpr uint32_t MMIO_QUEUE_DEVICE_HIGH = 0x0A4;
static constexpr uint32_t MMIO_QUEUE_PFN = 0x040;
static constexpr uint32_t MMIO_DEVICE_FEATURES = 0x010;
static constexpr uint32_t MMIO_DEVICE_FEATURES_SEL = 0x014;
static constexpr uint32_t MMIO_DRIVER_FEATURES = 0x020;
static constexpr uint32_t MMIO_DRIVER_FEATURES_SEL = 0x024;
static constexpr uint32_t MMIO_STATUS_ACK = 1;
static constexpr uint32_t MMIO_STATUS_DRIVER = 2;
static constexpr uint32_t MMIO_STATUS_FEATURES_OK = 8;
static constexpr uint32_t MMIO_STATUS_DRIVER_OK = 4;
static constexpr uint32_t VIRTIO_F_VERSION_1 = 0;

static constexpr uint32_t GPU_DEVICE_ID = 16;
static constexpr uint32_t GPU_CMD_GET_DISPLAY_INFO = 0x0100;
static constexpr uint32_t GPU_CMD_RESOURCE_CREATE_2D = 0x0101;
static constexpr uint32_t GPU_CMD_RESOURCE_ATTACH_BACKING = 0x0102;
static constexpr uint32_t GPU_CMD_SET_SCANOUT = 0x0103;
static constexpr uint32_t GPU_CMD_TRANSFER_TO_HOST_2D = 0x0105;
static constexpr uint32_t GPU_CMD_RESOURCE_FLUSH = 0x0106;
static constexpr uint32_t GPU_RESP_OK_DISPLAY_INFO = 0x1100;
static constexpr uint32_t GPU_RESP_OK_NODATA = 0x1100;
static constexpr uint32_t GPU_FORMAT_B8G8R8A8_UNORM = 1;
static constexpr uint32_t GPU_QUEUE_SIZE = 8;
static constexpr uint32_t MAX_WIDTH = 1024;
static constexpr uint32_t MAX_HEIGHT = 768;

struct Descriptor { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct UsedElem { uint32_t id; uint32_t len; };
struct QueueMemory {
    alignas(16) Descriptor desc[GPU_QUEUE_SIZE];
    alignas(2) uint16_t avail_flags;
    alignas(2) uint16_t avail_idx;
    alignas(2) uint16_t avail_ring[GPU_QUEUE_SIZE];
    uint16_t avail_used_event;
    alignas(4) uint16_t used_flags;
    alignas(4) uint16_t used_idx;
    UsedElem used_ring[GPU_QUEUE_SIZE];
    uint16_t used_avail_event;
    // Legacy VirtIO places the device ring on the next page boundary.
    uint8_t legacy_padding[4096 - 222];
    uint16_t legacy_used_flags;
    uint16_t legacy_used_idx;
    UsedElem legacy_used_ring[GPU_QUEUE_SIZE];
    uint16_t legacy_used_avail_event;
};

struct Header { uint32_t type; uint32_t flags; uint64_t fence_id; uint32_t ctx_id; uint32_t ring_idx; };
struct Rect { uint32_t x; uint32_t y; uint32_t width; uint32_t height; };
struct DisplayInfo { uint32_t r; uint32_t g; uint32_t b; uint32_t a; Rect rect; uint32_t enabled; uint32_t flags; };
struct GetDisplayInfo { Header hdr; DisplayInfo pmodes[16]; };
struct Create2D { Header hdr; uint32_t resource_id; uint32_t format; uint32_t width; uint32_t height; };
struct AttachEntry { uint64_t addr; uint32_t length; uint32_t padding; };
struct AttachBacking { Header hdr; uint32_t resource_id; uint32_t nr_entries; AttachEntry entry; };
struct SetScanout { Header hdr; Rect rect; uint32_t scanout_id; uint32_t resource_id; };
struct Transfer2D { Header hdr; Rect rect; uint64_t offset; };
struct Flush { Header hdr; Rect rect; };

alignas(4096) static QueueMemory queue;
alignas(4096) static uint8_t backing[MAX_WIDTH * MAX_HEIGHT * 4];
alignas(16) static uint8_t response[sizeof(GetDisplayInfo)];
static uintptr_t mmio_base = 0;
static uint32_t mmio_version = 0;
static uint32_t width = 0;
static uint32_t height = 0;
static uint32_t queue_size = GPU_QUEUE_SIZE;
static bool active = false;

static volatile uint32_t* reg(uint32_t offset) { return reinterpret_cast<volatile uint32_t*>(mmio_base + offset); }
static uint32_t read(uint32_t offset) { return *reg(offset); }
static void write(uint32_t offset, uint32_t value) { *reg(offset) = value; }
static void zero(void* p, size_t n) { uint8_t* b = reinterpret_cast<uint8_t*>(p); for (size_t i=0;i<n;++i) b[i]=0; }
static uint16_t used_index() { return mmio_version == 1 ? queue.legacy_used_idx : queue.used_idx; }
static void dma_barrier() {
#if defined(__aarch64__)
    asm volatile("dsb sy" ::: "memory");
#elif defined(__riscv)
    asm volatile("fence rw, rw" ::: "memory");
#else
    asm volatile("mfence" ::: "memory");
#endif
}

static bool setup_queue() {
    write(MMIO_QUEUE_SEL, 0);
    uint32_t max = read(MMIO_QUEUE_NUM_MAX);
    if (max < 2) return false;
    queue_size = GPU_QUEUE_SIZE < max ? GPU_QUEUE_SIZE : max;
    write(MMIO_QUEUE_NUM, queue_size);
    zero(&queue, sizeof(queue));
    const uintptr_t q = reinterpret_cast<uintptr_t>(&queue);
    if (mmio_version == 1) {
        write(MMIO_QUEUE_PFN, static_cast<uint32_t>(q >> 12));
    } else {
        write(MMIO_QUEUE_DESC_LOW, static_cast<uint32_t>(q));
        write(MMIO_QUEUE_DESC_HIGH, static_cast<uint32_t>(q >> 32));
        const uintptr_t driver = reinterpret_cast<uintptr_t>(&queue.avail_flags);
        const uintptr_t device = reinterpret_cast<uintptr_t>(&queue.used_flags);
        write(MMIO_QUEUE_DRIVER_LOW, static_cast<uint32_t>(driver));
        write(MMIO_QUEUE_DRIVER_HIGH, static_cast<uint32_t>(driver >> 32));
        write(MMIO_QUEUE_DEVICE_LOW, static_cast<uint32_t>(device));
        write(MMIO_QUEUE_DEVICE_HIGH, static_cast<uint32_t>(device >> 32));
    }
    write(MMIO_QUEUE_READY, 1);
    return true;
}

static bool submit(void* request, uint32_t request_len, void* reply, uint32_t reply_len) {
    const uint16_t slot = queue.avail_idx % queue_size;
    const uint16_t expected_used = used_index() + 1;
    queue.desc[0] = {reinterpret_cast<uintptr_t>(request), request_len, 1, 1};
    queue.desc[1] = {reinterpret_cast<uintptr_t>(reply), reply_len, 2, 0};
    queue.avail_ring[slot] = 0;
    dma_barrier();
    ++queue.avail_idx;
    dma_barrier();
    write(MMIO_QUEUE_NOTIFY, 0);
    for (uint32_t spins = 0; spins < 1000; ++spins) {
        dma_barrier();
        if (used_index() == expected_used) return true;
    }
    return false;
}

static bool command_ok(uint32_t response_type) { return response_type == GPU_RESP_OK_DISPLAY_INFO || response_type == GPU_RESP_OK_NODATA; }

[[maybe_unused]] static bool probe_base(uintptr_t base) {
    if (base == 0) return false;
    mmio_base = base;
    if (!memory::VirtualMemoryManager::map_page(mmio_base, mmio_base, memory::PAGE_PRESENT | memory::PAGE_WRITABLE | memory::PAGE_DEVICE)) return false;
    if (read(MMIO_MAGIC) != 0x74726976u || read(MMIO_DEVICE_ID) != GPU_DEVICE_ID) return false;
    mmio_version = read(MMIO_VERSION);
    write(MMIO_STATUS, 0);
    write(MMIO_STATUS, MMIO_STATUS_ACK | MMIO_STATUS_DRIVER);
    if (mmio_version != 1) {
        write(MMIO_DEVICE_FEATURES_SEL, 1);
        const uint32_t features_hi = read(MMIO_DEVICE_FEATURES);
        if (!(features_hi & (1u << VIRTIO_F_VERSION_1))) return false;
        write(MMIO_DRIVER_FEATURES_SEL, 0);
        write(MMIO_DRIVER_FEATURES, 0);
        write(MMIO_DRIVER_FEATURES_SEL, 1);
        write(MMIO_DRIVER_FEATURES, 1u << VIRTIO_F_VERSION_1);
        write(MMIO_STATUS, MMIO_STATUS_ACK | MMIO_STATUS_DRIVER | MMIO_STATUS_FEATURES_OK);
        if (!(read(MMIO_STATUS) & MMIO_STATUS_FEATURES_OK)) return false;
    }
    if (!setup_queue()) return false;
    write(MMIO_STATUS, MMIO_STATUS_ACK | MMIO_STATUS_DRIVER | MMIO_STATUS_FEATURES_OK | MMIO_STATUS_DRIVER_OK);
    return true;
}

[[maybe_unused]] static bool get_display_info() {
    GetDisplayInfo request{};
    request.hdr.type = GPU_CMD_GET_DISPLAY_INFO;
    zero(response, sizeof(response));
    if (!submit(&request, sizeof(request.hdr), response, sizeof(response))) {
        return false;
    }
    const auto* info = reinterpret_cast<const GetDisplayInfo*>(response);
    if (!command_ok(info->hdr.type) || info->pmodes[0].enabled == 0) return false;
    width = info->pmodes[0].rect.width; height = info->pmodes[0].rect.height;
    if (width == 0 || height == 0 || width > MAX_WIDTH || height > MAX_HEIGHT) return false;
    return true;
}

[[maybe_unused]] static bool create_scanout() {
    Create2D create{}; create.hdr.type=GPU_CMD_RESOURCE_CREATE_2D; create.resource_id=1; create.format=GPU_FORMAT_B8G8R8A8_UNORM; create.width=width; create.height=height;
    Header reply{}; if (!submit(&create,sizeof(create),&reply,sizeof(reply)) || !command_ok(reply.type)) return false;
    AttachBacking attach{}; attach.hdr.type=GPU_CMD_RESOURCE_ATTACH_BACKING; attach.resource_id=1; attach.nr_entries=1; attach.entry.addr=reinterpret_cast<uintptr_t>(backing); attach.entry.length=width*height*4;
    if (!submit(&attach,sizeof(attach),&reply,sizeof(reply)) || !command_ok(reply.type)) return false;
    SetScanout scan{}; scan.hdr.type=GPU_CMD_SET_SCANOUT; scan.rect={0,0,width,height}; scan.scanout_id=0; scan.resource_id=1;
    return submit(&scan,sizeof(scan),&reply,sizeof(reply)) && command_ok(reply.type);
}

}

bool init(hal::FramebufferInfo* out) {
    if (!out) return false;
#if !defined(OMEGA_ENABLE_EXPERIMENTAL_VIRTIO_GPU)
    // Keep device discovery opt-in until the queue completion path is proven
    // on each transport revision. A failed GPU probe must never stall boot.
    return false;
#else
    active = false;
    for (uint32_t ordinal=0; ordinal<64; ++ordinal) {
        fdt::VirtioMmioDevice device{};
        if (!fdt::find_virtio_mmio(&device, ordinal)) break;
        if (!probe_base(device.phys_addr)) continue;
        if (!get_display_info() || !create_scanout()) continue;
        zero(backing, static_cast<size_t>(width)*height*4);
        out->phys_addr=reinterpret_cast<uintptr_t>(backing); out->virt_addr=out->phys_addr; out->size=static_cast<uint64_t>(width)*height*4;
        out->width=width; out->height=height; out->pitch=width*4; out->bpp=32;
        out->red_mask=0xFF; out->green_mask=0xFF; out->blue_mask=0xFF; out->red_shift=16; out->green_shift=8; out->blue_shift=0;
        active = true; return true;
    }
    return false;
#endif
}

bool flush() {
    if (!active) return false;
    Header reply{};
    Transfer2D transfer{}; transfer.hdr.type=GPU_CMD_TRANSFER_TO_HOST_2D; transfer.rect={0,0,width,height}; transfer.offset=0;
    if (!submit(&transfer,sizeof(transfer),&reply,sizeof(reply)) || !command_ok(reply.type)) return false;
    Flush flush_cmd{}; flush_cmd.hdr.type=GPU_CMD_RESOURCE_FLUSH; flush_cmd.rect={0,0,width,height};
    return submit(&flush_cmd,sizeof(flush_cmd),&reply,sizeof(reply)) && command_ok(reply.type);
}

bool self_test() {
    if (!active || width == 0 || height == 0) return false;
    backing[0] = 0xFF;
    backing[1] = 0x00;
    backing[2] = 0x00;
    backing[3] = 0xFF;
    return flush();
}

} // namespace virtio_gpu
