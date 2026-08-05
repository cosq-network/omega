#ifndef OMEGA_KERNEL_KPRINT_HPP
#define OMEGA_KERNEL_KPRINT_HPP

#include "std/cstdint.hpp"

namespace kernel {
    void kputc(char c);
    void kputs(const char* str);
    void kprint_hex(uint64_t val);
    void kprint_dec(uint64_t val);
    void kprintf(const char* fmt, ...);
}

#endif // OMEGA_KERNEL_KPRINT_HPP
