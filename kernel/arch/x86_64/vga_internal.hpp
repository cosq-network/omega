#ifndef OMEGA_X86_64_VGA_INTERNAL_HPP
#define OMEGA_X86_64_VGA_INTERNAL_HPP

#include "arch/display.hpp"
#include "std/cstdint.hpp"

namespace hal {
namespace vga {

void wait_for_vsync();
void crtc_write(uint8_t index, uint8_t value);
uint8_t crtc_read(uint8_t index);
void bochs_write(uint16_t index, uint16_t value);
uint16_t bochs_read(uint16_t index);

bool text_buffer_accessible();
void text_clear(uint8_t attr);
void text_putc(uint8_t col, uint8_t row, char c, uint8_t attr);
void text_set_cursor(uint8_t col, uint8_t row);
void text_scroll_up(uint8_t attr);
bool text_self_test();
uint16_t text_peek(uint8_t col, uint8_t row);
uint32_t text_cols();
uint32_t text_rows();

bool map_framebuffer(FramebufferInfo* info);
bool bochs_verify_mode(uint32_t width, uint32_t height, uint8_t bpp);

bool bochs_probe(uintptr_t* out_fb_phys);
bool bochs_set_mode(uint32_t width, uint32_t height, uint8_t bpp, FramebufferInfo* out);
bool bochs_self_test(uintptr_t fb_phys);

struct BootFramebufferResult {
    bool valid;
    FramebufferInfo info;
};
BootFramebufferResult boot_framebuffer_probe();

} // namespace vga
} // namespace hal

#endif
