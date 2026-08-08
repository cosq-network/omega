#ifndef OMEGA_KERNEL_PROCESS_HPP
#define OMEGA_KERNEL_PROCESS_HPP

#include "std/cstdint.hpp"
#include "kernel/security.hpp"
#include "kernel/vfs.hpp"
#include "kernel/vmm.hpp"

namespace process {

using pid_t = int32_t;

struct Mapping {
    uintptr_t address;
    size_t length;
};

struct Process {
    pid_t pid;
    memory::AddressSpace address_space;
    uintptr_t next_mmap;
    uintptr_t program_break;
    bool alive;
    security::Credentials credentials;
    vfs::VfsNode* fd_table[16];
    Mapping mappings[32];
    uint32_t mapping_count;
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
    static int64_t self_test();
};

} // namespace process

#endif // OMEGA_KERNEL_PROCESS_HPP
