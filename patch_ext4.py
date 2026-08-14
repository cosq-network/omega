import sys
content = open('kernel/sys/ext4.cpp').read()

if '#include "kernel/partition.hpp"' not in content:
    content = content.replace('#include "kernel/heap.hpp"', '#include "kernel/heap.hpp"\n#include "kernel/partition.hpp"')

content = content.replace('storage::Device* device;', 'storage::Device* device;\n    uint64_t partition_offset;')

content = content.replace(
    'storage::Request request{type, lba, count, buffer, 0, nullptr, nullptr};',
    'storage::Request request{type, fs->partition_offset + lba, count, buffer, 0, nullptr, nullptr};'
)

content = content.replace(
    'storage::Request request{storage::RequestType::Write, byte_offset / 512u, bytes / 512u, const_cast<void*>(buffer), 0, nullptr, nullptr};',
    'storage::Request request{storage::RequestType::Write, fs->partition_offset + byte_offset / 512u, bytes / 512u, const_cast<void*>(buffer), 0, nullptr, nullptr};'
)

open('kernel/sys/ext4.cpp', 'w').write(content)
