#include "kernel/initrd.hpp"
#include "kernel/kprint.hpp"

namespace initrd {

vfs::VfsNode* Initrd::init(uintptr_t location) {
    (void)location;
    kernel::kprintf("[+] RAM Disk (Initrd) Initialized.\n");
    return nullptr;
}

} // namespace initrd
