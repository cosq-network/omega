#include "vga_internal.hpp"

extern "C" uint64_t multiboot_info_ptr;

namespace hal {
namespace vga {

static constexpr uint32_t MULTIBOOT2_MAGIC         = 0x36D76289;
static constexpr uint32_t MULTIBOOT_TAG_TYPE_END   = 0;
static constexpr uint32_t MULTIBOOT_TAG_TYPE_FB    = 8;

struct Multiboot2Tag {
    uint32_t type;
    uint32_t size;
};

static const uint8_t* tag_payload(const Multiboot2Tag* tag) {
    return reinterpret_cast<const uint8_t*>(tag) + sizeof(Multiboot2Tag);
}

BootFramebufferResult boot_framebuffer_probe() {
    BootFramebufferResult result{};
    result.valid = false;

    if (multiboot_info_ptr == 0) {
        return result;
    }

    const auto* info = reinterpret_cast<const uint32_t*>(multiboot_info_ptr);
    if (info[0] != MULTIBOOT2_MAGIC) {
        return result;
    }

    const uint32_t total_size = info[1];
    uint32_t offset = 8;

    while (offset + sizeof(Multiboot2Tag) <= total_size) {
        const auto* tag = reinterpret_cast<const Multiboot2Tag*>(multiboot_info_ptr + offset);
        if (tag->type == MULTIBOOT_TAG_TYPE_END || tag->size < 8) {
            break;
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_FB && tag->size >= 32) {
            const uint8_t* data = tag_payload(tag);
            const uint64_t addr =
                static_cast<uint64_t>(data[0]) |
                (static_cast<uint64_t>(data[1]) << 8) |
                (static_cast<uint64_t>(data[2]) << 16) |
                (static_cast<uint64_t>(data[3]) << 24) |
                (static_cast<uint64_t>(data[4]) << 32) |
                (static_cast<uint64_t>(data[5]) << 40) |
                (static_cast<uint64_t>(data[6]) << 48) |
                (static_cast<uint64_t>(data[7]) << 56);

            const uint32_t pitch = static_cast<uint32_t>(data[8]) |
                                   (static_cast<uint32_t>(data[9]) << 8) |
                                   (static_cast<uint32_t>(data[10]) << 16) |
                                   (static_cast<uint32_t>(data[11]) << 24);

            const uint32_t width = static_cast<uint32_t>(data[12]) |
                                   (static_cast<uint32_t>(data[13]) << 8) |
                                   (static_cast<uint32_t>(data[14]) << 16) |
                                   (static_cast<uint32_t>(data[15]) << 24);

            const uint32_t height = static_cast<uint32_t>(data[16]) |
                                    (static_cast<uint32_t>(data[17]) << 8) |
                                    (static_cast<uint32_t>(data[18]) << 16) |
                                    (static_cast<uint32_t>(data[19]) << 24);

            const uint8_t bpp = data[20];

            if (addr != 0 && width > 0 && height > 0 && bpp >= 8) {
                result.valid = true;
                result.info.phys_addr  = static_cast<uintptr_t>(addr);
                result.info.virt_addr = static_cast<uintptr_t>(addr);
                result.info.width     = width;
                result.info.height    = height;
                result.info.pitch     = pitch != 0 ? pitch : width * (bpp / 8);
                result.info.bpp       = bpp;
                result.info.red_mask  = 0xFF;
                result.info.green_mask = 0xFF;
                result.info.blue_mask = 0xFF;
                return result;
            }
        }

        offset += (tag->size + 7) & ~7u;
    }

    return result;
}

} // namespace vga
} // namespace hal
