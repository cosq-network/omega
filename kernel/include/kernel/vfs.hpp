#ifndef OMEGA_KERNEL_VFS_HPP
#define OMEGA_KERNEL_VFS_HPP

#include "std/cstdint.hpp"
#include "kernel/security.hpp"

namespace vfs {

enum NodeType {
    FILE_TYPE = 1,
    DIRECTORY_TYPE = 2,
};

// Linux getdents64 struct linux_dirent64 header layout (offsets).
static constexpr size_t DIRENT64_RECLEN_OFF = 16;
static constexpr size_t DIRENT64_NAME_OFF = 19;

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
    // Returns the number of bytes written to buf, or -1 on error. Each
    // entry is a linux_dirent64: ino(8) off(8) reclen(2) type(1) name\0.
    int (*readdir)(VfsNode* node, size_t offset, uint8_t* buf, size_t len);
    // Create a child node (filesystem backends that support it).
    VfsNode* (*create)(VfsNode* node, const char* name, uint32_t mode);
    // Truncate/resize a node.
    int (*truncate)(VfsNode* node, size_t size);
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
    static VfsNode* create(const char* path, uint32_t mode);
    static int truncate(VfsNode* node, size_t size);
    static int readdir(VfsNode* node, size_t offset, uint8_t* buf, size_t len);
    static int chmod(const char* path, uint32_t mode);
    static int chown(const char* path, security::uid_t uid, security::gid_t gid);
    // Mount an in-memory tmpfs at the given absolute path (e.g. "/tmp").
    static bool mount_tmpfs(const char* path);

private:
    static VfsNode* root_node;
};

} // namespace vfs

#endif // OMEGA_KERNEL_VFS_HPP
