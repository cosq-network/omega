#ifndef OMEGA_KERNEL_PROCESS_HPP
#define OMEGA_KERNEL_PROCESS_HPP

#include "std/cstdint.hpp"
#include "kernel/security.hpp"
#include "kernel/vfs.hpp"
#include "kernel/vmm.hpp"

namespace process {

using pid_t = int32_t;

// Max open file descriptors per process (musl requires >= 256).
static constexpr uint32_t FD_TABLE_SIZE = 256;

struct Mapping {
    uintptr_t address;
    size_t length;
    bool cow;
};

// Per-process open file description: the VFS node plus the current file
// position and the open(2) flags used to create it.
struct FdEntry {
    vfs::VfsNode* node;
    uint64_t offset;
    uint32_t flags;
};

struct Process {
    pid_t pid;
    memory::AddressSpace address_space;
    uintptr_t next_mmap;
    uintptr_t program_break;
    bool alive;
    bool exited;
    int32_t exit_status;
    Process* parent;
    Process* children[8];
    uint32_t child_count;
    security::Credentials credentials;
    FdEntry fd_table[FD_TABLE_SIZE];
    Mapping mappings[32];
    uint32_t mapping_count;
    uintptr_t user_entry;
    uintptr_t user_stack;
    uintptr_t tls_base; // Per-process TLS base (0 if unused)
    char cwd[256];
};

class Manager {
public:
    static void init();
    static Process* current();
    static Process* create();
    static int64_t mmap(uintptr_t address, size_t length, uint32_t prot,
                        uint32_t flags, int32_t fd, uint64_t offset);
    static int64_t munmap(uintptr_t address, size_t length);
    static int64_t brk(uintptr_t address);
    static int64_t fork();
    static int64_t exit(int32_t status);
    static int64_t wait4(pid_t pid, int32_t* status);
    static void release_mappings(Process* process);
    static bool handle_cow_fault(uintptr_t address);
    static int64_t self_test();
    static bool activate(Process* process);
    static uintptr_t tls_base();
};

} // namespace process

#endif // OMEGA_KERNEL_PROCESS_HPP
