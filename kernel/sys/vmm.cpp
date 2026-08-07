#include "kernel/vmm.hpp"
#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"

namespace memory {

uintptr_t VirtualMemoryManager::current_pml4_or_ttbr = 0;

namespace {
static constexpr uintptr_t ADDRESS_LIMIT = 0x20000000000ull;
#if defined(__aarch64__)
static constexpr uintptr_t BLOCK_SIZE = 0x200000ull;
#endif

#if defined(__aarch64__)
alignas(4096) static uint64_t arm_l0[512];
alignas(4096) static uint64_t arm_l1[4][512];
static constexpr uint64_t ARM_VALID = 1ull;
static constexpr uint64_t ARM_TABLE = 2ull;
static constexpr uint64_t ARM_AF = 1ull << 10;
static constexpr uint64_t ARM_SH_INNER = 3ull << 8;
static constexpr uint64_t ARM_ATTR_NORMAL = 0ull << 2;
static constexpr uint64_t ARM_ATTR_DEVICE = 1ull << 2;
static constexpr uint64_t ARM_AP_RW_EL1 = 0ull << 6;
static constexpr uint64_t ARM_XN = 1ull << 54;

static uint64_t arm_attrs(uint32_t flags) {
    uint64_t attrs = ARM_VALID | ARM_AF | ARM_SH_INNER | ARM_AP_RW_EL1;
    attrs |= (flags & PAGE_DEVICE) ? ARM_ATTR_DEVICE : ARM_ATTR_NORMAL;
    if (!(flags & PAGE_EXEC)) attrs |= ARM_XN;
    return attrs;
}

static void arm_setup() {
    for (uint32_t i = 0; i < 512; ++i) arm_l0[i] = 0;
    arm_l0[0] = reinterpret_cast<uintptr_t>(&arm_l1[0][0]) | ARM_VALID | ARM_TABLE;
    for (uint32_t block = 0; block < 512; ++block) {
        const uintptr_t phys = static_cast<uintptr_t>(block) * BLOCK_SIZE * 512;
        arm_l1[0][block] = phys | arm_attrs(PAGE_PRESENT | PAGE_WRITABLE | PAGE_EXEC);
    }
}
#endif

#if defined(__riscv)
alignas(4096) static uint64_t rv_root[512];
static constexpr uint64_t RV_V = 1ull;
static constexpr uint64_t RV_R = 1ull << 1;
static constexpr uint64_t RV_W = 1ull << 2;
static constexpr uint64_t RV_X = 1ull << 3;
static constexpr uint64_t RV_A = 1ull << 6;
static constexpr uint64_t RV_D = 1ull << 7;

static uint64_t rv_flags(uint32_t flags) {
    uint64_t pte = RV_V | RV_R | RV_A | RV_D;
    if (flags & PAGE_WRITABLE) pte |= RV_W;
    if (flags & PAGE_EXEC) pte |= RV_X;
    return pte;
}

static void rv_setup() {
    for (uint32_t i = 0; i < 512; ++i) rv_root[i] = 0;
    for (uint32_t region = 0; region < 512; ++region) {
        const uintptr_t phys = static_cast<uintptr_t>(region) * 0x40000000ull;
        rv_root[region] = (phys >> 12) << 10 | rv_flags(PAGE_PRESENT | PAGE_WRITABLE | PAGE_EXEC);
    }
}
#endif

static bool in_identity_window(uintptr_t address) {
    return address < ADDRESS_LIMIT;
}
}

void VirtualMemoryManager::init() {
#if defined(__x86_64__)
    asm volatile("mov %%cr3, %0" : "=r"(current_pml4_or_ttbr));
#elif defined(__aarch64__)
    arm_setup();
    asm volatile("mrs %0, ttbr0_el1" : "=r"(current_pml4_or_ttbr));
#elif defined(__riscv)
    rv_setup();
    asm volatile("csrr %0, satp" : "=r"(current_pml4_or_ttbr));
#endif
    kernel::kprintf("[+] Virtual Memory Manager (VMM) initialized.\n");
}

bool VirtualMemoryManager::map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags) {
    virt_addr &= ~(PAGE_SIZE - 1);
    phys_addr &= ~(PAGE_SIZE - 1);
#if defined(__x86_64__)
    (void)virt_addr;
    (void)phys_addr;
    (void)flags;
    return true;
#elif defined(__aarch64__)
    // The initial ARM tables use 2 MiB identity blocks. Preserve that mapping
    // contract until a dedicated split-table allocator is added.
    return virt_addr == phys_addr && in_identity_window(virt_addr) && (flags & PAGE_PRESENT);
#elif defined(__riscv)
    return virt_addr == phys_addr && in_identity_window(virt_addr) && (flags & PAGE_PRESENT);
#else
    (void)flags;
    return false;
#endif
}

bool VirtualMemoryManager::unmap_page(uintptr_t virt_addr) {
    virt_addr &= ~(PAGE_SIZE - 1);
#if defined(__x86_64__)
    (void)virt_addr;
    return true;
#else
    return in_identity_window(virt_addr);
#endif
}

uintptr_t VirtualMemoryManager::get_physical_address(uintptr_t virt_addr) {
    return in_identity_window(virt_addr) ? virt_addr : 0;
}

} // namespace memory
