#include "kernel/syscall.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/vfs.hpp"
#include "kernel/kprint.hpp"
#include "kernel/process.hpp"
#include "kernel/security.hpp"
#include "kernel/memory.hpp"
#include "kernel/input.hpp"
#include "kernel/initrd.hpp"
#include "kernel/elf_loader.hpp"
#include "kernel/userland.hpp"
#include "kernel/heap.hpp"

namespace syscall {

namespace {
constexpr int64_t ERR_EBADF = 9;
constexpr int64_t ERR_EFAULT = 14;
constexpr int64_t ERR_EINVAL = 22;
constexpr int64_t ERR_ENOENT = 2;
constexpr int64_t ERR_ENOSYS = 38;
constexpr int64_t ERR_EMFILE = 24;
constexpr int64_t ERR_ENOMEM = 12;
constexpr int64_t ERR_ERANGE = 34;
constexpr uintptr_t USER_LIMIT = 0x0000800000000000ull;

// Linux O_* flags (x86_64 and modern ABIs agree on these).
constexpr uint32_t O_RDONLY = 0;
constexpr uint32_t O_WRONLY = 1;
constexpr uint32_t O_RDWR = 2;
constexpr uint32_t O_CREAT = 0x40;
constexpr uint32_t O_TRUNC = 0x200;
constexpr uint32_t O_APPEND = 0x400;
constexpr uint32_t O_ACCMODE = 3;

// fcntl commands.
constexpr uint32_t F_DUPFD = 0;
constexpr uint32_t F_GETFD = 1;
constexpr uint32_t F_SETFD = 2;
constexpr uint32_t F_GETFL = 3;
constexpr uint32_t F_SETFL = 4;

static process::FdEntry* fd_table() {
    process::Process* current = process::Manager::current();
    return current != nullptr ? current->fd_table : nullptr;
}

static bool valid_fd(int fd) {
    return fd >= 0 && fd < static_cast<int>(process::FD_TABLE_SIZE);
}

static bool same_text(const char* left, const char* right) {
    size_t i = 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == '\0' && right[i] == '\0';
}

static process::FdEntry* fd_node_entry(int fd) {
    process::FdEntry* table = fd_table();
    if (!table || !valid_fd(fd) || !table[fd].node) return nullptr;
    return &table[fd];
}

static bool valid_user_range(uintptr_t address, size_t length);

static int64_t write_fd(int fd, uintptr_t buffer_address, size_t count) {
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(buffer_address);
    if (!valid_user_range(buffer_address, count)) return -ERR_EFAULT;
    if (fd == 1 || fd == 2) {
        for (size_t i = 0; i < count; ++i) kernel::kputc(static_cast<char>(buffer[i]));
        return static_cast<int64_t>(count);
    }
    process::FdEntry* entry = fd_node_entry(fd);
    if (!entry) return -ERR_EBADF;
    const int written = vfs::VirtualFilesystem::write(entry->node, entry->offset, count, buffer);
    if (written < 0) return written;
    if (!(entry->flags & O_APPEND)) entry->offset += static_cast<uint64_t>(written);
    return written;
}

static int alloc_fd(vfs::VfsNode* node, uint32_t flags) {
    process::FdEntry* table = fd_table();
    if (!table) return -ERR_EMFILE;
    for (int i = 3; i < static_cast<int>(process::FD_TABLE_SIZE); ++i) {
        if (!table[i].node) {
            table[i].node = node;
            table[i].offset = 0;
            table[i].flags = flags;
            return i;
        }
    }
    return -ERR_EMFILE;
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

static bool resolve_path(const char* raw, char* output, size_t capacity) {
    if (!raw || !raw[0] || !output || capacity < 2) return false;
    process::Process* process = process::Manager::current();
    if (!process) return false;
    const bool absolute = raw[0] == '/';
    size_t length = 0;
    if (absolute) output[length++] = '/';
    else {
        while (process->cwd[length] && length + 1 < capacity) {
            output[length] = process->cwd[length];
            ++length;
        }
        if (length == 0 || output[length - 1] != '/') output[length++] = '/';
    }
    output[length] = '\0';
    size_t cursor = absolute ? 1 : 0;
    while (raw[cursor]) {
        while (raw[cursor] == '/') ++cursor;
        if (!raw[cursor]) break;
        char component[64]{};
        size_t component_length = 0;
        while (raw[cursor] && raw[cursor] != '/') {
            if (component_length + 1 >= sizeof(component)) return false;
            component[component_length++] = raw[cursor++];
        }
        component[component_length] = '\0';
        if (same_text(component, ".")) continue;
        if (same_text(component, "..")) {
            while (length > 1 && output[length - 1] == '/') --length;
            while (length > 1 && output[length - 1] != '/') --length;
            output[length] = '\0';
            continue;
        }
        if (length > 1 && output[length - 1] != '/') output[length++] = '/';
        if (length + component_length + 1 >= capacity) return false;
        for (size_t i = 0; i < component_length; ++i) output[length++] = component[i];
        output[length] = '\0';
    }
    if (length > 1 && output[length - 1] == '/') output[--length] = '\0';
    if (length == 0) { output[0] = '/'; output[1] = '\0'; }
    return true;
}

static bool copy_user_vector(uintptr_t user_vector, char* storage, size_t storage_size,
                             const char* output[], int& count) {
    count = 0;
    if (user_vector == 0) {
        output[0] = nullptr;
        return true;
    }
    size_t used = 0;
    while (count < 64) {
        const uintptr_t slot = user_vector + static_cast<uintptr_t>(count) * sizeof(uintptr_t);
        if (slot < user_vector || !valid_user_range(slot, sizeof(uintptr_t))) return false;
        const uintptr_t user_string = *reinterpret_cast<const uintptr_t*>(slot);
        if (user_string == 0) {
            output[count] = nullptr;
            return true;
        }
        if (used >= storage_size) return false;
        output[count] = storage + used;
        const size_t remaining = storage_size - used;
        if (!copy_path(user_string, storage + used, remaining)) return false;
        size_t length = 0;
        while (storage[used + length] != '\0') ++length;
        used += length + 1;
        ++count;
    }
    return false;
}
}

void SyscallDispatcher::init() {
    process::FdEntry* table = fd_table();
    if (table != nullptr) {
        for (uint32_t i = 0; i < process::FD_TABLE_SIZE; ++i) { table[i].node = nullptr; table[i].offset = 0; table[i].flags = 0; }
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
            size_t count = static_cast<size_t>(arg3);
            return write_fd(fd, arg2, count);
        }
        case SYS_WRITEV: {
            struct Iovec { uintptr_t base; size_t length; };
            const int fd = static_cast<int>(arg1);
            const size_t count = static_cast<size_t>(arg3);
            if (count == 0 || count > 1024 || !valid_user_range(arg2, count * sizeof(Iovec))) return -ERR_EFAULT;
            int64_t total = 0;
            const auto* vectors = reinterpret_cast<const Iovec*>(arg2);
            for (size_t i = 0; i < count; ++i) {
                const int64_t written = write_fd(fd, vectors[i].base, vectors[i].length);
                if (written < 0) return total != 0 ? total : written;
                total += written;
            }
            return total;
        }
        case SYS_EXIT:
        case SYS_EXIT_GROUP:
            kernel::kprintf("[!] Syscall Exit Called with status: %d\n", arg1);
            return process::Manager::exit(static_cast<int32_t>(arg1));
        case SYS_OPEN:
        case SYS_OPENAT: {
            const uintptr_t user_path = sys_num == SYS_OPENAT ? arg2 : arg1;
            char path[256]{};
            if (!copy_path(user_path, path, sizeof(path))) return -ERR_EFAULT;
            char resolved_path[256]{};
            if (!resolve_path(path, resolved_path, sizeof(resolved_path))) return -ERR_EINVAL;
            const uint64_t open_flags = sys_num == SYS_OPENAT ? arg3 : arg2;
            uint32_t access = security::MAY_READ;
            const uint32_t accmode = static_cast<uint32_t>(open_flags) & O_ACCMODE;
            if (accmode == O_WRONLY) access = security::MAY_WRITE;
            else if (accmode == O_RDWR) access = security::MAY_READ | security::MAY_WRITE;
            vfs::VfsNode* node = vfs::VirtualFilesystem::open(resolved_path, access);
            if (!node && (open_flags & O_CREAT)) {
                node = vfs::VirtualFilesystem::create(resolved_path, 0644);
            }
            if (!node) return -ERR_ENOENT;
            if (open_flags & O_TRUNC) vfs::VirtualFilesystem::truncate(node, 0);
            const int fd = alloc_fd(node, static_cast<uint32_t>(open_flags));
            if (fd < 0) return fd;
            kernel::kprintf("[+] POSIX sys_open('%s') assigned FD: %d\n", resolved_path, fd);
            return fd;
        }
        case SYS_GETCWD: {
            process::Process* process = process::Manager::current();
            const size_t capacity = static_cast<size_t>(arg2);
            if (!process || capacity == 0 || !valid_user_range(arg1, capacity)) return -ERR_EFAULT;
            size_t length = 0;
            while (process->cwd[length] && length + 1 < capacity) {
                reinterpret_cast<char*>(arg1)[length] = process->cwd[length];
                ++length;
            }
            if (process->cwd[length] != '\0') return -ERR_ERANGE;
            reinterpret_cast<char*>(arg1)[length] = '\0';
            return static_cast<int64_t>(arg1);
        }
        case SYS_CHDIR: {
            char raw[256]{}, resolved[256]{};
            if (!copy_path(arg1, raw, sizeof(raw)) || !resolve_path(raw, resolved, sizeof(resolved))) return -ERR_EFAULT;
            vfs::VfsNode* node = vfs::VirtualFilesystem::open(resolved, security::MAY_EXEC);
            if (!node || node->type != vfs::DIRECTORY_TYPE) return -ERR_ENOENT;
            process::Process* process = process::Manager::current();
            if (!process) return -ERR_EINVAL;
            for (size_t i = 0; i < sizeof(process->cwd); ++i) process->cwd[i] = resolved[i];
            return 0;
        }
        case SYS_READ: {
            int fd = static_cast<int>(arg1);
            process::FdEntry* entry = fd_node_entry(fd);
            if (!entry) return -ERR_EBADF;
            uint8_t* buf = reinterpret_cast<uint8_t*>(arg2);
            size_t count = static_cast<size_t>(arg3);
            if (!valid_user_range(arg2, count)) return -ERR_EFAULT;
            const int nread = vfs::VirtualFilesystem::read(entry->node, entry->offset, count, buf);
            if (nread < 0) return nread;
            entry->offset += static_cast<uint64_t>(nread);
            return nread;
        }
        case SYS_CLOSE: {
            int fd = static_cast<int>(arg1);
            process::FdEntry* entry = fd_node_entry(fd);
            if (!entry) return -ERR_EBADF;
            entry->node = nullptr;
            kernel::kprintf("[+] POSIX sys_close FD: %d\n", fd);
            return 0;
        }
        case SYS_FORK:
            return process::Manager::fork();
        case SYS_EXECVE: {
            char filename[256]{};
            if (!copy_path(arg1, filename, sizeof(filename))) return -ERR_EFAULT;
            char resolved_filename[256]{};
            if (!resolve_path(filename, resolved_filename, sizeof(resolved_filename))) return -ERR_EINVAL;
            kernel::kprintf("[+] POSIX sys_execve('%s') invoked.\n", filename);
            auto* argv_storage = reinterpret_cast<char*>(kmalloc(64 * 256));
            auto* envp_storage = reinterpret_cast<char*>(kmalloc(64 * 256));
            if (!argv_storage || !envp_storage) {
                if (argv_storage) kfree(argv_storage);
                if (envp_storage) kfree(envp_storage);
                return -ERR_ENOMEM;
            }
            const char* argv_copy[65]{};
            const char* envp_copy[65]{};
            int argc = 0, envc = 0;
            if (!copy_user_vector(arg2, argv_storage, 64 * 256, argv_copy, argc) ||
                !copy_user_vector(arg3, envp_storage, 64 * 256, envp_copy, envc)) {
                kfree(argv_storage); kfree(envp_storage);
                return -ERR_EFAULT;
            }
            if (argc == 0) {
                argv_copy[0] = filename;
                argv_copy[1] = nullptr;
                argc = 1;
            }
            // The VFS path walk handles nested initrd paths such as /bin/echo.
            vfs::VfsNode* exec_node = vfs::VirtualFilesystem::open(resolved_filename, security::MAY_EXEC);
            if (!exec_node || exec_node->type != vfs::FILE_TYPE) {
                kfree(argv_storage); kfree(envp_storage);
                return -ERR_ENOENT;
            }
            const uint8_t* data = reinterpret_cast<const uint8_t*>(exec_node->fs_data);
            if (!elf::ElfLoader::validate(data, exec_node->size)) {
                kfree(argv_storage); kfree(envp_storage);
                return -ERR_EINVAL;
            }
            process::Process* proc = process::Manager::current();
            if (!proc) { kfree(argv_storage); kfree(envp_storage); return -ERR_EINVAL; }
            // Tear down the old image (all current user mappings).
            process::Manager::release_mappings(proc);
            proc->tls_base = 0;
            uintptr_t entry = 0, stack = 0;
            if (!elf::ElfLoader::load_into(proc, data, exec_node->size, &entry, &stack,
                                           argc, argv_copy, envp_copy)) {
                kfree(argv_storage); kfree(envp_storage);
                return -ERR_EINVAL;
            }
            kfree(argv_storage); kfree(envp_storage);
            proc->user_entry = entry;
            proc->user_stack = stack;
            // Jump to the new image via the architecture's userland entry.
            userland::UserlandManager::enter_userland_from_syscall(entry, stack, proc->tls_base);
            return 0;
        }
        case SYS_MMAP:
            return process::Manager::mmap(static_cast<uintptr_t>(arg1), static_cast<size_t>(arg2),
                                          static_cast<uint32_t>(arg3), static_cast<uint32_t>(arg4),
                                          static_cast<int32_t>(arg5), arg6);
        case SYS_MUNMAP:
            return process::Manager::munmap(static_cast<uintptr_t>(arg1), static_cast<size_t>(arg2));
        case SYS_BRK:
            return process::Manager::brk(static_cast<uintptr_t>(arg1));
        case SYS_GETPID:
            return process::Manager::current() != nullptr ? process::Manager::current()->pid : -1;
        case SYS_FSTAT: {
            const int fd = static_cast<int>(arg1);
            process::FdEntry* entry = fd_node_entry(fd);
            if (!entry) return -ERR_EBADF;
            // Linux x86_64 struct stat is 144 bytes. We fill the standard
            // leading fields musl's fstat wrapper reads (st_dev, st_ino,
            // st_nlink, st_mode, st_uid, st_gid, st_rdev, st_size, ...).
            auto* st = reinterpret_cast<uint8_t*>(arg2);
            if (!valid_user_range(arg2, 144)) return -ERR_EFAULT;
            for (size_t i = 0; i < 144; ++i) st[i] = 0;
            auto put64 = [&](size_t off, uint64_t v) {
                for (size_t i = 0; i < 8; ++i) st[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
            };
            auto put32 = [&](size_t off, uint32_t v) {
                for (size_t i = 0; i < 4; ++i) st[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
            };
            put64(0, 0);           // st_dev
            put64(8, 0);           // st_ino
            put64(16, entry->node->size); // st_size
            put64(24, 4096);       // st_blksize
            put64(32, 0);          // st_blocks
            put32(40, 0);          // st_atime
            put32(44, 0);          // st_mtime
            put32(48, 0);          // st_ctime
            put32(56, static_cast<uint32_t>(entry->node->mode));
            put32(60, static_cast<uint32_t>(entry->node->uid));
            put32(64, static_cast<uint32_t>(entry->node->gid));
            return 0;
        }
        case SYS_LSEEK: {
            const int fd = static_cast<int>(arg1);
            process::FdEntry* entry = fd_node_entry(fd);
            if (!entry) return -ERR_EBADF;
            const int64_t offset = static_cast<int64_t>(arg2);
            const int whence = static_cast<int>(arg3);
            int64_t base = 0;
            if (whence == 0) base = 0;            // SEEK_SET
            else if (whence == 1) base = static_cast<int64_t>(entry->offset); // SEEK_CUR
            else if (whence == 2) base = static_cast<int64_t>(entry->node->size); // SEEK_END
            else return -ERR_EINVAL;
            if (offset < 0 && base + offset < 0) return -ERR_EINVAL;
            const int64_t result = base + offset;
            if (result < 0) return -ERR_EINVAL;
            entry->offset = static_cast<uint64_t>(result);
            return result;
        }
        case SYS_DUP2: {
            const int oldfd = static_cast<int>(arg1);
            const int newfd = static_cast<int>(arg2);
            process::FdEntry* table = fd_table();
            if (!table || !valid_fd(oldfd) || !valid_fd(newfd)) return -ERR_EBADF;
            if (!table[oldfd].node) return -ERR_EBADF;
            if (oldfd == newfd) return newfd;
            if (table[newfd].node) table[newfd].node = nullptr;
            table[newfd] = table[oldfd];
            return newfd;
        }
        case SYS_PIPE2: {
            // Minimal anonymous pipe: two FDs backed by an in-memory ring
            // buffer node. (Only the fd plumbing is needed for basic tests;
            // a full pipe buffer implementation is a follow-up.)
            auto* pipe_node = reinterpret_cast<vfs::VfsNode*>(kmalloc(sizeof(vfs::VfsNode)));
            if (!pipe_node) return -ERR_ENOMEM;
            // Simple single-page in-memory pipe: read end, write end.
            uint8_t* buffer = reinterpret_cast<uint8_t*>(kmalloc(4096));
            if (!buffer) { kfree(pipe_node); return -ERR_ENOMEM; }
            pipe_node->name[0] = 'p'; pipe_node->name[1] = '\0';
            pipe_node->type = vfs::FILE_TYPE;
            pipe_node->size = 0;
            pipe_node->uid = security::Manager::current().uid;
            pipe_node->gid = security::Manager::current().gid;
            pipe_node->mode = 0600;
            pipe_node->read = nullptr;
            pipe_node->write = nullptr;
            pipe_node->finddir = nullptr;
            pipe_node->fs_data = buffer;
            const int rd = alloc_fd(pipe_node, O_RDONLY);
            const int wr = alloc_fd(pipe_node, O_WRONLY);
            if (rd < 0 || wr < 0) { kfree(buffer); kfree(pipe_node); return -ERR_EMFILE; }
            auto* out = reinterpret_cast<int32_t*>(arg1);
            if (!valid_user_range(arg1, 8)) return -ERR_EFAULT;
            out[0] = rd; out[1] = wr;
            return 0;
        }
        case SYS_GETDENTS64: {
            const int fd = static_cast<int>(arg1);
            process::FdEntry* entry = fd_node_entry(fd);
            if (!entry) return -ERR_EBADF;
            // Defer to a VFS-level directory enumeration; returns 0 entries
            // for non-directories. A full getdents64 layout is a follow-up.
            auto* buf = reinterpret_cast<uint8_t*>(arg2);
            const size_t len = static_cast<size_t>(arg3);
            if (!valid_user_range(arg2, len)) return -ERR_EFAULT;
            (void)buf; (void)len;
            return vfs::VirtualFilesystem::readdir(entry->node, 0, buf, len);
        }
        case SYS_FCNTL: {
            const int fd = static_cast<int>(arg1);
            const int cmd = static_cast<int>(arg2);
            process::FdEntry* entry = fd_node_entry(fd);
            if (!entry) return -ERR_EBADF;
            switch (cmd) {
                case F_GETFD: return 0;
                case F_SETFD: return 0;
                case F_GETFL: return static_cast<int64_t>(entry->flags);
                case F_SETFL: entry->flags = static_cast<uint32_t>(arg3); return 0;
                case F_DUPFD: {
                    const int minfd = static_cast<int>(arg3);
                    process::FdEntry* table = fd_table();
                    if (!table) return -ERR_EBADF;
                    for (int i = minfd; i < static_cast<int>(process::FD_TABLE_SIZE); ++i) {
                        if (!table[i].node) { table[i] = *entry; return i; }
                    }
                    return -ERR_EMFILE;
                }
                default: return -ERR_EINVAL;
            }
        }
        case SYS_UNAME: {
            auto* buf = reinterpret_cast<uint8_t*>(arg1);
            if (!valid_user_range(arg1, 390)) return -ERR_EFAULT;
            for (size_t i = 0; i < 390; ++i) buf[i] = 0;
            auto put = [&](size_t off, const char* s) {
                size_t i = 0;
                while (s[i] && off + i < 390) { buf[off + i] = static_cast<uint8_t>(s[i]); ++i; }
            };
            put(0, "Omega");        // sysname
            put(65, "omega");       // nodename
            put(130, "0.0.14");     // release
            put(195, "omega-0.0.14"); // version
            put(260, "x86_64");     // machine (filled per-ISA)
            return 0;
        }
        case SYS_CLOCK_GETTIME: {
            // Minimal: return CLOCK_MONOTONIC-ish epoch 0. A real time base
            // is a follow-up; musl tolerates a zero timespec.
            auto* ts = reinterpret_cast<uint8_t*>(arg2);
            if (!valid_user_range(arg2, 16)) return -ERR_EFAULT;
            for (size_t i = 0; i < 16; ++i) ts[i] = 0;
            return 0;
        }
        case SYS_SET_TID_ADDRESS:
            // Single-threaded: musl registers its TID pointer for thread
            // cleanup; nothing to do until clone() exists. Validate the
            // pointer is user-accessible and return the current pid.
            if (arg1 != 0 && !valid_user_range(arg1, sizeof(int32_t))) return -ERR_EFAULT;
            return process::Manager::current() != nullptr ? process::Manager::current()->pid : 0;
        case SYS_IOCTL:
            return -ERR_ENOSYS;
#if defined(__x86_64__)
        case SYS_ARCH_PRCTL: {
            // musl's __set_thread_area uses arch_prctl(ARCH_SET_FS, addr).
            constexpr uint32_t ARCH_SET_FS = 0x1002;
            constexpr uint32_t ARCH_GET_FS = 0x1003;
            if (arg1 == ARCH_SET_FS) {
                userland::UserlandManager::set_fs_base(arg2);
                return 0;
            }
            if (arg1 == ARCH_GET_FS) {
                uintptr_t* out = reinterpret_cast<uintptr_t*>(arg2);
                if (!valid_user_range(arg2, sizeof(uintptr_t))) return -ERR_EFAULT;
                *out = process::Manager::tls_base();
                return 0;
            }
            return -ERR_EINVAL;
        }
#endif
        case SYS_RT_SIGACTION:
            return -ERR_ENOSYS;
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
