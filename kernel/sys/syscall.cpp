#include "kernel/syscall.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/vfs.hpp"
#include "kernel/kprint.hpp"
#include "kernel/process.hpp"
#include "kernel/security.hpp"
#include "kernel/memory.hpp"
#include "kernel/input.hpp"

namespace syscall {

namespace {
constexpr int64_t ERR_EBADF = 9;
constexpr int64_t ERR_EFAULT = 14;
constexpr int64_t ERR_EINVAL = 22;
constexpr int64_t ERR_ENFILE = 23;
constexpr uintptr_t USER_LIMIT = 0x0000800000000000ull;

static vfs::VfsNode** fd_table() {
    process::Process* current = process::Manager::current();
    return current != nullptr ? current->fd_table : nullptr;
}

static bool valid_user_range(uintptr_t address, size_t length) {
    if (length == 0 || address >= USER_LIMIT || address > USER_LIMIT - length) return false;
    process::Process* current = process::Manager::current();
    if (current == nullptr) return false;
    for (uintptr_t offset = 0; offset < length;) {
        if (memory::VirtualMemoryManager::get_physical_address(
                &current->address_space, address + offset) == 0) return false;
        const size_t remaining = length - offset;
        const size_t page_remaining = memory::PAGE_SIZE - ((address + offset) & (memory::PAGE_SIZE - 1));
        offset += remaining < page_remaining ? remaining : page_remaining;
    }
    return true;
}

static bool copy_path(uintptr_t user_path, char* path, size_t capacity) {
    if (!path || capacity == 0) return false;
    for (size_t i = 0; i + 1 < capacity; ++i) {
        if (!valid_user_range(user_path + i, 1)) return false;
        path[i] = reinterpret_cast<const char*>(user_path)[i];
        if (path[i] == '\0') return true;
    }
    path[capacity - 1] = '\0';
    return false;
}
}

void SyscallDispatcher::init() {
    vfs::VfsNode** table = fd_table();
    if (table != nullptr) {
        for (int i = 0; i < 16; ++i) table[i] = nullptr;
    }
    kernel::kprintf("[+] POSIX System Call Surface Initialized (SYS_OPEN, SYS_READ, SYS_CLOSE, SYS_FORK, SYS_EXECVE).\n");
}

int64_t SyscallDispatcher::dispatch(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return dispatch6(sys_num, arg1, arg2, arg3, 0, 0, 0);
}

int64_t SyscallDispatcher::dispatch6(uint64_t sys_num, uint64_t arg1, uint64_t arg2,
                                     uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    switch (sys_num) {
        case SYS_SCHED_YIELD:
            scheduler::Scheduler::yield();
            return 0;
        case SYS_INPUT_READ: {
            const size_t capacity = static_cast<size_t>(arg2);
            if (capacity == 0 || capacity > input::EventQueue::CAPACITY ||
                !valid_user_range(arg1, capacity * sizeof(input::InputEvent))) return -ERR_EFAULT;
            return static_cast<int64_t>(input::Manager::read(reinterpret_cast<input::InputEvent*>(arg1), capacity));
        }
        case SYS_INPUT_POLL:
            return static_cast<int64_t>(input::Manager::available());
        case SYS_INPUT_SUBSCRIBE:
            return input::Manager::subscribe(arg1);
        case SYS_WRITE: {
            const int fd = static_cast<int>(arg1);
            const char* buf = reinterpret_cast<const char*>(arg2);
            size_t count = static_cast<size_t>(arg3);
            if ((fd != 1 && fd != 2) || !valid_user_range(arg2, count)) return -ERR_EFAULT;
            for (size_t i = 0; i < count; ++i) {
                kernel::kputc(buf[i]);
            }
            return count;
        }
        case SYS_EXIT:
            kernel::kprintf("[!] Syscall Exit Called with status: %d\n", arg1);
            return process::Manager::exit(static_cast<int32_t>(arg1));
        case SYS_OPEN:
        case SYS_OPENAT: {
            const uintptr_t user_path = sys_num == SYS_OPENAT ? arg2 : arg1;
            char path[256]{};
            if (!copy_path(user_path, path, sizeof(path))) return -ERR_EFAULT;
            const uint64_t open_flags = sys_num == SYS_OPENAT ? arg3 : arg2;
            uint32_t access = security::MAY_READ;
            if ((open_flags & 3) == 1) access = security::MAY_WRITE;
            else if ((open_flags & 3) == 2) access = security::MAY_READ | security::MAY_WRITE;
            vfs::VfsNode* node = vfs::VirtualFilesystem::open(path, access);
            if (!node) return -ERR_EINVAL;
            vfs::VfsNode** table = fd_table();
            if (!table) return -ERR_EFAULT;
            for (int i = 3; i < 16; ++i) {
                if (!table[i]) {
                    table[i] = node;
                    kernel::kprintf("[+] POSIX sys_open('%s') assigned FD: %d\n", path, i);
                    return i;
                }
            }
            return -ERR_ENFILE;
        }
        case SYS_READ: {
            int fd = static_cast<int>(arg1);
            vfs::VfsNode** table = fd_table();
            if (!table || fd < 0 || fd >= 16 || !table[fd]) return -ERR_EBADF;
            uint8_t* buf = reinterpret_cast<uint8_t*>(arg2);
            size_t count = static_cast<size_t>(arg3);
            if (!valid_user_range(arg2, count)) return -ERR_EFAULT;
            return vfs::VirtualFilesystem::read(table[fd], 0, count, buf);
        }
        case SYS_CLOSE: {
            int fd = static_cast<int>(arg1);
            vfs::VfsNode** table = fd_table();
            if (!table || fd < 0 || fd >= 16 || !table[fd]) return -ERR_EBADF;
            table[fd] = nullptr;
            kernel::kprintf("[+] POSIX sys_close FD: %d\n", fd);
            return 0;
        }
        case SYS_FORK:
            return process::Manager::fork();
        case SYS_EXECVE: {
            char filename[256]{};
            if (!copy_path(arg1, filename, sizeof(filename))) return -ERR_EFAULT;
            kernel::kprintf("[+] POSIX sys_execve('%s') invoked.\n", filename);
            return -38; // ELF replacement remains intentionally unavailable.
        }
        case SYS_MMAP:
            return process::Manager::mmap(static_cast<uintptr_t>(arg1), static_cast<size_t>(arg2),
                                          static_cast<uint32_t>(arg3), static_cast<uint32_t>(arg4),
                                          static_cast<int32_t>(arg5), arg6);
        case SYS_MUNMAP:
            return process::Manager::munmap(static_cast<uintptr_t>(arg1), static_cast<size_t>(arg2));
        case SYS_BRK:
            return process::Manager::brk(static_cast<uintptr_t>(arg1));
        case SYS_WAIT4:
            return process::Manager::wait4(static_cast<process::pid_t>(arg1),
                                           reinterpret_cast<int32_t*>(arg3));
        case SYS_GETUID: return security::Manager::current().uid;
        case SYS_GETEUID: return security::Manager::current().euid;
        case SYS_GETGID: return security::Manager::current().gid;
        case SYS_GETEGID: return security::Manager::current().egid;
        case SYS_SETUID: return security::Manager::setuid(static_cast<security::uid_t>(arg1)) ? -1 : 0;
        case SYS_SETGID: return security::Manager::setgid(static_cast<security::gid_t>(arg1)) ? -1 : 0;
        case SYS_SETRESUID: return security::Manager::setresuid(
            static_cast<security::uid_t>(arg1), static_cast<security::uid_t>(arg2),
            static_cast<security::uid_t>(arg3)) ? -1 : 0;
        case SYS_SETRESGID: return security::Manager::setresgid(
            static_cast<security::gid_t>(arg1), static_cast<security::gid_t>(arg2),
            static_cast<security::gid_t>(arg3)) ? -1 : 0;
        case SYS_GETRESUID: {
            auto* real = reinterpret_cast<security::uid_t*>(arg1);
            auto* effective = reinterpret_cast<security::uid_t*>(arg2);
            auto* saved = reinterpret_cast<security::uid_t*>(arg3);
            if (!real || !effective || !saved || !valid_user_range(arg1, sizeof(*real)) ||
                !valid_user_range(arg2, sizeof(*effective)) || !valid_user_range(arg3, sizeof(*saved))) return -ERR_EFAULT;
            const auto& c = security::Manager::current(); *real = c.uid; *effective = c.euid; *saved = c.suid;
            return 0;
        }
        case SYS_GETRESGID: {
            auto* real = reinterpret_cast<security::gid_t*>(arg1);
            auto* effective = reinterpret_cast<security::gid_t*>(arg2);
            auto* saved = reinterpret_cast<security::gid_t*>(arg3);
            if (!real || !effective || !saved || !valid_user_range(arg1, sizeof(*real)) ||
                !valid_user_range(arg2, sizeof(*effective)) || !valid_user_range(arg3, sizeof(*saved))) return -ERR_EFAULT;
            const auto& c = security::Manager::current(); *real = c.gid; *effective = c.egid; *saved = c.sgid;
            return 0;
        }
        case SYS_SETGROUPS: return security::Manager::setgroups(
            reinterpret_cast<const security::gid_t*>(arg2), static_cast<uint32_t>(arg1)) ? -1 : 0;
        case SYS_GETGROUPS: {
            const uint32_t count = security::Manager::getgroups(
                reinterpret_cast<security::gid_t*>(arg2), static_cast<uint32_t>(arg1));
            return count == static_cast<uint32_t>(-1) ? -1 : static_cast<int64_t>(count);
        }
        case SYS_UMASK: return security::Manager::set_umask(static_cast<uint32_t>(arg1));
        case SYS_CHMOD:
        case SYS_FCHMODAT: {
            const char* path = reinterpret_cast<const char*>(sys_num == SYS_FCHMODAT ? arg2 : arg1);
            const uint32_t mode = static_cast<uint32_t>(sys_num == SYS_FCHMODAT ? arg3 : arg2);
            return vfs::VirtualFilesystem::chmod(path, mode);
        }
        case SYS_CHOWN:
        case SYS_FCHOWNAT: {
            const char* path = reinterpret_cast<const char*>(sys_num == SYS_FCHOWNAT ? arg2 : arg1);
            const security::uid_t uid = static_cast<security::uid_t>(sys_num == SYS_FCHOWNAT ? arg3 : arg2);
            const security::gid_t gid = static_cast<security::gid_t>(sys_num == SYS_FCHOWNAT ? arg4 : arg3);
            return vfs::VirtualFilesystem::chown(path, uid, gid);
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

extern "C" int64_t sys_call6(uint64_t sys_num, uint64_t arg1, uint64_t arg2,
                              uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    return syscall::SyscallDispatcher::dispatch6(sys_num, arg1, arg2, arg3, arg4, arg5, arg6);
}
