#ifndef OMEGA_KERNEL_VMM_HPP
#define OMEGA_KERNEL_VMM_HPP

#include "std/cstdint.hpp"

namespace memory {

enum PageFlags {
    PAGE_PRESENT  = (1 << 0),
    PAGE_WRITABLE = (1 << 1),
    PAGE_USER     = (1 << 2),
    PAGE_EXEC     = (1 << 3),
    PAGE_DEVICE   = (1 << 4),
};

class VirtualMemoryManager {
public:
    static void init();
    static bool map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);
    static bool unmap_page(uintptr_t virt_addr);
    static uintptr_t get_physical_address(uintptr_t virt_addr);

private:
    static uintptr_t current_pml4_or_ttbr;
};

} // namespace memory

#endif // OMEGA_KERNEL_VMM_HPP
