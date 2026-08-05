#include "kernel/kprint.hpp"
#include "arch/uart.hpp"

// Minimal stdarg implementations for freestanding C++
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)    __builtin_va_arg(v, l)

namespace kernel {

void kputc(char c) {
    hal::uart_putc(c);
}

void kputs(const char* str) {
    hal::uart_puts(str);
}

void kprint_hex(uint64_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    kputs("0x");
    if (val == 0) {
        kputc('0');
        return;
    }
    char buf[16];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = hex_chars[val & 0xF];
        val >>= 4;
    }
    for (int i = idx - 1; i >= 0; --i) {
        kputc(buf[i]);
    }
}

void kprint_dec(uint64_t val) {
    if (val == 0) {
        kputc('0');
        return;
    }
    char buf[20];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; --i) {
        kputc(buf[i]);
    }
}

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            ++i;
            switch (fmt[i]) {
                case 's': {
                    const char* s = va_arg(args, const char*);
                    kputs(s ? s : "(null)");
                    break;
                }
                case 'c': {
                    char c = static_cast<char>(va_arg(args, int));
                    kputc(c);
                    break;
                }
                case 'x':
                case 'p': {
                    uint64_t val = va_arg(args, uint64_t);
                    kprint_hex(val);
                    break;
                }
                case 'd':
                case 'u': {
                    uint64_t val = va_arg(args, uint64_t);
                    kprint_dec(val);
                    break;
                }
                case '%':
                    kputc('%');
                    break;
                default:
                    kputc('%');
                    kputc(fmt[i]);
                    break;
            }
        } else {
            kputc(fmt[i]);
        }
    }
    va_end(args);
}

} // namespace kernel
