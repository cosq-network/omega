#include "kernel/initrd.hpp"
#include "kernel/kprint.hpp"
#include "kernel/heap.hpp"
#include "kernel/security.hpp"

namespace {
struct InitrdEntry {
    vfs::VfsNode node;
    InitrdEntry* next;
    InitrdEntry* children;
};

static InitrdEntry* entries = nullptr;

static vfs::VfsNode* finddir(vfs::VfsNode* node, const char* name) {
    auto* owner = node == nullptr ? nullptr : reinterpret_cast<InitrdEntry*>(node->fs_data);
    auto* entry = node != nullptr && node->name[0] == '/' ? owner : (owner ? owner->children : nullptr);
    while (entry != nullptr) {
        if (name != nullptr) {
            size_t i = 0;
            while (name[i] && entry->node.name[i] && name[i] == entry->node.name[i]) ++i;
            if (!name[i] && !entry->node.name[i]) return &entry->node;
        }
        entry = entry->next;
    }
    return nullptr;
}

static int readdir(vfs::VfsNode* node, size_t offset, uint8_t* buf, size_t len) {
    auto* owner = node == nullptr ? nullptr : reinterpret_cast<InitrdEntry*>(node->fs_data);
    if (!owner || !buf) return -1;
    InitrdEntry* entry = node->name[0] == '/' ? owner : owner->children;
    for (size_t index = 0; entry && index < offset; ++index) entry = entry->next;
    if (!entry) return 0;
    size_t name_len = 0;
    while (entry->node.name[name_len]) ++name_len;
    const size_t reclen = (19 + name_len + 1 + 7) & ~size_t(7);
    if (reclen > len) return -1;
    for (size_t i = 0; i < reclen; ++i) buf[i] = 0;
    auto put64 = [&](size_t off, uint64_t value) {
        for (size_t i = 0; i < 8; ++i) buf[off + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    };
    put64(0, static_cast<uint64_t>(offset + 1));
    put64(8, static_cast<uint64_t>(offset + 1));
    buf[16] = static_cast<uint8_t>(reclen);
    buf[18] = static_cast<uint8_t>(entry->node.type == vfs::DIRECTORY_TYPE ? 4 : 8);
    for (size_t i = 0; i < name_len; ++i) buf[19 + i] = static_cast<uint8_t>(entry->node.name[i]);
    return static_cast<int>(reclen);
}

static int read_file(vfs::VfsNode* node, size_t offset, size_t size, uint8_t* buffer) {
    if (node == nullptr || buffer == nullptr || offset > node->size) return -1;
    if (size > node->size - offset) size = node->size - offset;
    const auto* data = reinterpret_cast<const uint8_t*>(node->fs_data);
    for (size_t i = 0; i < size; ++i) buffer[i] = data[offset + i];
    return static_cast<int>(size);
}

static void append_child(InitrdEntry*& list, InitrdEntry* child) {
    child->next = nullptr;
    if (!list) { list = child; return; }
    auto* tail = list;
    while (tail->next) tail = tail->next;
    tail->next = child;
}

static InitrdEntry* new_directory(const char* name) {
    auto* entry = reinterpret_cast<InitrdEntry*>(kmalloc(sizeof(InitrdEntry)));
    if (!entry) return nullptr;
    for (size_t i = 0; i < sizeof(entry->node.name); ++i) entry->node.name[i] = 0;
    for (size_t i = 0; name[i] && i + 1 < sizeof(entry->node.name); ++i) entry->node.name[i] = name[i];
    entry->node.type = vfs::DIRECTORY_TYPE;
    entry->node.size = 0;
    entry->node.flags = 0;
    entry->node.uid = security::ROOT_UID;
    entry->node.gid = security::ROOT_GID;
    entry->node.mode = 0755;
    entry->node.read = nullptr;
    entry->node.write = nullptr;
    entry->node.finddir = finddir;
    entry->node.readdir = readdir;
    entry->node.create = nullptr;
    entry->node.truncate = nullptr;
    // Directory nodes retain their owning entry so finddir/readdir can reach
    // the mutable child list while file nodes use fs_data for file bytes.
    entry->node.fs_data = entry;
    entry->children = nullptr;
    entry->next = nullptr;
    return entry;
}

static InitrdEntry* ensure_directory(InitrdEntry*& list, const char* name) {
    for (auto* item = list; item; item = item->next) {
        if (item->node.type != vfs::DIRECTORY_TYPE) continue;
        size_t i = 0;
        while (name[i] && item->node.name[i] && name[i] == item->node.name[i]) ++i;
        if (!name[i] && !item->node.name[i]) return item;
    }
    auto* created = new_directory(name);
    if (created) append_child(list, created);
    return created;
}

static void add_file(InitrdEntry*& root, const char* path, const uint8_t* data, uint32_t size) {
    char component[64]{};
    InitrdEntry** list = &root;
    size_t pos = 0;
    while (path[pos]) {
        while (path[pos] == '/') ++pos;
        if (!path[pos]) break;
        size_t length = 0;
        while (path[pos] && path[pos] != '/' && length + 1 < sizeof(component)) component[length++] = path[pos++];
        component[length] = '\0';
        bool final = path[pos] == '\0';
        if (final) {
            auto* entry = reinterpret_cast<InitrdEntry*>(kmalloc(sizeof(InitrdEntry)));
            if (!entry) return;
            for (size_t i = 0; i < sizeof(entry->node.name); ++i) entry->node.name[i] = 0;
            for (size_t i = 0; component[i] && i + 1 < sizeof(entry->node.name); ++i) entry->node.name[i] = component[i];
            entry->node.type = vfs::FILE_TYPE;
            entry->node.size = size;
            entry->node.flags = 0;
            entry->node.uid = security::ROOT_UID;
            entry->node.gid = security::ROOT_GID;
            entry->node.mode = 0755;
            entry->node.read = read_file;
            entry->node.write = nullptr;
            entry->node.finddir = nullptr;
            entry->node.readdir = nullptr;
            entry->node.create = nullptr;
            entry->node.truncate = nullptr;
            entry->node.fs_data = const_cast<uint8_t*>(data);
            entry->children = nullptr;
            append_child(*list, entry);
            return;
        }
        auto* directory = ensure_directory(*list, component);
        if (!directory) return;
        list = &directory->children;
    }
}
}

namespace initrd {

vfs::VfsNode* Initrd::init(uintptr_t location) {
    entries = nullptr;
    kernel::kprintf("[+] RAM Disk (Initrd) Initialized.\n");
    auto* header = reinterpret_cast<const InitrdHeader*>(location);
    auto valid_header = [](uintptr_t candidate) {
        const auto* candidate_header = reinterpret_cast<const InitrdHeader*>(candidate);
        if (candidate_header == nullptr || candidate_header->nfiles == 0 || candidate_header->nfiles > 128) return false;
        const auto* candidate_file = reinterpret_cast<const InitrdFileHeader*>(candidate + sizeof(InitrdHeader));
        return candidate_file->magic == 0xBF && candidate_file->offset >= sizeof(InitrdHeader) &&
               candidate_file->offset < 0x02000000 && candidate_file->length != 0 &&
               reinterpret_cast<const uint8_t*>(candidate + candidate_file->offset)[0] == 0x7F;
    };
    if (!valid_header(location)) {
        // QEMU's direct-kernel initrd loader may choose a platform-dependent
        // low-RAM address. Search the reserved identity-mapped bring-up range
        // for the self-describing Omega header instead of trusting one ABI.
#if defined(__x86_64__)
        constexpr uintptr_t scan_start = 0x600000ull, scan_end = 0x01000000ull;
#endif
#if defined(__x86_64__)
        for (uintptr_t candidate = scan_start; candidate < scan_end; candidate += 0x1000) {
            if (valid_header(candidate)) { location = candidate; header = reinterpret_cast<const InitrdHeader*>(location); break; }
        }
#endif
    }
    if (!valid_header(location)) {
        kernel::kprintf("[!] Initrd rejected: valid image not found.\n");
        return nullptr;
    }

    auto* root = vfs::VirtualFilesystem::get_root();
    if (root == nullptr) return nullptr;
    root->finddir = finddir;
    root->fs_data = nullptr;
    auto* files = reinterpret_cast<const InitrdFileHeader*>(location + sizeof(InitrdHeader));
    for (uint32_t i = 0; i < header->nfiles; ++i) {
        if (files[i].magic != 0xBF || files[i].length == 0 || files[i].offset < sizeof(InitrdHeader)) continue;
        add_file(entries, files[i].name, reinterpret_cast<const uint8_t*>(location + files[i].offset), files[i].length);
        kernel::kprintf("[+] Initrd file: %s (%u bytes)\n", files[i].name, files[i].length);
    }
    root->fs_data = entries;
    if (entries != nullptr) kernel::kprintf("[+] Initrd userspace files registered.\n");
    return root;
}

vfs::VfsNode* Initrd::find(const char* path) {
    // The kernel performs the initial exec lookup before entering userspace;
    // permission checks for subsequent opens remain in the normal VFS path.
    if (path != nullptr && path[0] == '/' && path[1] != '\0') {
        const char* requested = path + 1;
        for (auto* entry = entries; entry != nullptr; entry = entry->next) {
            size_t i = 0;
            while (requested[i] && entry->node.name[i] && requested[i] == entry->node.name[i]) ++i;
            if (!requested[i] && !entry->node.name[i]) return &entry->node;
        }
    }
    return vfs::VirtualFilesystem::open(path);
}

} // namespace initrd
