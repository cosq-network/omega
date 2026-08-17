#include "kernel/tmpfs.hpp"
#include "kernel/vfs.hpp"
#include "kernel/heap.hpp"
#include "kernel/security.hpp"
#include "kernel/kprint.hpp"

namespace tmpfs {

namespace {

struct TmpfsFile {
    vfs::VfsNode node;
    uint8_t* data;      // heap buffer (may grow)
    size_t capacity;    // allocated size
    TmpfsFile* next;    // sibling list (dir entries)
};

struct TmpfsDir {
    TmpfsFile* children; // linked list of entries
};

static constexpr size_t INITIAL_CAPACITY = 4096;

static TmpfsFile* file_of(vfs::VfsNode* node) {
    return node ? reinterpret_cast<TmpfsFile*>(node->fs_data) : nullptr;
}

static int read_file(vfs::VfsNode* node, size_t offset, size_t size, uint8_t* buffer) {
    TmpfsFile* file = file_of(node);
    if (!file || offset > node->size) return -1;
    if (size > node->size - offset) size = node->size - offset;
    for (size_t i = 0; i < size; ++i) buffer[i] = file->data[offset + i];
    return static_cast<int>(size);
}

static int write_file(vfs::VfsNode* node, size_t offset, size_t size, const uint8_t* buffer) {
    TmpfsFile* file = file_of(node);
    if (!file) return -1;
    const size_t end = offset + size;
    if (end > file->capacity) {
        size_t new_capacity = file->capacity;
        while (new_capacity < end) new_capacity *= 2;
        uint8_t* grown = reinterpret_cast<uint8_t*>(kmalloc(new_capacity));
        if (!grown) return -1;
        for (size_t i = 0; i < file->capacity; ++i) grown[i] = file->data[i];
        kfree(file->data);
        file->data = grown;
        file->capacity = new_capacity;
    }
    for (size_t i = 0; i < size; ++i) file->data[offset + i] = buffer[i];
    if (end > node->size) node->size = end;
    return static_cast<int>(size);
}

static int truncate_file(vfs::VfsNode* node, size_t size) {
    TmpfsFile* file = file_of(node);
    if (!file) return -1;
    if (size > file->capacity) {
        size_t new_capacity = file->capacity;
        while (new_capacity < size) new_capacity *= 2;
        uint8_t* grown = reinterpret_cast<uint8_t*>(kmalloc(new_capacity));
        if (!grown) return -1;
        for (size_t i = 0; i < file->capacity; ++i) grown[i] = file->data[i];
        kfree(file->data);
        file->data = grown;
        file->capacity = new_capacity;
    }
    node->size = size;
    return 0;
}

static vfs::VfsNode* finddir(vfs::VfsNode* node, const char* name) {
    TmpfsDir* dir = node ? reinterpret_cast<TmpfsDir*>(node->fs_data) : nullptr;
    if (!dir || !name) return nullptr;
    for (TmpfsFile* entry = dir->children; entry; entry = entry->next) {
        size_t i = 0;
        while (name[i] && entry->node.name[i] && name[i] == entry->node.name[i]) ++i;
        if (!name[i] && !entry->node.name[i]) return &entry->node;
    }
    return nullptr;
}

static vfs::VfsNode* create_file(vfs::VfsNode* node, const char* name, uint32_t mode) {
    TmpfsDir* dir = node ? reinterpret_cast<TmpfsDir*>(node->fs_data) : nullptr;
    if (!dir || !name || finddir(node, name)) return nullptr;
    TmpfsFile* file = reinterpret_cast<TmpfsFile*>(kmalloc(sizeof(TmpfsFile)));
    if (!file) return nullptr;
    file->data = reinterpret_cast<uint8_t*>(kmalloc(INITIAL_CAPACITY));
    if (!file->data) { kfree(file); return nullptr; }
    file->capacity = INITIAL_CAPACITY;
    file->next = dir->children;
    for (size_t i = 0; name[i] && i + 1 < sizeof(file->node.name); ++i) file->node.name[i] = name[i];
    file->node.name[sizeof(file->node.name) - 1] = '\0';
    file->node.type = vfs::FILE_TYPE;
    file->node.size = 0;
    file->node.flags = 0;
    file->node.uid = security::Manager::current().uid;
    file->node.gid = security::Manager::current().gid;
    file->node.mode = mode;
    file->node.read = read_file;
    file->node.write = write_file;
    file->node.finddir = nullptr;
    file->node.readdir = nullptr;
    file->node.create = nullptr;
    file->node.truncate = truncate_file;
    file->node.fs_data = file;
    dir->children = file;
    return &file->node;
}

static int readdir(vfs::VfsNode* node, size_t offset, uint8_t* buf, size_t len) {
    TmpfsDir* dir = node ? reinterpret_cast<TmpfsDir*>(node->fs_data) : nullptr;
    if (!dir || !buf) return -1;
    // Iterate to the entry at `offset` (entry index).
    TmpfsFile* entry = dir->children;
    for (size_t i = 0; entry && i < offset; ++i) entry = entry->next;
    if (!entry) return 0;
    // linux_dirent64: ino(8) off(8) reclen(2) type(1) name\0 (padded to 8).
    size_t name_len = 0;
    while (entry->node.name[name_len]) ++name_len;
    size_t reclen = (19 + name_len + 1 + 7) & ~size_t(7);
    if (reclen > len) return -1;
    for (size_t i = 0; i < reclen; ++i) buf[i] = 0;
    auto put64 = [&](size_t off, uint64_t v) {
        for (size_t i = 0; i < 8; ++i) buf[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    };
    put64(0, 1); // d_ino
    put64(8, static_cast<uint64_t>(offset + 1)); // d_off
    buf[16] = static_cast<uint8_t>(reclen);      // d_reclen (low byte; < 256)
    buf[18] = static_cast<uint8_t>(entry->node.type == vfs::DIRECTORY_TYPE ? 4 : 8); // d_type
    for (size_t i = 0; i < name_len; ++i) buf[19 + i] = static_cast<uint8_t>(entry->node.name[i]);
    return static_cast<int>(reclen);
}

static vfs::VfsNode* make_dir_node(const char* name) {
    TmpfsDir* dir = reinterpret_cast<TmpfsDir*>(kmalloc(sizeof(TmpfsDir)));
    if (!dir) return nullptr;
    dir->children = nullptr;
    vfs::VfsNode* node = reinterpret_cast<vfs::VfsNode*>(kmalloc(sizeof(vfs::VfsNode)));
    if (!node) { kfree(dir); return nullptr; }
    for (size_t i = 0; name[i] && i + 1 < sizeof(node->name); ++i) node->name[i] = name[i];
    node->name[sizeof(node->name) - 1] = '\0';
    node->type = vfs::DIRECTORY_TYPE;
    node->size = 0;
    node->flags = 0;
    node->uid = security::ROOT_UID;
    node->gid = security::ROOT_GID;
    node->mode = 01777; // sticky, world-writable (like /tmp)
    node->read = nullptr;
    node->write = nullptr;
    node->finddir = finddir;
    node->readdir = readdir;
    node->create = create_file;
    node->truncate = nullptr;
    node->fs_data = dir;
    return node;
}

} // namespace

bool mount(const char* path) {
    if (!path || path[0] != '/') return false;
    vfs::VfsNode* root = vfs::VirtualFilesystem::get_root();
    if (!root) return false;
    // Walk to the parent of the mount point.
    if (path[1] == '\0') {
        // Mounting over root: attach children to the existing root by
        // giving it tmpfs dir semantics is not supported; refuse.
        return false;
    }
    vfs::VfsNode* parent = root;
    size_t pos = 1;
    const char* name = nullptr;
    while (path[pos]) {
        while (path[pos] == '/') ++pos;
        if (!path[pos]) break;
        const char* start = path + pos;
        while (path[pos] && path[pos] != '/') ++pos;
        name = start;
        if (path[pos]) {
            // Intermediate component: resolve into a child.
            if (!parent->finddir) return false;
            vfs::VfsNode* child = parent->finddir(parent, name);
            if (!child) return false;
            parent = child;
        }
    }
    if (!name) return false;
    // Create the tmpfs directory node and hook it under the parent.
    vfs::VfsNode* node = make_dir_node(name);
    if (!node) return false;
    if (!parent->finddir) return false;
    // If the node already exists (e.g. /tmp pre-created in initrd), replace
    // its semantics with the tmpfs dir.
    vfs::VfsNode* existing = parent->finddir(parent, name);
    if (existing) {
        existing->type = vfs::DIRECTORY_TYPE;
        existing->finddir = node->finddir;
        existing->readdir = node->readdir;
        existing->create = node->create;
        existing->truncate = node->truncate;
        existing->fs_data = node->fs_data;
        existing->mode = node->mode;
        kfree(node);
    } else {
        // Attach as a child of the parent via the parent's create hook if
        // it has one; otherwise we cannot add children (e.g. initrd root).
        if (parent->create) {
            vfs::VfsNode* attached = parent->create(parent, name, node->mode);
            if (attached) {
                attached->type = vfs::DIRECTORY_TYPE;
                attached->finddir = node->finddir;
                attached->readdir = node->readdir;
                attached->create = node->create;
                attached->truncate = node->truncate;
                attached->fs_data = node->fs_data;
                attached->mode = node->mode;
                kfree(node);
            } else {
                kfree(node);
                return false;
            }
        } else {
            // No create hook on the parent: cannot mount here.
            kfree(node);
            return false;
        }
    }
    kernel::kprintf("[+] tmpfs mounted at '%s'\n", path);
    return true;
}

} // namespace tmpfs
