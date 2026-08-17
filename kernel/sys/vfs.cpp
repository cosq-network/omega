#include "kernel/vfs.hpp"
#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"

namespace vfs {

VfsNode* VirtualFilesystem::root_node = nullptr;

void VirtualFilesystem::init() {
    root_node = reinterpret_cast<VfsNode*>(kmalloc(sizeof(VfsNode)));
    root_node->name[0] = '/';
    root_node->name[1] = '\0';
    root_node->type = DIRECTORY_TYPE;
    root_node->size = 0;
    root_node->flags = 0;
    root_node->uid = security::ROOT_UID;
    root_node->gid = security::ROOT_GID;
    root_node->mode = 0755;
    root_node->read = nullptr;
    root_node->write = nullptr;
    root_node->finddir = nullptr;
    root_node->fs_data = nullptr;

    kernel::kprintf("[+] Virtual Filesystem (VFS) Initialized.\n");
    kernel::kprintf("    Root Node '/' Mounted.\n");
}

VfsNode* VirtualFilesystem::get_root() {
    return root_node;
}

void VirtualFilesystem::mount_root(VfsNode* node) {
    if (node) root_node = node;
}

int VirtualFilesystem::read(VfsNode* node, size_t offset, size_t size, uint8_t* buffer) {
    if (node && node->read && security::Manager::check(node->mode, node->uid, node->gid,
                                                        security::Manager::current(), security::MAY_READ)) {
        return node->read(node, offset, size, buffer);
    }
    return -1;
}

int VirtualFilesystem::write(VfsNode* node, size_t offset, size_t size, const uint8_t* buffer) {
    if (node && node->write && security::Manager::check(node->mode, node->uid, node->gid,
                                                        security::Manager::current(), security::MAY_WRITE)) {
        return node->write(node, offset, size, buffer);
    }
    return -1;
}

VfsNode* VirtualFilesystem::open(const char* path) {
    return open(path, 0);
}

VfsNode* VirtualFilesystem::open(const char* path, uint32_t access) {
    if (!path || path[0] != '/' || !root_node) return nullptr;
    VfsNode* current = root_node;
    size_t pos = 1;
    while (path[pos]) {
        while (path[pos] == '/') ++pos;
        if (!path[pos]) break;
        char component[64]{};
        size_t length = 0;
        while (path[pos] && path[pos] != '/') {
            if (length + 1 >= sizeof(component)) return nullptr;
            component[length++] = path[pos++];
        }
        component[length] = '\0';
        if (!current->finddir) return nullptr;
        if (!security::Manager::check(current->mode, current->uid, current->gid,
                                      security::Manager::current(), security::MAY_EXEC)) return nullptr;
        current = current->finddir(current, component);
        if (!current) return nullptr;
    }
    if (access && !security::Manager::check(current->mode, current->uid, current->gid,
                                            security::Manager::current(), access)) return nullptr;
    return current;
}

int VirtualFilesystem::chmod(const char* path, uint32_t mode) {
    VfsNode* node = open(path);
    if (!node) return -2; // ENOENT
    const auto& credentials = security::Manager::current();
    if (credentials.euid != security::ROOT_UID && credentials.euid != node->uid) return -1; // EPERM
    node->mode = (node->mode & ~07777u) | (mode & 07777u);
    return 0;
}

int VirtualFilesystem::chown(const char* path, security::uid_t uid, security::gid_t gid) {
    VfsNode* node = open(path);
    if (!node) return -2;
    if (security::Manager::current().euid != security::ROOT_UID) return -1;
    node->uid = uid;
    node->gid = gid;
    return 0;
}

VfsNode* VirtualFilesystem::create(const char* path, uint32_t mode) {
    if (!path || path[0] != '/' || !root_node) return nullptr;
    // Split into parent path + final component.
    size_t slash = 0;
    for (size_t i = 0; path[i]; ++i) {
        if (path[i] == '/') slash = i;
    }
    if (slash == 0) return nullptr; // cannot create at root
    char parent_path[256]{};
    for (size_t i = 0; i < slash && i + 1 < sizeof(parent_path); ++i) parent_path[i] = path[i];
    const char* name = path + slash + 1;
    if (!name[0]) return nullptr;
    VfsNode* parent = open(parent_path);
    if (!parent || parent->type != DIRECTORY_TYPE || !parent->create) return nullptr;
    VfsNode* node = parent->create(parent, name, mode);
    if (node) kernel::kprintf("[+] VFS created '%s' (mode %o)\n", path, mode);
    return node;
}

int VirtualFilesystem::truncate(VfsNode* node, size_t size) {
    if (!node) return -1;
    if (node->truncate) return node->truncate(node, size);
    return -1;
}

int VirtualFilesystem::readdir(VfsNode* node, size_t offset, uint8_t* buf, size_t len) {
    if (!node || node->type != DIRECTORY_TYPE || !node->readdir) return -1;
    return node->readdir(node, offset, buf, len);
}

} // namespace vfs
