#include "kernel/framebuffer.hpp"
#include "arch/display.hpp"

namespace display {

static const hal::FramebufferInfo* active_fb = nullptr;

static uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    if (active_fb == nullptr) {
        return 0;
    }
    if (active_fb->bpp == 32) {
        return (static_cast<uint32_t>(r) << active_fb->red_shift) |
               (static_cast<uint32_t>(g) << active_fb->green_shift) |
               (static_cast<uint32_t>(b) << active_fb->blue_shift);
    }
    if (active_fb->bpp == 24) {
        return (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               static_cast<uint32_t>(b);
    }
    if (active_fb->bpp == 16) {
        return ((static_cast<uint32_t>(r) >> 3) & active_fb->red_mask) << active_fb->red_shift |
               ((static_cast<uint32_t>(g) >> (active_fb->green_mask == 0x3F ? 2 : 3)) & active_fb->green_mask) << active_fb->green_shift |
               ((static_cast<uint32_t>(b) >> 3) & active_fb->blue_mask) << active_fb->blue_shift;
    }
    return r;
}

void framebuffer_init() {
    active_fb = hal::Display::framebuffer();
}

bool framebuffer_active() {
    return active_fb != nullptr && active_fb->virt_addr != 0;
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer_active() || active_fb == nullptr) {
        return;
    }
    if (x >= active_fb->width || y >= active_fb->height) {
        return;
    }

    const uint32_t bpp = active_fb->bpp;
    uint8_t* base = reinterpret_cast<uint8_t*>(active_fb->virt_addr);
    uint8_t* pixel = base + y * active_fb->pitch + x * (bpp / 8);

    if (bpp == 32) {
        pixel[0] = static_cast<uint8_t>(color & 0xFF);
        pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
        pixel[2] = static_cast<uint8_t>((color >> 16) & 0xFF);
        pixel[3] = 0xFF;
    } else if (bpp == 24) {
        pixel[0] = static_cast<uint8_t>(color & 0xFF);
        pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
        pixel[2] = static_cast<uint8_t>((color >> 16) & 0xFF);
    } else if (bpp == 16) {
        pixel[0] = static_cast<uint8_t>(color & 0xFF);
        pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
    } else if (bpp == 8) {
        pixel[0] = static_cast<uint8_t>(color & 0xFF);
    }
}

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (w == 0 || h == 0) {
        return;
    }
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            put_pixel(x + col, y + row, color);
        }
    }
}

bool framebuffer_self_test() {
    if (!framebuffer_active()) {
        return false;
    }
    const uint32_t fg = pack_color(0xFF, 0xFF, 0xFF);
    put_pixel(10, 10, fg);
    const uint8_t* base = reinterpret_cast<const uint8_t*>(active_fb->virt_addr);
    const uint32_t offset = 10 * active_fb->pitch + 10 * (active_fb->bpp / 8);
    return base[offset] != 0;
}

} // namespace display
