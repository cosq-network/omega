#ifndef OMEGA_KERNEL_PARTITION_HPP
#define OMEGA_KERNEL_PARTITION_HPP

#include "kernel/storage.hpp"

namespace storage::partition {

static constexpr uint32_t MAX_PARTITIONS = 32;

struct Entry {
    uint32_t index;
    uint8_t type_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    bool protective_mbr;
    bool bootable;
};

struct Table {
    bool gpt;
    bool valid;
    uint32_t count;
    Entry entries[MAX_PARTITIONS];
};

Status scan(Device* device, Table* table);

} // namespace storage::partition

#endif // OMEGA_KERNEL_PARTITION_HPP
