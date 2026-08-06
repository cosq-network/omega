#include "arch/uart.hpp"

namespace hal {

// Use OpenSBI console write call via ecall serial console
static inline void sbi_console_putchar(char c) {
    register uintptr_t a0 asm("a0") = static_cast<uintptr_t>(c);
    register uintptr_t a6 asm("a6") = 0; // SBI Extension: Console Putchar (0x01)
    register uintptr_t a7 asm("a7") = 1;
    asm volatile ("ecall" : : "r"(a0), "r"(a6), "r"(a7) : "memory");
}

void uart_init() {
}

void uart_putc(char c) {
    sbi_console_putchar(c);
}

void uart_puts(const char* str) {
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

} // namespace hal
