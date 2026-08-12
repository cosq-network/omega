#ifndef OMEGA_USER_SYSCALL_H
#define OMEGA_USER_SYSCALL_H

#include <stdint.h>

int64_t omega_syscall6(uint64_t number, uint64_t a1, uint64_t a2,
                       uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);

#endif
