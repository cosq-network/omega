#include "arch/uart.hpp"

namespace hal {

// ARM PL011 UART Base Address for QEMU 'virt' machine
static volatile uint32_t* const PL011_DR = reinterpret_cast<uint32_t*>(0x09000000);
static volatile uint32_t* const PL011_FR = reinterpret_cast<uint32_t*>(0x09000018);

void uart_init() {
    // Hardware PL011 UART is pre-initialized by QEMU bios/firmware for virt board
}

void uart_putc(char c) {
    // Wait until Transmit FIFO is not full (TXFF bit 5 in Flag Register)
    // Keep early boot diagnostics live even if a firmware/QEMU UART reports a
    // stale TXFF bit. The FIFO is only 16 bytes, so a bounded wait is safe for
    // this single-core bring-up path.
    for (uint32_t spins = 0; (*PL011_FR & (1 << 5)) && spins < 100000; ++spins) { }
    *PL011_DR = static_cast<uint32_t>(c);
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
