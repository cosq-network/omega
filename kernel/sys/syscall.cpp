#include "kernel/syscall.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/kprint.hpp"

namespace syscall {

void SyscallDispatcher::init() {
    kernel::kprintf("[+] System Call ABI Engine Initialized.\n");
}

int64_t SyscallDispatcher::dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (void)arg3;
    switch (sys_num) {
        case SYS_YIELD:
            scheduler::Scheduler::yield();
            return 0;
        case SYS_WRITE: {
            const char* buf = reinterpret_cast<const char*>(arg1);
            size_t count = static_cast<size_t>(arg2);
            for (size_t i = 0; i < count; ++i) {
                kernel::kputc(buf[i]);
            }
            return count;
        }
        case SYS_EXIT:
            kernel::kprintf("[!] Syscall Exit Called with status: %d\n", arg1);
            return 0;
        default:
            kernel::kprintf("[!] Invalid Syscall Number: %d\n", sys_num);
            return -1;
    }
}

} // namespace syscall

extern "C" int64_t sys_call(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return syscall::SyscallDispatcher::dispatch(sys_num, arg1, arg2, arg3);
}
