#include "kernel/partition.hpp"
#include "kernel/storage.hpp"

extern "C" void* kmalloc(size_t size) {
    static uint8_t heap[64 * 1024] __attribute__((aligned(4096)));
    static size_t used = 0;
    if (used + size > sizeof(heap)) return nullptr;
    void* result = heap + used;
    used += (size + 7) & ~static_cast<size_t>(7);
    return result;
}
extern "C" void kfree(void*) {}
namespace kernel { void kprintf(const char*, ...) {} }

namespace {
static uint8_t disk[4096 * 512] __attribute__((aligned(4096)));
static void put32(uint8_t* p, uint32_t value) { for (uint32_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8)); }
static void put64(uint8_t* p, uint64_t value) { for (uint32_t i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8)); }
static uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
static storage::Status fake_submit(storage::Device* device, storage::Request* request) {
    if (!device || !request || request->type != storage::RequestType::Read) return storage::Status::Unsupported;
    if (request->lba >= device->geometry.total_blocks || request->block_count != 1) return storage::Status::OutOfBounds;
    const size_t offset = static_cast<size_t>(request->lba) * 512;
    for (uint32_t i = 0; i < 512; ++i) reinterpret_cast<uint8_t*>(request->buffer)[i] = disk[offset + i];
    if (request->complete) request->complete(request, storage::Status::Success, request->context);
    return storage::Status::Success;
}
static storage::Status fake_cancel(storage::Device*, storage::Request*) { return storage::Status::Unsupported; }
static storage::Status fake_flush(storage::Device*) { return storage::Status::Success; }
static storage::Status fake_reset(storage::Device*) { return storage::Status::Success; }
static storage::Status fake_eject(storage::Device*) { return storage::Status::Unsupported; }
static const storage::DeviceOps fake_ops{fake_submit, fake_cancel, fake_flush, fake_reset, fake_eject};
static storage::Device fake_device{
    0, 0, 1, storage::DeviceType::Removable, storage::Protocol::Memory,
    {4096, 512, 512, 32}, storage::DEVICE_REMOVABLE | storage::DEVICE_READ_ONLY,
    storage::State::Discovered, "unitdisk", "Storage unit disk", &fake_ops, nullptr
};
static storage::Status fake_probe(storage::Device*) { return storage::Status::Success; }
static storage::Status fake_start(storage::Device*) { return storage::Status::Success; }
static const storage::DriverMatch fake_match{storage::Protocol::Memory, 0, 0, 0, 0, "unit,memory"};
static const storage::Driver fake_driver{"unit-driver", &fake_match, 1, fake_probe, fake_start, nullptr, nullptr, nullptr};

static void build_gpt() {
    for (size_t i = 0; i < sizeof(disk); ++i) disk[i] = 0;
    uint8_t* header = disk + 512;
    const uint8_t signature[8] = {'E','F','I',' ','P','A','R','T'};
    for (uint32_t i = 0; i < 8; ++i) header[i] = signature[i];
    put32(header + 8, 0x00010000); put32(header + 12, 92);
    put64(header + 24, 1); put64(header + 32, 4095);
    put64(header + 40, 34); put64(header + 48, 4062);
    put64(header + 72, 2); put32(header + 80, 1); put32(header + 84, 128);
    uint8_t* entry = disk + 1024;
    entry[0] = 0xAA; entry[1] = 0xBB;
    put64(entry + 32, 2048); put64(entry + 40, 3071); put64(entry + 48, 1ull << 2);
    put32(header + 16, 0); put32(header + 16, crc32(header, 92));
}
static void build_mbr() {
    for (size_t i = 0; i < sizeof(disk); ++i) disk[i] = 0;
    uint8_t* mbr = disk;
    mbr[446] = 0x80; mbr[450] = 0x0C; put32(mbr + 454, 8); put32(mbr + 458, 128);
    mbr[510] = 0x55; mbr[511] = 0xAA;
}
}

int main() {
    storage::Manager::init();
    if (storage::Manager::register_driver(&fake_driver) != storage::Status::Success) return 1;
    if (storage::Manager::find_driver(storage::Protocol::Memory) != &fake_driver) return 2;
    if (storage::Manager::register_device(&fake_device) != storage::Status::Success) return 3;
    uint8_t aligned[512] __attribute__((aligned(512))){};
    storage::Request outside{storage::RequestType::Read, 4096, 1, aligned, 0, nullptr, nullptr};
    if (storage::Manager::submit(&fake_device, &outside) != storage::Status::OutOfBounds) return 4;
    storage::Request unaligned{storage::RequestType::Read, 0, 1, aligned + 1, 0, nullptr, nullptr};
    if (storage::Manager::submit(&fake_device, &unaligned) != storage::Status::InvalidRequest) return 5;
    storage::Request write{storage::RequestType::Write, 0, 1, aligned, 0, nullptr, nullptr};
    if (storage::Manager::submit(&fake_device, &write) != storage::Status::ReadOnly) return 6;

    storage::partition::Table table{};
    build_gpt();
    if (storage::partition::scan(&fake_device, &table) != storage::Status::Success || !table.gpt || table.count != 1) return 7;
    if (table.entries[0].first_lba != 2048 || table.entries[0].last_lba != 3071 || !table.entries[0].bootable) return 8;
    build_mbr();
    if (storage::partition::scan(&fake_device, &table) != storage::Status::Success || table.gpt || table.count != 1) return 9;
    if (table.entries[0].first_lba != 8 || table.entries[0].last_lba != 135 || !table.entries[0].bootable) return 10;
    if (storage::Manager::unregister_device(&fake_device) != storage::Status::Success) return 11;
    if (storage::Manager::submit(&fake_device, &outside) != storage::Status::DeviceRemoved) return 12;
    return 0;
}
