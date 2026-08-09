#include "kernel/framebuffer.hpp"
#include "arch/display.hpp"

namespace display {

static const hal::FramebufferInfo* active_fb = nullptr;

static uint32_t channel_to_bits(uint8_t value, uint8_t mask) {
    if (mask == 0) return 0;
    uint32_t max = mask;
    uint32_t width = 0;
    while (max != 0) { ++width; max >>= 1; }
    const uint32_t scaled = (static_cast<uint32_t>(value) * ((1u << width) - 1u) + 127u) / 255u;
    return scaled & mask;
}

static uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    if (active_fb == nullptr) {
        return 0;
    }
    return (channel_to_bits(r, active_fb->red_mask) << active_fb->red_shift) |
           (channel_to_bits(g, active_fb->green_mask) << active_fb->green_shift) |
           (channel_to_bits(b, active_fb->blue_mask) << active_fb->blue_shift);
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
    const uint32_t bytes_per_pixel = (bpp + 7) / 8;
    if ((bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) ||
        bytes_per_pixel == 0 || active_fb->pitch < active_fb->width * bytes_per_pixel ||
        active_fb->size < static_cast<uint64_t>(active_fb->pitch) * active_fb->height) return;
    uint8_t* base = reinterpret_cast<uint8_t*>(active_fb->virt_addr);
    uint8_t* pixel = base + static_cast<size_t>(y) * active_fb->pitch + x * bytes_per_pixel;
    const uint32_t packed = pack_color(static_cast<uint8_t>((color >> 16) & 0xFF),
                                       static_cast<uint8_t>((color >> 8) & 0xFF),
                                       static_cast<uint8_t>(color & 0xFF));
    for (uint32_t i = 0; i < bytes_per_pixel; ++i) pixel[i] = static_cast<uint8_t>(packed >> (i * 8));
    if (bpp == 32) pixel[3] = 0xFF;
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
    const uint32_t fg = 0xFFFFFF;
    const uint32_t packed = pack_color(0xFF, 0xFF, 0xFF);
    const uint32_t bytes_per_pixel = (active_fb->bpp + 7) / 8;
    if (active_fb->width == 0 || active_fb->height == 0 || bytes_per_pixel == 0 ||
        active_fb->size < static_cast<uint64_t>(active_fb->pitch) * active_fb->height) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(active_fb->virt_addr);
    uint8_t* mutable_base = reinterpret_cast<uint8_t*>(active_fb->virt_addr);
    uint8_t saved[4];
    for (uint32_t i = 0; i < bytes_per_pixel && i < 4; ++i) saved[i] = base[i];
    put_pixel(0, 0, fg);
    bool ok = true;
    for (uint32_t i = 0; i < bytes_per_pixel && i < 4; ++i) {
        const uint8_t expected = (active_fb->bpp == 32 && i == 3)
            ? 0xFF : static_cast<uint8_t>(packed >> (i * 8));
        if (base[i] != expected) ok = false;
    }
    for (uint32_t i = 0; i < bytes_per_pixel && i < 4; ++i) mutable_base[i] = saved[i];
    return ok;
}

} // namespace display
