#ifndef OMEGA_KERNEL_SYSCALL_HPP
#define OMEGA_KERNEL_SYSCALL_HPP

#include "std/cstdint.hpp"

namespace syscall {

enum SyscallNumber {
    SYS_YIELD  = 1,
    SYS_WRITE  = 2,
    SYS_EXIT   = 3,
    SYS_OPEN   = 4,
    SYS_READ   = 5,
    SYS_CLOSE  = 6,
    SYS_FORK   = 7,
    SYS_EXECVE = 8,
};

class SyscallDispatcher {
public:
    static void init();
    static int64_t dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
};

} // namespace syscall

extern "C" int64_t sys_call(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#endif // OMEGA_KERNEL_SYSCALL_HPP
