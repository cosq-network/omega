// Omega startup glue for musl.
//
// musl's crt1.o provides _start and __libc_start_main, which expect the
// Linux process stack: argc, argv, envp, auxv. Omega's ELF loader now
// builds exactly that layout, so no custom entry is required for static
// binaries. This file provides the few Omega-specific runtime hooks musl
// needs that are not already in libc.a.

#include <stddef.h>

// musl expects __dso_handle for cxa_atexit registration.
void* __dso_handle = (void*)0;

// musl's cxa_atexit/cxa_finalize are in libc.a; these weak aliases ensure
// static C++ programs link cleanly even before full C++ ABI support.
int __cxa_atexit(void (*func)(void*), void* arg, void* dso) __attribute__((weak));
int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    (void)func; (void)arg; (void)dso;
    return 0; // no-op: Omega's exit() path reaps the process
}
void __cxa_finalize(void* dso) __attribute__((weak));
void __cxa_finalize(void* dso) { (void)dso; }

// Omega's exit syscall terminates the process; musl's exit() calls
// __libc_exit_fini then _Exit. We ensure _Exit goes through the syscall.
void _Exit(int status) __attribute__((noreturn));
void _Exit(int status) {
    extern long __omega_syscall3(long, long, long, long);
#if defined(__x86_64__)
    const long nr_exit = 60;
#else
    const long nr_exit = 93;
#endif
    __omega_syscall3(nr_exit, status, 0, 0);
    for (;;) { }
}
