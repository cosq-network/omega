#include <stdint.h>
#include <omega/errno.h>
#include <omega/stdio.h>
#include <omega/stdlib.h>
#include <omega/string.h>
#include <omega/syscall.h>
#include <omega/unistd.h>

int errno;

#if defined(__x86_64__)
#define OMEGA_NR_WRITE 1
#define OMEGA_NR_CLOSE 3
#define OMEGA_NR_YIELD 24
#define OMEGA_NR_EXIT 60
#else
#define OMEGA_NR_WRITE 64
#define OMEGA_NR_CLOSE 57
#define OMEGA_NR_YIELD 124
#define OMEGA_NR_EXIT 93
#endif

extern int main(int argc, char** argv, char** envp);

static uint8_t heap_area[4 * 1024];
static uint64_t heap_offset;

uint64_t strlen(const char* value) {
    uint64_t length = 0;
    if (!value) return 0;
    while (value[length]) ++length;
    return length;
}

int strcmp(const char* left, const char* right) {
    uint64_t i = 0;
    while (left[i] && left[i] == right[i]) ++i;
    return (unsigned char)left[i] == (unsigned char)right[i] ? 0 :
           ((unsigned char)left[i] < (unsigned char)right[i] ? -1 : 1);
}

void* memcpy(void* destination, const void* source, uint64_t count) {
    uint8_t* out = destination;
    const uint8_t* in = source;
    for (uint64_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}

void* memset(void* destination, int value, uint64_t count) {
    uint8_t* out = destination;
    for (uint64_t i = 0; i < count; ++i) out[i] = (uint8_t)value;
    return destination;
}

int memcmp(const void* left, const void* right, uint64_t count) {
    const uint8_t* a = left;
    const uint8_t* b = right;
    for (uint64_t i = 0; i < count; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

void* malloc(uint64_t size) {
    if (size == 0 || size > sizeof(heap_area) - heap_offset) return 0;
    size = (size + 15) & ~15ull;
    if (size > sizeof(heap_area) - heap_offset) return 0;
    void* result = heap_area + heap_offset;
    heap_offset += size;
    return result;
}

void free(void* pointer) { (void)pointer; }

ssize_t write(int fd, const void* buffer, uint64_t count) {
    int64_t result = omega_syscall6(OMEGA_NR_WRITE, (uint64_t)fd, (uint64_t)buffer, count, 0, 0, 0);
    if (result < 0) { errno = (int)-result; return -1; }
    return result;
}

int close(int fd) {
    int64_t result = omega_syscall6(OMEGA_NR_CLOSE, (uint64_t)fd, 0, 0, 0, 0, 0);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int sched_yield(void) {
    int64_t result = omega_syscall6(OMEGA_NR_YIELD, 0, 0, 0, 0, 0, 0);
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}

int fputs(const char* value, int fd) {
    ssize_t result = write(fd, value, strlen(value));
    return result < 0 ? -1 : 0;
}

int puts(const char* value) {
    if (fputs(value, 1) < 0 || write(1, "\n", 1) < 0) return -1;
    return 0;
}

void _exit(int status) {
    (void)omega_syscall6(OMEGA_NR_EXIT, (uint64_t)status, 0, 0, 0, 0, 0);
    for (;;) { }
}

void exit(int status) { _exit(status); }

void omega_start(uintptr_t* stack) {
    int argc = (int)stack[0];
    // The kernel provides the conventional contiguous process stack:
    // argc, argv[0..argc], NULL, envp[0..n], NULL, auxv. The first argv
    // pointer is therefore stack[1], not a pointer-to-argv stored there.
    char** argv = (char**)(stack + 1);
    char** envp = argv + argc + 1;
    exit(main(argc, argv, envp));
}
