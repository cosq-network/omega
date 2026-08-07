#ifndef OMEGA_KERNEL_CONSOLE_HPP
#define OMEGA_KERNEL_CONSOLE_HPP

#include "std/cstdint.hpp"

namespace display {

enum ConsoleTarget : uint32_t {
    CONSOLE_SERIAL       = 1u << 0,
    CONSOLE_VGA_TEXT     = 1u << 1,
    CONSOLE_FRAMEBUFFER  = 1u << 2,
    CONSOLE_ALL          = 0xFFFFFFFFu,
};

class Console {
public:
    static void init(uint32_t targets = CONSOLE_ALL);
    static void putchar(char c);
    static void write(const char* s, size_t len);
    static void clear();
    static void set_targets(uint32_t targets);
    static uint32_t targets();

    static bool self_test();
};

} // namespace display

#endif // OMEGA_KERNEL_CONSOLE_HPP
