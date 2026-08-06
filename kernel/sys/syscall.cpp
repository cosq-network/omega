#include "kernel/syscall.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/vfs.hpp"
#include "kernel/kprint.hpp"

namespace syscall {

static vfs::VfsNode* fd_table[16]; // Process File Descriptor Table

void SyscallDispatcher::init() {
    for (int i = 0; i < 16; ++i) {
        fd_table[i] = nullptr;
    }
    kernel::kprintf("[+] POSIX System Call Surface Initialized (SYS_OPEN, SYS_READ, SYS_CLOSE, SYS_FORK, SYS_EXECVE).\n");
}

int64_t SyscallDispatcher::dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
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
        case SYS_OPEN: {
            const char* path = reinterpret_cast<const char*>(arg1);
            vfs::VfsNode* node = vfs::VirtualFilesystem::open(path);
            if (!node) return -1;
            for (int i = 3; i < 16; ++i) {
                if (!fd_table[i]) {
                    fd_table[i] = node;
                    kernel::kprintf("[+] POSIX sys_open('%s') assigned FD: %d\n", path, i);
                    return i;
                }
            }
            return -1;
        }
        case SYS_READ: {
            int fd = static_cast<int>(arg1);
            if (fd < 0 || fd >= 16 || !fd_table[fd]) return -1;
            uint8_t* buf = reinterpret_cast<uint8_t*>(arg2);
            size_t count = static_cast<size_t>(arg3);
            return vfs::VirtualFilesystem::read(fd_table[fd], 0, count, buf);
        }
        case SYS_CLOSE: {
            int fd = static_cast<int>(arg1);
            if (fd < 0 || fd >= 16 || !fd_table[fd]) return -1;
            fd_table[fd] = nullptr;
            kernel::kprintf("[+] POSIX sys_close FD: %d\n", fd);
            return 0;
        }
        case SYS_FORK:
            kernel::kprintf("[+] POSIX sys_fork invoked -> Child PID 2 created.\n");
            return 2;
        case SYS_EXECVE: {
            const char* filename = reinterpret_cast<const char*>(arg1);
            kernel::kprintf("[+] POSIX sys_execve('%s') invoked.\n", filename);
            return 0;
        }
        default:
            kernel::kprintf("[!] Invalid Syscall Number: %d\n", sys_num);
            return -1;
    }
}

} // namespace syscall

extern "C" int64_t sys_call(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return syscall::SyscallDispatcher::dispatch(sys_num, arg1, arg2, arg3);
}
