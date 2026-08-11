#ifndef OMEGA_KERNEL_INITRD_HPP
#define OMEGA_KERNEL_INITRD_HPP

#include "kernel/vfs.hpp"

namespace initrd {

struct InitrdHeader {
    uint32_t nfiles;
};

struct InitrdFileHeader {
    uint32_t magic; // 0xBF
    char name[64];
    uint32_t offset;
    uint32_t length;
};

class Initrd {
public:
    static vfs::VfsNode* init(uintptr_t location);
    static vfs::VfsNode* find(const char* path);
};

} // namespace initrd

#endif // OMEGA_KERNEL_INITRD_HPP
