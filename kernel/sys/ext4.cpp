#include "kernel/ext4.hpp"
#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"

namespace ext4 {
namespace {

static constexpr uint16_t EXT4_MAGIC = 0xEF53;
static constexpr uint32_t INCOMPAT_FILETYPE = 0x0002;
static constexpr uint32_t INCOMPAT_EXTENTS = 0x0040;
static constexpr uint32_t INCOMPAT_64BIT = 0x0080;
static constexpr uint32_t INCOMPAT_FLEX_BG = 0x0200;
static constexpr uint32_t EXT4_EXTENTS = 0x00080000;

struct Fs {
    storage::Device* device;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint16_t inode_size;
    uint16_t desc_size;
    uint64_t blocks_count;
    uint64_t first_data_block;
    uint32_t groups;
    bool readonly;
};
struct Inode {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t flags;
    uint64_t size;
    uint8_t block[60];
};
struct NodeData { Fs* fs; uint32_t inode; };
struct ExtentHeader { uint16_t magic, entries, max, depth; uint32_t generation; };
struct Extent { uint32_t logical; uint16_t length; uint16_t start_high; uint32_t start_low; };

static Fs* active = nullptr;

static uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8); }
static uint32_t le32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }
static void copy_bytes(void* dst, const void* src, size_t count) { auto* d = reinterpret_cast<uint8_t*>(dst); const auto* s = reinterpret_cast<const uint8_t*>(src); for (size_t i = 0; i < count; ++i) d[i] = s[i]; }
static bool equal_name(const char* a, const char* b) { size_t i = 0; while (a[i] && b[i] && a[i] == b[i]) ++i; return a[i] == '\0' && b[i] == '\0'; }

static bool io(Fs* fs, storage::RequestType type, uint64_t lba, uint32_t count, void* buffer) {
    storage::Request request{type, lba, count, buffer, 0, nullptr, nullptr};
    return storage::Manager::submit_sync(fs->device, &request) == storage::Status::Success;
}
static bool valid_block(const Fs* fs, uint64_t block) {
    if (!fs || !fs->device || !fs->block_size || block >= fs->blocks_count) return false;
    if (fs->device->geometry.total_blocks > (~static_cast<uint64_t>(0) / 512ull)) return false;
    const uint64_t device_bytes = fs->device->geometry.total_blocks * 512ull;
    return device_bytes >= fs->block_size && block <= (device_bytes - fs->block_size) / fs->block_size;
}
static bool read_bytes(Fs* fs, uint64_t byte_offset, uint32_t bytes, void* buffer) {
    if ((byte_offset & 511u) || (bytes & 511u)) return false;
    return io(fs, storage::RequestType::Read, byte_offset / 512u, bytes / 512u, buffer);
}
static bool write_bytes(Fs* fs, uint64_t byte_offset, uint32_t bytes, const void* buffer) {
    if ((byte_offset & 511u) || (bytes & 511u) || (fs->readonly)) return false;
    storage::Request request{storage::RequestType::Write, byte_offset / 512u, bytes / 512u, const_cast<void*>(buffer), 0, nullptr, nullptr};
    return storage::Manager::submit_sync(fs->device, &request) == storage::Status::Success;
}
static bool read_block(Fs* fs, uint64_t block, void* buffer) {
    return valid_block(fs, block) && read_bytes(fs, block * fs->block_size, fs->block_size, buffer);
}
static bool write_block(Fs* fs, uint64_t block, const void* buffer) {
    return valid_block(fs, block) && write_bytes(fs, block * fs->block_size, fs->block_size, buffer);
}

static bool read_inode(Fs* fs, uint32_t inode_number, Inode* out) {
    if (!fs || !out || !inode_number || !fs->inodes_per_group) return false;
    const uint32_t group = (inode_number - 1) / fs->inodes_per_group;
    if (group >= fs->groups) return false;
    const uint32_t index = (inode_number - 1) % fs->inodes_per_group;
    const uint64_t descriptor_block = (fs->block_size == 1024 ? 2 : 1) + (static_cast<uint64_t>(group) * fs->desc_size) / fs->block_size;
    const uint64_t descriptor_byte_offset = static_cast<uint64_t>(group) * fs->desc_size;
    const uint32_t descriptor_offset = descriptor_byte_offset % fs->block_size;
    if (descriptor_offset + fs->desc_size > fs->block_size) return false;
    alignas(4096) uint8_t descriptor[4096]{};
    if (!read_block(fs, descriptor_block, descriptor)) return false;
    uint64_t inode_table = le32(descriptor + descriptor_offset + 8);
    if (fs->desc_size >= 64) inode_table |= static_cast<uint64_t>(le32(descriptor + descriptor_offset + 40)) << 32;
    const uint64_t byte_offset = static_cast<uint64_t>(index) * fs->inode_size;
    const uint64_t inode_block = inode_table + byte_offset / fs->block_size;
    const uint32_t inode_offset = byte_offset % fs->block_size;
    alignas(4096) uint8_t block[4096]{};
    if (!read_block(fs, inode_block, block) || inode_offset + fs->inode_size > fs->block_size) return false;
    const uint8_t* raw = block + inode_offset;
    out->mode = le16(raw);
    out->uid = le16(raw + 2);
    out->gid = le16(raw + 24);
    out->flags = le32(raw + 32);
    out->size = le32(raw + 4);
    if (fs->inode_size >= 112) out->size |= static_cast<uint64_t>(le32(raw + 108)) << 32;
    copy_bytes(out->block, raw + 40, sizeof(out->block));
    return true;
}

static bool map_block(Fs* fs, const Inode* inode, uint32_t logical, uint64_t* physical) {
    (void)fs;
    if (!inode || !physical) return false;
    if (inode->flags & EXT4_EXTENTS) {
        const auto* header = reinterpret_cast<const ExtentHeader*>(inode->block);
        if (le16(reinterpret_cast<const uint8_t*>(&header->magic)) != 0xF30A || le16(reinterpret_cast<const uint8_t*>(&header->depth)) != 0) return false;
        const uint16_t count = le16(reinterpret_cast<const uint8_t*>(&header->entries));
        if (count > (sizeof(inode->block) - sizeof(ExtentHeader)) / sizeof(Extent)) return false;
        const auto* extents = reinterpret_cast<const Extent*>(inode->block + sizeof(ExtentHeader));
        for (uint16_t i = 0; i < count; ++i) {
            const uint32_t length = le16(reinterpret_cast<const uint8_t*>(&extents[i].length)) & 0x7FFFu;
            const uint32_t first = le32(reinterpret_cast<const uint8_t*>(&extents[i].logical));
            if (logical >= first && logical - first < length) {
                const uint64_t start = le32(reinterpret_cast<const uint8_t*>(&extents[i].start_low)) | (static_cast<uint64_t>(le16(reinterpret_cast<const uint8_t*>(&extents[i].start_high))) << 32);
                *physical = start + logical - first;
                return true;
            }
        }
        return false;
    }
    if (logical < 12) {
        *physical = le32(inode->block + logical * 4);
        return *physical != 0;
    }
    return false;
}

static int file_read(vfs::VfsNode* node, size_t offset, size_t size, uint8_t* output) {
    if (!node || !output || !node->fs_data) return -1;
    auto* data = reinterpret_cast<NodeData*>(node->fs_data);
    Inode inode{}; if (!read_inode(data->fs, data->inode, &inode)) return -1;
    if (offset >= inode.size) return 0;
    if (size > inode.size - offset) size = inode.size - offset;
    alignas(4096) uint8_t block[4096]{};
    size_t done = 0;
    while (done < size) {
        const uint32_t logical = static_cast<uint32_t>((offset + done) / data->fs->block_size);
        const uint32_t inside = static_cast<uint32_t>((offset + done) % data->fs->block_size);
        uint64_t physical = 0;
        if (!map_block(data->fs, &inode, logical, &physical) || !read_block(data->fs, physical, block)) return -1;
        const size_t amount = (size - done < data->fs->block_size - inside) ? size - done : data->fs->block_size - inside;
        copy_bytes(output + done, block + inside, amount); done += amount;
    }
    return static_cast<int>(done);
}

static int file_write(vfs::VfsNode* node, size_t offset, size_t size, const uint8_t* input) {
    if (!node || !input || !node->fs_data) return -1;
    auto* data = reinterpret_cast<NodeData*>(node->fs_data);
    Inode inode{}; if (!read_inode(data->fs, data->inode, &inode) || offset >= inode.size) return -1;
    if (size > inode.size - offset) size = inode.size - offset;
    alignas(4096) uint8_t block[4096]{};
    size_t done = 0;
    while (done < size) {
        const uint32_t logical = static_cast<uint32_t>((offset + done) / data->fs->block_size);
        const uint32_t inside = static_cast<uint32_t>((offset + done) % data->fs->block_size);
        uint64_t physical = 0;
        if (!map_block(data->fs, &inode, logical, &physical) || !read_block(data->fs, physical, block)) return -1;
        const size_t amount = (size - done < data->fs->block_size - inside) ? size - done : data->fs->block_size - inside;
        copy_bytes(block + inside, input + done, amount);
        if (!write_block(data->fs, physical, block)) return -1;
        done += amount;
    }
    return static_cast<int>(done);
}

static vfs::VfsNode* make_node(Fs* fs, uint32_t inode_number, const char* name);
static vfs::VfsNode* find_directory(vfs::VfsNode* node, const char* wanted) {
    if (!node || !node->fs_data || node->type != vfs::DIRECTORY_TYPE) return nullptr;
    auto* data = reinterpret_cast<NodeData*>(node->fs_data);
    Inode inode{}; if (!read_inode(data->fs, data->inode, &inode)) return nullptr;
    alignas(4096) uint8_t block[4096]{};
    const uint32_t count = static_cast<uint32_t>((inode.size + data->fs->block_size - 1) / data->fs->block_size);
    for (uint32_t logical = 0; logical < count; ++logical) {
        uint64_t physical = 0;
        if (!map_block(data->fs, &inode, logical, &physical) || !read_block(data->fs, physical, block)) continue;
        uint32_t offset = 0;
        while (offset + 8 <= data->fs->block_size) {
            const uint32_t child = le32(block + offset); const uint16_t record = le16(block + offset + 4); const uint8_t name_length = block[offset + 6];
            if (record < 8 || offset + record > data->fs->block_size || name_length > record - 8) break;
            if (child && name_length < 64) {
                char name[64]{}; for (uint8_t i = 0; i < name_length; ++i) name[i] = static_cast<char>(block[offset + 8 + i]);
                if (equal_name(name, wanted)) return make_node(data->fs, child, name);
            }
            offset += record;
        }
    }
    return nullptr;
}

static vfs::VfsNode* make_node(Fs* fs, uint32_t inode_number, const char* name) {
    Inode inode{}; if (!read_inode(fs, inode_number, &inode)) return nullptr;
    auto* node = reinterpret_cast<vfs::VfsNode*>(kmalloc(sizeof(vfs::VfsNode)));
    auto* data = reinterpret_cast<NodeData*>(kmalloc(sizeof(NodeData)));
    if (!node || !data) return nullptr;
    for (size_t i = 0; i < sizeof(*node); ++i) reinterpret_cast<uint8_t*>(node)[i] = 0;
    data->fs = fs; data->inode = inode_number;
    for (size_t i = 0; i + 1 < sizeof(node->name) && name && name[i]; ++i) node->name[i] = name[i];
    node->name[sizeof(node->name) - 1] = '\0';
    node->type = (inode.mode & 0xF000u) == 0x4000u ? vfs::DIRECTORY_TYPE : vfs::FILE_TYPE;
    node->uid = inode.uid;
    node->gid = inode.gid;
    node->mode = inode.mode & 07777u;
    node->size = inode.size; node->flags = fs->readonly ? 1u : 0u;
    node->read = file_read; node->write = node->type == vfs::FILE_TYPE ? file_write : nullptr;
    node->finddir = node->type == vfs::DIRECTORY_TYPE ? find_directory : nullptr; node->fs_data = data;
    return node;
}
}

storage::Status mount(storage::Device* device, vfs::VfsNode** root) {
    if (!device || device->geometry.logical_block_size != 512) return storage::Status::InvalidRequest;
    auto* fs = reinterpret_cast<Fs*>(kmalloc(sizeof(Fs))); if (!fs) return storage::Status::IoError;
    fs->device = device; fs->readonly = (device->flags & storage::DEVICE_READ_ONLY) != 0;
    alignas(4096) uint8_t super[4096]{};
    if (!read_bytes(fs, 1024, 1024, super) || le16(super + 0x38) != EXT4_MAGIC) return storage::Status::ProtocolError;
    const uint32_t log = le32(super + 0x18); if (log > 2) return storage::Status::Unsupported;
    fs->block_size = 1024u << log; fs->blocks_per_group = le32(super + 0x20); fs->inodes_per_group = le32(super + 0x28);
    fs->inode_size = le16(super + 0x58); if (fs->inode_size < 128 || fs->inode_size > fs->block_size || fs->block_size % fs->inode_size) return storage::Status::ProtocolError;
    fs->desc_size = le16(super + 0xFE); if (fs->desc_size < 32) fs->desc_size = 32;
    fs->blocks_count = le32(super + 0x04); fs->blocks_count |= static_cast<uint64_t>(le32(super + 0x150)) << 32;
    fs->first_data_block = le32(super + 0x14);
    const uint32_t incompat = le32(super + 0x60);
    if (incompat & ~(INCOMPAT_FILETYPE | INCOMPAT_EXTENTS | INCOMPAT_64BIT | INCOMPAT_FLEX_BG)) return storage::Status::Unsupported;
    if (!fs->blocks_per_group || !fs->inodes_per_group || fs->desc_size > fs->block_size ||
        fs->blocks_count == 0 || fs->blocks_count > (device->geometry.total_blocks * 512ull) / fs->block_size) {
        return storage::Status::ProtocolError;
    }
    fs->groups = static_cast<uint32_t>((fs->blocks_count - 1) / fs->blocks_per_group + 1);
    if (!fs->groups || fs->groups > 65536) return storage::Status::ProtocolError;
    auto* root_node = make_node(fs, 2, "/"); if (!root_node || root_node->type != vfs::DIRECTORY_TYPE) return storage::Status::ProtocolError;
    active = fs;
    if (root) *root = root_node; else vfs::VirtualFilesystem::mount_root(root_node);
    kernel::kprintf("[+] ext4 mounted: block_size=%u groups=%u inode_size=%u\n", fs->block_size, fs->groups, fs->inode_size);
    return storage::Status::Success;
}

bool mounted() { return active != nullptr; }

} // namespace ext4
