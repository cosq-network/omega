// Omega shim for musl's internal syscall layer.
//
// musl's arch code defines __syscall() with the Linux ABI per-ISA. Omega
// uses the same Linux syscall numbers and conventions, so the shim is a
// pass-through to the Omega kernel's dispatch. We provide __syscall and
// __syscall_cp (the cancellation-point variant), which musl's internal
// headers reference.

#if defined(__x86_64__)
#define OMEGA_NR_READ 0
#define OMEGA_NR_WRITE 1
#define OMEGA_NR_OPEN 2
#define OMEGA_NR_CLOSE 3
#define OMEGA_NR_FSTAT 5
#define OMEGA_NR_LSEEK 8
#define OMEGA_NR_MMAP 9
#define OMEGA_NR_MUNMAP 11
#define OMEGA_NR_BRK 12
#define OMEGA_NR_EXIT 60
#elif defined(__aarch64__)
#define OMEGA_NR_READ 63
#define OMEGA_NR_WRITE 64
#define OMEGA_NR_CLOSE 57
#define OMEGA_NR_FSTAT 80
#define OMEGA_NR_LSEEK 62
#define OMEGA_NR_MMAP 222
#define OMEGA_NR_MUNMAP 215
#define OMEGA_NR_BRK 214
#define OMEGA_NR_EXIT 93
#elif defined(__riscv)
#define OMEGA_NR_READ 63
#define OMEGA_NR_WRITE 64
#define OMEGA_NR_CLOSE 57
#define OMEGA_NR_FSTAT 80
#define OMEGA_NR_LSEEK 62
#define OMEGA_NR_MMAP 222
#define OMEGA_NR_MUNMAP 215
#define OMEGA_NR_BRK 214
#define OMEGA_NR_EXIT 93
#endif

extern long __omega_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6);
extern long __omega_syscall3(long nr, long a1, long a2, long a3);

// musl's internal __syscall() — called with musl's own syscall numbers,
// which for the subset Omega implements are the Linux numbers.
long __syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
    return __omega_syscall6(nr, a1, a2, a3, a4, a5, a6);
}

// Cancellation-point variant: no threads yet, so identical to __syscall.
long __syscall_cp(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
    return __omega_syscall6(nr, a1, a2, a3, a4, a5, a6);
}

// sbrk/brk: musl's malloc uses mmap, not brk, on Linux by default, but
// provide brk for compatibility with programs that call it directly.
long __omega_brk(void* address) {
    return __omega_syscall3(OMEGA_NR_BRK, (long)address, 0, 0);
}
