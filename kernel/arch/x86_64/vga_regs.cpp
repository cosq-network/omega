#include "std/cstdint.hpp"

namespace hal {
namespace vga {

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void wait_for_vsync() {
    /* Input Status Register 1 — wait for vertical retrace (bit 0 clear then set). */
    for (uint32_t i = 0; i < 100000; ++i) if ((inb(0x3DA) & 0x08) != 0) break;
    for (uint32_t i = 0; i < 100000; ++i) if ((inb(0x3DA) & 0x08) == 0) break;
}

void crtc_write(uint8_t index, uint8_t value) {
    outb(0x3D4, index);
    outb(0x3D5, value);
}

uint8_t crtc_read(uint8_t index) {
    outb(0x3D4, index);
    return inb(0x3D5);
}

void bochs_write(uint16_t index, uint16_t value) {
    outw(0x01CE, index);
    outw(0x01CF, value);
}

uint16_t bochs_read(uint16_t index) {
    outw(0x01CE, index);
    return inw(0x01CF);
}

} // namespace vga
} // namespace hal
