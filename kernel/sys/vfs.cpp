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
    if (node && node->read) {
        return node->read(node, offset, size, buffer);
    }
    return -1;
}

int VirtualFilesystem::write(VfsNode* node, size_t offset, size_t size, const uint8_t* buffer) {
    if (node && node->write) {
        return node->write(node, offset, size, buffer);
    }
    return -1;
}

VfsNode* VirtualFilesystem::open(const char* path) {
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
        current = current->finddir(current, component);
        if (!current) return nullptr;
    }
    return current;
}

} // namespace vfs
