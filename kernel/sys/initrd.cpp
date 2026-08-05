#include "kernel/initrd.hpp"
#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"

namespace initrd {

static uint8_t* initrd_location = nullptr;
static InitrdHeader* header = nullptr;

static int initrd_read(vfs::VfsNode* node, size_t offset, size_t size, uint8_t* buffer) {
    InitrdFileHeader* file = reinterpret_cast<InitrdFileHeader*>(node->flags);
    if (offset > file->length) return 0;
    if (offset + size > file->length) {
        size = file->length - offset;
    }
    uint8_t* src = initrd_location + file->offset + offset;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = src[i];
    }
    return size;
}

vfs::VfsNode* Initrd::init(uintptr_t location) {
    initrd_location = reinterpret_cast<uint8_t*>(location);
    header = reinterpret_cast<InitrdHeader*>(location);

    vfs::VfsNode* root = reinterpret_cast<vfs::VfsNode*>(kmalloc(sizeof(vfs::VfsNode)));
    root->name[0] = 'd'; root->name[1] = 'e'; root->name[2] = 'v'; root->name[3] = '\0';
    root->type = vfs::DIRECTORY_TYPE;
    root->size = header->nfiles;
    root->read = nullptr;
    root->write = nullptr;

    kernel::kprintf("[+] RAM Disk (Initrd) Initialized at location: %x\n", location);
    kernel::kprintf("    Total Ramdisk Files: %u\n", header->nfiles);

    return root;
}

} // namespace initrd
