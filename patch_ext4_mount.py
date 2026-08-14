import sys
content = open('kernel/sys/ext4.cpp').read()

old_mount = """storage::Status mount(storage::Device* device, vfs::VfsNode** root) {
    if (!device || device->geometry.logical_block_size != 512) return storage::Status::InvalidRequest;
    auto* fs = reinterpret_cast<Fs*>(kmalloc(sizeof(Fs))); if (!fs) return storage::Status::IoError;
    fs->device = device; fs->readonly = (device->flags & storage::DEVICE_READ_ONLY) != 0;
    alignas(4096) uint8_t super[4096]{};
    if (!read_bytes(fs, 1024, 1024, super) || le16(super + 0x38) != EXT4_MAGIC) return storage::Status::ProtocolError;
    const uint32_t log = le32(super + 0x18); if (log > 2) return storage::Status::Unsupported;"""

new_mount = """storage::Status mount(storage::Device* device, vfs::VfsNode** root) {
    if (!device || device->geometry.logical_block_size != 512) return storage::Status::InvalidRequest;
    auto* fs = reinterpret_cast<Fs*>(kmalloc(sizeof(Fs))); if (!fs) return storage::Status::IoError;
    fs->device = device; fs->readonly = (device->flags & storage::DEVICE_READ_ONLY) != 0;
    fs->partition_offset = 0;

    alignas(4096) uint8_t super[4096]{};
    bool found_superblock = false;

    storage::partition::Table table{};
    if (storage::partition::scan(device, &table) == storage::Status::Success && table.valid) {
        for (uint32_t i = 0; i < table.count; ++i) {
            fs->partition_offset = table.entries[i].first_lba;
            if (read_bytes(fs, 1024, 1024, super) && le16(super + 0x38) == EXT4_MAGIC) {
                found_superblock = true;
                break;
            }
        }
    }

    if (!found_superblock) {
        fs->partition_offset = 0;
        if (read_bytes(fs, 1024, 1024, super) && le16(super + 0x38) == EXT4_MAGIC) {
            found_superblock = true;
        }
    }

    if (!found_superblock) return storage::Status::ProtocolError;
    const uint32_t log = le32(super + 0x18); if (log > 2) return storage::Status::Unsupported;"""

content = content.replace(old_mount, new_mount)
open('kernel/sys/ext4.cpp', 'w').write(content)
