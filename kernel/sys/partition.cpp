#include "kernel/partition.hpp"

namespace storage::partition {
namespace {
static uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static uint64_t le64(const uint8_t* p) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (i * 8);
    return value;
}
static uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
static void clear(Table* table) {
    table->gpt = false; table->valid = false; table->count = 0;
    for (uint32_t i = 0; i < MAX_PARTITIONS; ++i) table->entries[i] = {};
}
static bool read_block(Device* device, uint64_t lba, void* buffer) {
    Request request{RequestType::Read, lba, 1, buffer, 0, nullptr, nullptr};
    return Manager::submit_sync(device, &request) == Status::Success;
}
static bool zero_guid(const uint8_t* guid) {
    for (uint32_t i = 0; i < 16; ++i) if (guid[i]) return false;
    return true;
}
}

Status scan(Device* device, Table* table) {
    if (!device || !table || device->geometry.logical_block_size < 512 ||
        device->geometry.logical_block_size > 4096) return Status::InvalidRequest;
    clear(table);
    const uint32_t block_size = device->geometry.logical_block_size;
    uint8_t sector[4096] __attribute__((aligned(4096))){};

    if (read_block(device, 1, sector) && sector[0] == 'E' && sector[1] == 'F' &&
        sector[2] == 'I' && sector[3] == ' ' && sector[4] == 'P' && sector[5] == 'A' &&
        sector[6] == 'R' && sector[7] == 'T') {
        const uint32_t header_size = le32(sector + 12);
        const uint32_t stored_crc = le32(sector + 16);
        if (header_size < 92 || header_size > block_size) return Status::ProtocolError;
        uint8_t header_copy[4096] __attribute__((aligned(4096))){};
        for (uint32_t i = 0; i < header_size; ++i) header_copy[i] = sector[i];
        header_copy[16] = header_copy[17] = header_copy[18] = header_copy[19] = 0;
        if (crc32(header_copy, header_size) != stored_crc) return Status::ProtocolError;
        const uint64_t entries_lba = le64(sector + 72);
        const uint32_t entry_count = le32(sector + 80);
        const uint32_t entry_size = le32(sector + 84);
        if (!entry_count || entry_size < 128 || entry_size > 4096 || entries_lba >= device->geometry.total_blocks) return Status::ProtocolError;
        table->gpt = true; table->valid = true;
        const uint32_t count = entry_count < MAX_PARTITIONS ? entry_count : MAX_PARTITIONS;
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t byte_offset = static_cast<uint64_t>(i) * entry_size;
            const uint64_t lba = entries_lba + byte_offset / block_size;
            const uint32_t offset = static_cast<uint32_t>(byte_offset % block_size);
            if (!read_block(device, lba, sector) || offset + entry_size > block_size) return Status::IoError;
            if (zero_guid(sector + offset)) continue;
            Entry& entry = table->entries[table->count++];
            entry.index = i + 1;
            for (uint32_t byte = 0; byte < 16; ++byte) entry.type_guid[byte] = sector[offset + byte];
            entry.first_lba = le64(sector + offset + 32);
            entry.last_lba = le64(sector + offset + 40);
            entry.attributes = le64(sector + offset + 48);
            entry.bootable = (entry.attributes & (1ull << 2)) != 0;
            if (entry.first_lba > entry.last_lba || entry.last_lba >= device->geometry.total_blocks) return Status::ProtocolError;
        }
        return Status::Success;
    }

    if (!read_block(device, 0, sector) || sector[510] != 0x55 || sector[511] != 0xAA) return Status::NotReady;
    table->valid = true;
    for (uint32_t i = 0; i < 4 && table->count < MAX_PARTITIONS; ++i) {
        const uint8_t* entry_data = sector + 446 + i * 16;
        const uint8_t type = entry_data[4];
        const uint32_t first = le32(entry_data + 8);
        const uint32_t count = le32(entry_data + 12);
        if (!type || !count) continue;
        Entry& entry = table->entries[table->count++];
        entry.index = i + 1;
        entry.first_lba = first;
        entry.last_lba = static_cast<uint64_t>(first) + count - 1;
        entry.bootable = entry_data[0] == 0x80;
        entry.protective_mbr = type == 0xEE;
    }
    return Status::Success;
}

} // namespace storage::partition
