#include "kernel/vmm.hpp"
#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"

namespace memory {

uintptr_t VirtualMemoryManager::current_pml4_or_ttbr = 0;

namespace {
#if !defined(__x86_64__)
static constexpr uintptr_t ADDRESS_LIMIT = 0x20000000000ull;
#endif
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

#if !defined(__x86_64__)
static bool in_identity_window(uintptr_t address) {
    return address < ADDRESS_LIMIT;
}
#endif

#if defined(__x86_64__)
constexpr uint64_t X86_PRESENT = 1ull;
constexpr uint64_t X86_WRITABLE = 1ull << 1;
constexpr uint64_t X86_USER = 1ull << 2;
constexpr uint64_t X86_HUGE = 1ull << 7;
constexpr uint64_t X86_NX = 1ull << 63;
constexpr uintptr_t X86_ADDR_MASK = 0x000ffffffffff000ull;

static uint64_t* x86_table(uintptr_t physical) {
    return reinterpret_cast<uint64_t*>(physical & X86_ADDR_MASK);
}

static uintptr_t alloc_table() {
    const uintptr_t frame = PhysicalMemoryManager::alloc_frame();
    if (frame == 0) return 0;
    auto* table = reinterpret_cast<uint64_t*>(frame);
    for (uint32_t i = 0; i < 512; ++i) table[i] = 0;
    return frame;
}

static uint64_t table_flags(uint32_t flags) {
    uint64_t value = X86_PRESENT | X86_WRITABLE;
    if (flags & PAGE_USER) value |= X86_USER;
    return value;
}

static uint64_t leaf_flags(uint32_t flags) {
    uint64_t value = X86_PRESENT;
    if (flags & PAGE_WRITABLE) value |= X86_WRITABLE;
    if (flags & PAGE_USER) value |= X86_USER;
    if (!(flags & PAGE_EXEC)) value |= X86_NX;
    return value;
}
#endif

#if defined(__x86_64__)
static bool canonical_address(uintptr_t address) {
    const uintptr_t upper = address >> 48;
    return upper == 0 || upper == 0xffff;
}

static bool user_address(uintptr_t address) {
    return canonical_address(address) && address < 0x0000800000000000ull;
}
#endif
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
    AddressSpace current{current_pml4_or_ttbr & X86_ADDR_MASK, true};
    return map_page(&current, virt_addr, phys_addr, flags);
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
    AddressSpace current{current_pml4_or_ttbr & X86_ADDR_MASK, true};
    return unmap_page(&current, virt_addr);
#else
    return in_identity_window(virt_addr);
#endif
}

uintptr_t VirtualMemoryManager::get_physical_address(uintptr_t virt_addr) {
#if defined(__x86_64__)
    AddressSpace current{current_pml4_or_ttbr & X86_ADDR_MASK, true};
    return get_physical_address(&current, virt_addr);
#else
    return in_identity_window(virt_addr) ? virt_addr : 0;
#endif
}

bool VirtualMemoryManager::create_address_space(AddressSpace* space) {
    if (space == nullptr) return false;
#if defined(__x86_64__)
    const uintptr_t current_root = current_pml4_or_ttbr & X86_ADDR_MASK;
    if (current_root == 0) return false;
    const uintptr_t root = alloc_table();
    const uintptr_t pdpt = alloc_table();
    if (root == 0 || pdpt == 0) {
        if (root != 0) PhysicalMemoryManager::free_frame(root);
        if (pdpt != 0) PhysicalMemoryManager::free_frame(pdpt);
        return false;
    }
    auto* root_table = reinterpret_cast<uint64_t*>(root);
    auto* current_table = x86_table(current_root);
    for (uint32_t i = 0; i < 512; ++i) root_table[i] = current_table[i];
    // Do not inherit user-space PML4 entries from the currently active
    // process.  The low kernel identity entry is retained below; all other
    // entries are rebuilt on demand for the new address space.
    for (uint32_t i = 1; i < 512; ++i) root_table[i] = 0;

    // Clone the low-level directory used by the boot identity map so adding
    // user mappings cannot mutate the kernel's active page tables.
    const uintptr_t current_pdpt = current_table[0] & X86_ADDR_MASK;
    if (current_pdpt == 0) {
        PhysicalMemoryManager::free_frame(root);
        PhysicalMemoryManager::free_frame(pdpt);
        return false;
    }
    auto* pdpt_table = reinterpret_cast<uint64_t*>(pdpt);
    auto* source_pdpt = x86_table(current_pdpt);
    for (uint32_t i = 0; i < 512; ++i) pdpt_table[i] = source_pdpt[i];
    root_table[0] = pdpt | (current_table[0] & 0xfffull);
    space->root = root;
    space->valid = true;
    return true;
#else
    space->root = 0;
    space->valid = false;
    return false;
#endif
}

bool VirtualMemoryManager::map_page(AddressSpace* space, uintptr_t virt_addr,
                                    uintptr_t phys_addr, uint32_t flags) {
#if defined(__x86_64__)
    if (space == nullptr || !space->valid || !(flags & PAGE_PRESENT)) return false;
    virt_addr &= ~(PAGE_SIZE - 1);
    phys_addr &= ~(PAGE_SIZE - 1);
    if (!canonical_address(virt_addr) || ((flags & PAGE_USER) && !user_address(virt_addr)) || phys_addr == 0) return false;
    auto* root = x86_table(space->root);
    const uint32_t indexes[4] = {
        static_cast<uint32_t>((virt_addr >> 39) & 0x1ff),
        static_cast<uint32_t>((virt_addr >> 30) & 0x1ff),
        static_cast<uint32_t>((virt_addr >> 21) & 0x1ff),
        static_cast<uint32_t>((virt_addr >> 12) & 0x1ff)};
    uint64_t* table = root;
    uintptr_t allocated[3] = {0, 0, 0};
    uint64_t* allocated_slots[3] = {nullptr, nullptr, nullptr};
    uint32_t allocated_count = 0;
    auto rollback_tables = [&]() {
        for (uint32_t i = 0; i < allocated_count; ++i) {
            if (allocated_slots[i] != nullptr) *allocated_slots[i] = 0;
            PhysicalMemoryManager::free_frame(allocated[i]);
        }
    };
    for (int level = 0; level < 3; ++level) {
        uint64_t& entry = table[indexes[level]];
        if (!(entry & X86_PRESENT)) {
            const uintptr_t child = alloc_table();
            if (child == 0) {
                rollback_tables();
                return false;
            }
            allocated[allocated_count++] = child;
            allocated_slots[allocated_count - 1] = &entry;
            entry = child | table_flags(flags);
        }
        if (entry & X86_HUGE) {
            rollback_tables();
            return false;
        }
        table = x86_table(entry);
    }
    if (table[indexes[3]] & X86_PRESENT) {
        rollback_tables();
        return false;
    }
    table[indexes[3]] = phys_addr | leaf_flags(flags);
    return true;
#else
    (void)space; (void)virt_addr; (void)phys_addr; (void)flags;
    return false;
#endif
}

bool VirtualMemoryManager::unmap_page(AddressSpace* space, uintptr_t virt_addr) {
#if defined(__x86_64__)
    if (space == nullptr || !space->valid) return false;
    virt_addr &= ~(PAGE_SIZE - 1);
    if (!canonical_address(virt_addr)) return false;
    auto* table = x86_table(space->root);
    const uint32_t indexes[4] = {
        static_cast<uint32_t>((virt_addr >> 39) & 0x1ff),
        static_cast<uint32_t>((virt_addr >> 30) & 0x1ff),
        static_cast<uint32_t>((virt_addr >> 21) & 0x1ff),
        static_cast<uint32_t>((virt_addr >> 12) & 0x1ff)};
    for (int level = 0; level < 3; ++level) {
        const uint64_t entry = table[indexes[level]];
        if (!(entry & X86_PRESENT) || (entry & X86_HUGE)) return false;
        table = x86_table(entry);
    }
    table[indexes[3]] = 0;
    if ((space->root & X86_ADDR_MASK) == (current_pml4_or_ttbr & X86_ADDR_MASK)) {
        asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    }
    return true;
#else
    (void)space; (void)virt_addr;
    return false;
#endif
}

uintptr_t VirtualMemoryManager::get_physical_address(const AddressSpace* space, uintptr_t virt_addr) {
#if defined(__x86_64__)
    if (space == nullptr || !space->valid) return 0;
    if (!canonical_address(virt_addr)) return 0;
    const uintptr_t page = virt_addr & ~(PAGE_SIZE - 1);
    auto* table = x86_table(space->root);
    const uint32_t indexes[4] = {
        static_cast<uint32_t>((page >> 39) & 0x1ff),
        static_cast<uint32_t>((page >> 30) & 0x1ff),
        static_cast<uint32_t>((page >> 21) & 0x1ff),
        static_cast<uint32_t>((page >> 12) & 0x1ff)};
    for (int level = 0; level < 4; ++level) {
        const uint64_t entry = table[indexes[level]];
        if (!(entry & X86_PRESENT)) return 0;
        if (level == 2 && (entry & X86_HUGE)) return (entry & 0x000ffffffe00000ull) | (virt_addr & 0x1fffff);
        if (level == 3) return (entry & X86_ADDR_MASK) | (virt_addr & (PAGE_SIZE - 1));
        table = x86_table(entry);
    }
#else
    (void)space; (void)virt_addr;
#endif
    return 0;
}

bool VirtualMemoryManager::activate(const AddressSpace* space) {
#if defined(__x86_64__)
    if (space == nullptr || !space->valid) return false;
    current_pml4_or_ttbr = space->root;
    asm volatile("mov %0, %%cr3" : : "r"(space->root) : "memory");
    return true;
#else
    (void)space;
    return false;
#endif
}

} // namespace memory
