#include "arch/display.hpp"
#include "kernel/framebuffer.hpp"

namespace {
hal::FramebufferInfo framebuffer;
const hal::FramebufferInfo* current_framebuffer() { return &framebuffer; }
}

namespace hal {
const FramebufferInfo* Display::framebuffer() { return current_framebuffer(); }
}

static bool equal_byte(uint8_t actual, uint8_t expected) { return actual == expected; }

int main() {
    uint8_t pixels[128];
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = 0;

    framebuffer.phys_addr = reinterpret_cast<uintptr_t>(pixels);
    framebuffer.virt_addr = framebuffer.phys_addr;
    framebuffer.width = 2;
    framebuffer.height = 2;
    framebuffer.pitch = 8;
    framebuffer.size = sizeof(pixels);

    framebuffer.bpp = 16;
    framebuffer.red_mask = 0x1f; framebuffer.green_mask = 0x3f; framebuffer.blue_mask = 0x1f;
    framebuffer.red_shift = 11; framebuffer.green_shift = 5; framebuffer.blue_shift = 0;
    display::framebuffer_init();
    display::put_pixel(0, 0, 0xff0000);
    if (!equal_byte(pixels[0], 0x00) || !equal_byte(pixels[1], 0xf8)) return 1;

    framebuffer.pitch = 8; framebuffer.size = sizeof(pixels); framebuffer.bpp = 24;
    framebuffer.red_mask = 0xff; framebuffer.green_mask = 0xff; framebuffer.blue_mask = 0xff;
    framebuffer.red_shift = 16; framebuffer.green_shift = 8; framebuffer.blue_shift = 0;
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = 0;
    display::framebuffer_init();
    display::put_pixel(0, 0, 0x123456);
    if (pixels[0] != 0x56 || pixels[1] != 0x34 || pixels[2] != 0x12) return 2;

    framebuffer.pitch = 8; framebuffer.size = sizeof(pixels); framebuffer.bpp = 32;
    framebuffer.red_mask = 0xff; framebuffer.green_mask = 0xff; framebuffer.blue_mask = 0xff;
    framebuffer.red_shift = 0; framebuffer.green_shift = 8; framebuffer.blue_shift = 16;
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = 0;
    display::framebuffer_init();
    display::put_pixel(0, 0, 0x123456);
    if (pixels[0] != 0x12 || pixels[1] != 0x34 || pixels[2] != 0x56 || pixels[3] != 0xff) return 3;

    return display::framebuffer_self_test() ? 0 : 4;
}
