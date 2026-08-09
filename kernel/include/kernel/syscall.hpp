#ifndef OMEGA_KERNEL_SYSCALL_HPP
#define OMEGA_KERNEL_SYSCALL_HPP

#include "std/cstdint.hpp"

namespace syscall {

enum SyscallNumber {
#if defined(__x86_64__)
    SYS_READ       = 0,
    SYS_WRITE      = 1,
    SYS_OPEN       = 2,
    SYS_CLOSE      = 3,
    SYS_MMAP       = 9,
    SYS_MUNMAP     = 11,
    SYS_BRK        = 12,
    SYS_SCHED_YIELD= 24,
    SYS_FORK       = 57,
    SYS_EXECVE     = 59,
    SYS_EXIT       = 60,
    SYS_WAIT4      = 61,
    SYS_OPENAT     = 257,
    SYS_UMASK      = 95,
    SYS_CHMOD      = 90,
    SYS_CHOWN      = 92,
    SYS_SETUID     = 105,
    SYS_SETGID     = 106,
    SYS_GETUID     = 102,
    SYS_GETEUID    = 107,
    SYS_GETGID     = 104,
    SYS_GETEGID    = 108,
    SYS_GETGROUPS  = 115,
    SYS_SETGROUPS  = 116,
    SYS_GETRESUID  = 118,
    SYS_GETRESGID  = 120,
    SYS_SETRESUID  = 117,
    SYS_SETRESGID  = 119,
    SYS_FCHOWNAT   = 260,
    SYS_FCHMODAT   = 268,
#else
    // Linux AArch64 and RV64 use the same modern syscall numbering for this
    // subset. fork is represented by clone in those ABIs.
    SYS_CLOSE      = 57,
    SYS_READ       = 63,
    SYS_WRITE      = 64,
    SYS_EXIT       = 93,
    SYS_BRK        = 214,
    SYS_MUNMAP     = 215,
    SYS_MMAP       = 222,
    SYS_SCHED_YIELD= 124,
    SYS_EXECVE     = 221,
    SYS_WAIT4      = 260,
    SYS_OPENAT     = 56,
    SYS_FORK       = 220,
    SYS_UMASK      = 166,
    SYS_FCHMODAT   = 53,
    SYS_FCHOWNAT   = 54,
    SYS_CHMOD      = 1025,
    SYS_CHOWN      = 1026,
    SYS_SETGID     = 144,
    SYS_SETUID     = 146,
    SYS_GETUID     = 174,
    SYS_GETEUID    = 175,
    SYS_GETGID     = 176,
    SYS_GETEGID    = 177,
    SYS_GETRESUID  = 148,
    SYS_GETRESGID  = 150,
    SYS_SETRESUID  = 147,
    SYS_SETRESGID  = 149,
    SYS_GETGROUPS  = 158,
    SYS_SETGROUPS  = 159,
#endif
#if !defined(__x86_64__)
    SYS_OPEN       = 1024, // deprecated Omega compatibility entry
#endif
    SYS_YIELD      = SYS_SCHED_YIELD,
    SYS_CLONE      = SYS_FORK,
    // Stable Omega input extension range, intentionally outside Linux's
    // architecture-specific syscall number spaces.
    SYS_INPUT_READ       = 0x4000,
    SYS_INPUT_POLL       = 0x4001,
    SYS_INPUT_SUBSCRIBE  = 0x4002,
    // Pre-Phase-7 compatibility aliases. New userspace must use the Linux
    // numbers above; these are retained only for old kernel test binaries.
    OMEGA_SYS_YIELD = 1,
    OMEGA_SYS_WRITE = 2,
    OMEGA_SYS_EXIT  = 3,
    OMEGA_SYS_OPEN  = 4,
    OMEGA_SYS_READ  = 5,
    OMEGA_SYS_CLOSE = 6,
    OMEGA_SYS_FORK  = 7,
    OMEGA_SYS_EXECVE= 8,
};

class SyscallDispatcher {
public:
    static void init();
    static int64_t dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
    static int64_t dispatch6(uint64_t sys_num, uint64_t arg1, uint64_t arg2,
                             uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);
};

} // namespace syscall

extern "C" int64_t sys_call(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern "C" int64_t sys_call6(uint64_t sys_num, uint64_t arg1, uint64_t arg2,
                              uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

#endif // OMEGA_KERNEL_SYSCALL_HPP
