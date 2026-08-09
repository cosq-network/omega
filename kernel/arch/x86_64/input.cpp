#include "arch/input.hpp"
#include "kernel/input.hpp"
#include "kernel/kprint.hpp"

namespace {
input::Ps2Decoder decoder;
static inline void outb(uint16_t port, uint8_t value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static void wait_write() { for (uint32_t i = 0; i < 100000; ++i) if ((inb(0x64) & 2) == 0) return; }
static void command(uint8_t value) { wait_write(); outb(0x64, value); }
static void data(uint8_t value) { wait_write(); outb(0x60, value); }
}

namespace hal {
void input_init() {
    command(0xad); command(0xa7);
    for (uint32_t i = 0; i < 32 && (inb(0x64) & 1); ++i) (void)inb(0x60);
    command(0xae); command(0xa8); command(0xd4); data(0xf4);
    kernel::kprintf("[+] x86_64 PS/2 keyboard and mouse input initialized (polling).\n");
}
void input_poll() {
    for (uint32_t i = 0; i < 32 && (inb(0x64) & 1); ++i) {
        uint8_t status = inb(0x64); uint8_t value = inb(0x60);
        if (status & 0x20) decoder.mouse_byte(value); else decoder.keyboard_byte(value);
    }
}
}
