#ifndef OMEGA_KERNEL_FRAMEBUFFER_HPP
#define OMEGA_KERNEL_FRAMEBUFFER_HPP

#include "std/cstdint.hpp"

namespace display {

void framebuffer_init();
bool framebuffer_active();

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void draw_string(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg);
void put_pixel(uint32_t x, uint32_t y, uint32_t color);

bool framebuffer_self_test();

} // namespace display

#endif // OMEGA_KERNEL_FRAMEBUFFER_HPP
