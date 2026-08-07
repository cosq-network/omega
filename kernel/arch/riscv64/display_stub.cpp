#include "arch/display.hpp"

namespace hal {

void Display::init() {}
DisplayBackend Display::active_backend() { return DisplayBackend::None; }
DisplayCapabilities Display::capabilities() { return {}; }
const char* Display::backend_name() { return "None"; }
void Display::text_clear(uint8_t) {}
void Display::text_putc(uint8_t, uint8_t, char, uint8_t) {}
void Display::text_set_cursor(uint8_t, uint8_t) {}
void Display::text_scroll_up(uint8_t) {}
uint16_t Display::text_peek(uint8_t, uint8_t) { return 0; }
bool Display::set_mode(uint32_t, uint32_t, uint8_t) { return false; }
const FramebufferInfo* Display::framebuffer() { return nullptr; }
void Display::flush() {}
bool Display::run_self_tests() { return true; }

} // namespace hal
