#include "vga_internal.hpp"

extern "C" uint64_t multiboot_info_ptr = 0;
extern "C" uint32_t multiboot_boot_magic = 0;

static void put32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value); p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16); p[3] = static_cast<uint8_t>(value >> 24);
}
static void put64(uint8_t* p, uint64_t value) {
    put32(p, static_cast<uint32_t>(value)); put32(p + 4, static_cast<uint32_t>(value >> 32));
}

int main() {
    uint8_t info[64];
    for (size_t i = 0; i < sizeof(info); ++i) info[i] = 0;
    put32(info, 56); put32(info + 4, 0);
    put32(info + 8, 8); put32(info + 12, 40);
    put64(info + 16, 0x200000); put32(info + 24, 8); put32(info + 28, 2);
    put32(info + 32, 1); info[36] = 32; info[37] = 1;
    info[40] = 16; info[41] = 8; info[42] = 8; info[43] = 8;
    info[44] = 0; info[45] = 8;
    put32(info + 48, 0); put32(info + 52, 8);
    multiboot_info_ptr = reinterpret_cast<uintptr_t>(info);
    multiboot_boot_magic = 0x36D76289u;

    const hal::vga::BootFramebufferResult result = hal::vga::boot_framebuffer_probe();
    if (!result.valid || result.info.phys_addr != 0x200000 || result.info.size != 8 ||
        result.info.red_shift != 16 || result.info.green_shift != 8 || result.info.blue_shift != 0) return 1;
    multiboot_boot_magic = 0;
    if (hal::vga::boot_framebuffer_probe().valid) return 2;
    return 0;
}
