#ifndef OMEGA_KERNEL_VFS_HPP
#define OMEGA_KERNEL_VFS_HPP

#include "std/cstdint.hpp"
#include "kernel/security.hpp"

namespace vfs {

enum NodeType {
    FILE_TYPE = 1,
    DIRECTORY_TYPE = 2,
};

struct VfsNode {
    char name[64];
    NodeType type;
    size_t size;
    uint32_t flags;
    security::uid_t uid;
    security::gid_t gid;
    uint32_t mode;

    int (*read)(VfsNode* node, size_t offset, size_t size, uint8_t* buffer);
    int (*write)(VfsNode* node, size_t offset, size_t size, const uint8_t* buffer);
    VfsNode* (*finddir)(VfsNode* node, const char* name);
    void* fs_data;
};

class VirtualFilesystem {
public:
    static void init();
    static VfsNode* get_root();
    static void mount_root(VfsNode* node);
    static int read(VfsNode* node, size_t offset, size_t size, uint8_t* buffer);
    static int write(VfsNode* node, size_t offset, size_t size, const uint8_t* buffer);
    static VfsNode* open(const char* path);
    static VfsNode* open(const char* path, uint32_t access);
    static int chmod(const char* path, uint32_t mode);
    static int chown(const char* path, security::uid_t uid, security::gid_t gid);

private:
    static VfsNode* root_node;
};

} // namespace vfs

#endif // OMEGA_KERNEL_VFS_HPP
