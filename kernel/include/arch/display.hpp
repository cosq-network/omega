#ifndef OMEGA_HAL_DISPLAY_HPP
#define OMEGA_HAL_DISPLAY_HPP

#include "std/cstdint.hpp"

namespace hal {

enum class DisplayBackend : uint8_t {
    None = 0,
    VgaText,
    BochsVbe,
    BootFramebuffer,
};

struct FramebufferInfo {
    uintptr_t phys_addr;
    uintptr_t virt_addr;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
    uint8_t   red_mask;
    uint8_t   green_mask;
    uint8_t   blue_mask;
};

struct DisplayCapabilities {
    bool text_mode;
    bool linear_framebuffer;
};

class Display {
public:
    static void init();
    static DisplayBackend active_backend();
    static DisplayCapabilities capabilities();
    static const char* backend_name();

    static void text_clear(uint8_t attr);
    static void text_putc(uint8_t col, uint8_t row, char c, uint8_t attr);
    static void text_set_cursor(uint8_t col, uint8_t row);
    static void text_scroll_up(uint8_t attr);
    static uint16_t text_peek(uint8_t col, uint8_t row);

    static bool set_mode(uint32_t width, uint32_t height, uint8_t bpp);
    static const FramebufferInfo* framebuffer();
    static void flush();

    static bool run_self_tests();
};

} // namespace hal

#endif // OMEGA_HAL_DISPLAY_HPP
