#ifndef OMEGA_HAL_UART_HPP
#define OMEGA_HAL_UART_HPP

#include "std/cstdint.hpp"

namespace hal {
    void uart_init();
    void uart_putc(char c);
    void uart_puts(const char* str);
}

#endif // OMEGA_HAL_UART_HPP
