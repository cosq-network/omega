#include "kernel/ext4.hpp"
#include "kernel/vfs.hpp"

extern "C" void* kmalloc(size_t size) {
    static uint8_t heap[128 * 1024] __attribute__((aligned(4096)));
    static size_t used = 0;
    if (used + size > sizeof(heap)) return nullptr;
    void* result = heap + used;
    used += (size + 7) & ~static_cast<size_t>(7);
    return result;
}
extern "C" void kfree(void*) {}
namespace kernel {
void kprintf(const char*, ...) {}
}

namespace {
static constexpr uint32_t BLOCK_SIZE = 1024;
static constexpr uint32_t BLOCKS = 8192;
static uint8_t image[BLOCKS * BLOCK_SIZE] __attribute__((aligned(4096)));

static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) { for (uint32_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (i * 8)); }
static void put_extent(uint8_t* p, uint32_t logical, uint16_t length, uint64_t start) {
    put32(p, logical); put16(p + 4, length); put16(p + 6, static_cast<uint16_t>(start >> 32)); put32(p + 8, static_cast<uint32_t>(start));
}
static void put_inode(uint8_t* p, uint16_t mode, uint32_t size, uint32_t flags, uint64_t block) {
    put16(p, mode); put32(p + 4, size); put32(p + 32, flags); put16(p + 26, 1);
    put16(p + 40, 0xF30A); put16(p + 42, 1); put16(p + 44, 4); put16(p + 46, 0); put32(p + 48, 0); put_extent(p + 52, 0, 1, block);
}
static void build_image() {
    for (size_t i = 0; i < sizeof(image); ++i) image[i] = 0;
    uint8_t* super = image + 1024;
    put32(super + 4, BLOCKS); put32(super + 0x14, 1); put32(super + 0x18, 0); put32(super + 0x20, BLOCKS); put32(super + 0x28, 128);
    put16(super + 0x38, 0xEF53); put16(super + 0x58, 128); put16(super + 0xFE, 32); put32(super + 0x60, 0x0040);
    put32(image + 2 * BLOCK_SIZE + 8, 4);
    put_inode(image + 4 * BLOCK_SIZE + 128, 0x41ED, BLOCK_SIZE, 0x00080000, 5);
    put_inode(image + 4 * BLOCK_SIZE + 256, 0x81A4, 5, 0x00080000, 6);
    uint8_t* dir = image + 5 * BLOCK_SIZE;
    put32(dir, 2); put16(dir + 4, 12); dir[6] = 1; dir[7] = 2; dir[8] = '.';
    put32(dir + 12, 2); put16(dir + 16, 12); dir[18] = 2; dir[19] = 2; dir[20] = '.'; dir[21] = '.';
    put32(dir + 24, 3); put16(dir + 28, BLOCK_SIZE - 24); dir[30] = 5; dir[31] = 1;
    dir[32] = 'h'; dir[33] = 'e'; dir[34] = 'l'; dir[35] = 'l'; dir[36] = 'o';
    image[6 * BLOCK_SIZE] = 'h'; image[6 * BLOCK_SIZE + 1] = 'e'; image[6 * BLOCK_SIZE + 2] = 'l'; image[6 * BLOCK_SIZE + 3] = 'l'; image[6 * BLOCK_SIZE + 4] = 'o';
}
static storage::Status submit(storage::Device* device, storage::Request* request) {
    if (!device || !request || request->lba + request->block_count > BLOCKS * 2) return storage::Status::OutOfBounds;
    const size_t offset = static_cast<size_t>(request->lba) * 512;
    const size_t bytes = static_cast<size_t>(request->block_count) * 512;
    if (request->type == storage::RequestType::Read) for (size_t i = 0; i < bytes; ++i) reinterpret_cast<uint8_t*>(request->buffer)[i] = image[offset + i];
    else if (request->type == storage::RequestType::Write) for (size_t i = 0; i < bytes; ++i) image[offset + i] = reinterpret_cast<const uint8_t*>(request->buffer)[i];
    else return storage::Status::Unsupported;
    if (request->complete) request->complete(request, storage::Status::Success, request->context);
    return storage::Status::Success;
}
static storage::Status unsupported(storage::Device*, storage::Request*) { return storage::Status::Unsupported; }
static storage::Status flush(storage::Device*) { return storage::Status::Success; }
static storage::Status reset(storage::Device*) { return storage::Status::Unsupported; }
static storage::Status eject(storage::Device*) { return storage::Status::Unsupported; }
static const storage::DeviceOps ops{submit, unsupported, flush, reset, eject};
static storage::Device device{0, 0, 1, storage::DeviceType::Block, storage::Protocol::Memory, {BLOCKS * 2, 512, 512, 32}, storage::DEVICE_WRITABLE | storage::DEVICE_FLUSH, storage::State::Discovered, "ext4test", "ext4 test", &ops, nullptr};
}

int main() {
    build_image(); storage::Manager::init();
    if (storage::Manager::register_device(&device) != storage::Status::Success) return 1;
    vfs::VirtualFilesystem::init();
    put32(image + 1024 + 0x20, 0);
    if (ext4::mount(&device, nullptr) != storage::Status::ProtocolError) return 10;
    build_image();
    if (ext4::mount(&device, nullptr) != storage::Status::Success) return 2;
    auto* root = vfs::VirtualFilesystem::get_root(); if (!root || root->type != vfs::DIRECTORY_TYPE) return 3;
    if (!root->fs_data) return 62;
    if (!root->finddir) return 60;
    put16(image + 4 * BLOCK_SIZE + 128 + 42, 5);
    if (root->finddir(root, "hello")) return 11;
    put16(image + 4 * BLOCK_SIZE + 128 + 42, 1);
    put16(image + 5 * BLOCK_SIZE + 28, 8);
    if (root->finddir(root, "hello")) return 12;
    put16(image + 5 * BLOCK_SIZE + 28, BLOCK_SIZE - 24);
    auto* direct = root->finddir(root, "hello"); if (!direct) return 61;
    auto* file = vfs::VirtualFilesystem::open("/hello"); if (!file || file->type != vfs::FILE_TYPE) return 5;
    uint8_t data[6]{}; if (vfs::VirtualFilesystem::read(file, 0, 5, data) != 5) return 6;
    if (data[0] != 'h' || data[4] != 'o') return 7;
    const uint8_t replacement[5] = {'H','E','L','L','O'};
    if (vfs::VirtualFilesystem::write(file, 0, 5, replacement) != 5) return 8;
    for (uint32_t i = 0; i < 5; ++i) data[i] = 0;
    if (vfs::VirtualFilesystem::read(file, 0, 5, data) != 5 || data[0] != 'H' || data[4] != 'O') return 9;
    return 0;
}
