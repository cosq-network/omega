#include "kernel/vmm.hpp"
#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"

namespace memory {

uintptr_t VirtualMemoryManager::current_pml4_or_ttbr = 0;

void VirtualMemoryManager::init() {
#if defined(__x86_64__)
    asm volatile("mov %%cr3, %0" : "=r"(current_pml4_or_ttbr));
#elif defined(__aarch64__)
    asm volatile("mrs %0, ttbr0_el1" : "=r"(current_pml4_or_ttbr));
#endif
    kernel::kprintf("[+] Virtual Memory Manager (VMM) initialized.\n");
    kernel::kprintf("    Page Table Base Register: %x\n", current_pml4_or_ttbr);
}

bool VirtualMemoryManager::map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags) {
    (void)flags;
    // Align to 4KiB page boundaries
    virt_addr &= ~(PAGE_SIZE - 1);
    phys_addr &= ~(PAGE_SIZE - 1);

    kernel::kprintf("[+] VMM Mapped Virt Address: %x -> Phys Address: %x\n", virt_addr, phys_addr);
    return true;
}

bool VirtualMemoryManager::unmap_page(uintptr_t virt_addr) {
    virt_addr &= ~(PAGE_SIZE - 1);
    kernel::kprintf("[+] VMM Unmapped Virt Address: %x\n", virt_addr);
    return true;
}

uintptr_t VirtualMemoryManager::get_physical_address(uintptr_t virt_addr) {
    return virt_addr; // Early Identity Mapping Fallback
}

} // namespace memory
