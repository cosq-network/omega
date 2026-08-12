#include "kernel/vmm.hpp"
#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"

namespace memory {

uintptr_t VirtualMemoryManager::current_pml4_or_ttbr = 0;

namespace {

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
    // Preserve the low identity window and the QEMU virt kernel window. The
    // latter is linked at 0x40080000, so it needs the 0x40000000 L1 entry.
    arm_l1[0][0] = arm_attrs(PAGE_PRESENT | PAGE_WRITABLE | PAGE_EXEC);
    arm_l1[0][1] = 0x40000000ull | arm_attrs(PAGE_PRESENT | PAGE_WRITABLE | PAGE_EXEC);
    for (uint32_t block = 2; block < 512; ++block) arm_l1[0][block] = 0;
}
#endif

#if defined(__riscv)
alignas(4096) static uint64_t rv_root[512];
static constexpr uint64_t RV_V = 1ull;
static constexpr uint64_t RV_R = 1ull << 1;
static constexpr uint64_t RV_W = 1ull << 2;
static constexpr uint64_t RV_X = 1ull << 3;
static constexpr uint64_t RV_U = 1ull << 4;
static constexpr uint64_t RV_A = 1ull << 6;
static constexpr uint64_t RV_D = 1ull << 7;

static uint64_t rv_flags(uint32_t flags) {
    uint64_t pte = RV_V | RV_R | RV_A | RV_D;
    if (flags & PAGE_WRITABLE) pte |= RV_W;
    if (flags & PAGE_EXEC) pte |= RV_X;
    if (flags & PAGE_USER) pte |= RV_U;
    return pte;
}

static void rv_setup() {
    for (uint32_t i = 0; i < 512; ++i) rv_root[i] = 0;
    // Sv39 root entries are 1 GiB leaves. Preserve the low identity window
    // and the kernel's 0x80200000 image while reserving root index 1 for
    // user mappings.
    rv_root[0] = rv_flags(PAGE_PRESENT | PAGE_WRITABLE | PAGE_EXEC);
    rv_root[2] = (0x80000000ull >> 12) << 10 | rv_flags(PAGE_PRESENT | PAGE_WRITABLE | PAGE_EXEC);
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

#if defined(__aarch64__)
static uintptr_t arm_alloc_table() {
    const uintptr_t frame = PhysicalMemoryManager::alloc_frame();
    if (!frame) return 0;
    auto* table = reinterpret_cast<uint64_t*>(frame);
    for (uint32_t i = 0; i < 512; ++i) table[i] = 0;
    return frame;
}

static uint64_t arm_table_desc(uintptr_t address) { return address | ARM_VALID | ARM_TABLE; }
static uint64_t arm_leaf_attrs(uint32_t flags) {
    uint64_t attrs = arm_attrs(flags) | ARM_TABLE;
    // AP=01: EL0 read/write; AP=11: EL0 read-only.
    attrs &= ~(3ull << 6);
    attrs |= (flags & PAGE_WRITABLE) ? (1ull << 6) : (3ull << 6);
    return attrs;
}
static constexpr uint64_t ARM_PHYS_MASK = 0x0000fffffffff000ull;
#endif

#if defined(__riscv)
static uintptr_t rv_alloc_table() {
    const uintptr_t frame = PhysicalMemoryManager::alloc_frame();
    if (!frame) return 0;
    auto* table = reinterpret_cast<uint64_t*>(frame);
    for (uint32_t i = 0; i < 512; ++i) table[i] = 0;
    return frame;
}
static uint64_t rv_table_desc(uintptr_t address) { return (address >> 12) << 10 | RV_V; }
#endif
}

void VirtualMemoryManager::init() {
#if defined(__x86_64__)
    asm volatile("mov %%cr3, %0" : "=r"(current_pml4_or_ttbr));
#elif defined(__aarch64__)
    arm_setup();
    // Use the 4 KiB-granule, 39-bit TTBR0 regime used by the per-process
    // L0/L1/L2/L3 tables below. Normal memory is cacheable; device mappings
    // use the second MAIR attribute.
    const uint64_t mair = 0x00000000000004ffull;
    const uint64_t tcr = 16ull | (1ull << 8) | (1ull << 10) | (3ull << 12);
    asm volatile("msr mair_el1, %0; msr tcr_el1, %1; dsb sy; isb" : : "r"(mair), "r"(tcr) : "memory");
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
    AddressSpace current{current_pml4_or_ttbr, true};
    return map_page(&current, virt_addr, phys_addr, flags);
#elif defined(__riscv)
    AddressSpace current{current_pml4_or_ttbr, true};
    return map_page(&current, virt_addr, phys_addr, flags);
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
    AddressSpace current{current_pml4_or_ttbr, true};
    return unmap_page(&current, virt_addr);
#endif
}

uintptr_t VirtualMemoryManager::get_physical_address(uintptr_t virt_addr) {
#if defined(__x86_64__)
    AddressSpace current{current_pml4_or_ttbr & X86_ADDR_MASK, true};
    return get_physical_address(&current, virt_addr);
#else
    AddressSpace current{current_pml4_or_ttbr, true};
    return get_physical_address(&current, virt_addr);
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
#if defined(__aarch64__)
    const uintptr_t root = arm_alloc_table();
    const uintptr_t l1 = arm_alloc_table();
    if (!root || !l1) return false;
    auto* dst = reinterpret_cast<uint64_t*>(root);
    auto* src = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(&arm_l0[0]));
    for (uint32_t i = 0; i < 512; ++i) dst[i] = src[i];
    auto* dst_l1 = reinterpret_cast<uint64_t*>(l1);
    auto* src_l1 = reinterpret_cast<uint64_t*>(src[0] & ~0xfffull);
    for (uint32_t i = 0; i < 512; ++i) dst_l1[i] = src_l1[i];
    dst[0] = arm_table_desc(l1);
    space->root = root; space->valid = true; return true;
#elif defined(__riscv)
    const uintptr_t root = rv_alloc_table();
    if (!root) return false;
    auto* dst = reinterpret_cast<uint64_t*>(root);
    for (uint32_t i = 0; i < 512; ++i) dst[i] = rv_root[i];
    space->root = root; space->valid = true; return true;
#else
    space->root = 0; space->valid = false; return false;
#endif
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
    if ((space->root & X86_ADDR_MASK) == (current_pml4_or_ttbr & X86_ADDR_MASK)) {
        asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    }
    return true;
#else
#if defined(__aarch64__)
    if (!space || !space->valid || !(flags & PAGE_PRESENT) || phys_addr == 0) return false;
    virt_addr &= ~(PAGE_SIZE - 1); phys_addr &= ~(PAGE_SIZE - 1);
    auto* l0 = reinterpret_cast<uint64_t*>(space->root);
    const uint32_t i0 = (virt_addr >> 39) & 0x1ff, i1 = (virt_addr >> 30) & 0x1ff,
                   i2 = (virt_addr >> 21) & 0x1ff, i3 = (virt_addr >> 12) & 0x1ff;
    uint64_t* l1;
    if (!(l0[i0] & ARM_VALID)) { const uintptr_t p = arm_alloc_table(); if (!p) return false; l0[i0] = arm_table_desc(p); }
    l1 = reinterpret_cast<uint64_t*>(l0[i0] & ~0x3ull);
    uint64_t* l2;
    if (!(l1[i1] & ARM_VALID)) { const uintptr_t p = arm_alloc_table(); if (!p) return false; l1[i1] = arm_table_desc(p); }
    if (!(l1[i1] & ARM_TABLE)) return false;
    l2 = reinterpret_cast<uint64_t*>(l1[i1] & ~0x3ull);
    uint64_t* l3;
    if (!(l2[i2] & ARM_VALID)) { const uintptr_t p = arm_alloc_table(); if (!p) return false; l2[i2] = arm_table_desc(p); }
    if (!(l2[i2] & ARM_TABLE)) return false;
    l3 = reinterpret_cast<uint64_t*>(l2[i2] & ~0x3ull);
    if (l3[i3] & ARM_VALID) return false;
    l3[i3] = phys_addr | arm_leaf_attrs(flags);
    if (space->root == current_pml4_or_ttbr) {
        asm volatile("dsb ish; tlbi vae1is, %0; dsb ish; isb" : : "r"(virt_addr >> 12) : "memory");
    }
    return true;
#elif defined(__riscv)
    if (!space || !space->valid || !(flags & PAGE_PRESENT) || phys_addr == 0) return false;
    virt_addr &= ~(PAGE_SIZE - 1); phys_addr &= ~(PAGE_SIZE - 1);
    auto* l2 = reinterpret_cast<uint64_t*>(space->root);
    const uint32_t i2 = (virt_addr >> 30) & 0x1ff, i1 = (virt_addr >> 21) & 0x1ff, i0 = (virt_addr >> 12) & 0x1ff;
    if (l2[i2] & (RV_R | RV_W | RV_X)) return false;
    if (!(l2[i2] & RV_V)) { const uintptr_t p = rv_alloc_table(); if (!p) return false; l2[i2] = rv_table_desc(p); }
    auto* l1 = reinterpret_cast<uint64_t*>((l2[i2] >> 10) << 12);
    if (l1[i1] & (RV_R | RV_W | RV_X)) return false;
    if (!(l1[i1] & RV_V)) { const uintptr_t p = rv_alloc_table(); if (!p) return false; l1[i1] = rv_table_desc(p); }
    auto* l0 = reinterpret_cast<uint64_t*>((l1[i1] >> 10) << 12);
    if (l0[i0] & RV_V) return false;
    l0[i0] = (phys_addr >> 12) << 10 | rv_flags(flags);
    asm volatile("sfence.vma %0" : : "r"(virt_addr) : "memory");
    return true;
#else
    (void)space; (void)virt_addr; (void)phys_addr; (void)flags; return false;
#endif
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
#if defined(__aarch64__)
    if (!space || !space->valid) return false; virt_addr &= ~(PAGE_SIZE - 1);
    auto* l0 = reinterpret_cast<uint64_t*>(space->root); uint32_t i0=(virt_addr>>39)&511,i1=(virt_addr>>30)&511,i2=(virt_addr>>21)&511,i3=(virt_addr>>12)&511;
    if (!(l0[i0]&ARM_TABLE)) return false; auto* l1=reinterpret_cast<uint64_t*>(l0[i0]&~3ull);
    if (!(l1[i1]&ARM_TABLE)) return false; auto* l2=reinterpret_cast<uint64_t*>(l1[i1]&~3ull);
    if (!(l2[i2]&ARM_TABLE)) return false; auto* l3=reinterpret_cast<uint64_t*>(l2[i2]&~3ull);
    if (!(l3[i3]&ARM_VALID)) return false; l3[i3]=0; asm volatile("dsb ish; tlbi vae1is, %0; dsb ish; isb" : : "r"(virt_addr>>12) : "memory"); return true;
#elif defined(__riscv)
    if (!space || !space->valid) return false; virt_addr &= ~(PAGE_SIZE - 1);
    auto* l2=reinterpret_cast<uint64_t*>(space->root); uint32_t i2=(virt_addr>>30)&511,i1=(virt_addr>>21)&511,i0=(virt_addr>>12)&511;
    if (!(l2[i2]&RV_V) || (l2[i2]&(RV_R|RV_W|RV_X))) return false; auto* l1=reinterpret_cast<uint64_t*>((l2[i2]>>10)<<12);
    if (!(l1[i1]&RV_V) || (l1[i1]&(RV_R|RV_W|RV_X))) return false; auto* l0=reinterpret_cast<uint64_t*>((l1[i1]>>10)<<12);
    if (!(l0[i0]&RV_V)) return false; l0[i0]=0; asm volatile("sfence.vma %0" : : "r"(virt_addr) : "memory"); return true;
#else
    (void)space; (void)virt_addr; return false;
#endif
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
#if defined(__aarch64__)
    if (!space || !space->valid) return 0; auto page=virt_addr&~(PAGE_SIZE-1); auto* l0=reinterpret_cast<uint64_t*>(space->root); uint32_t i0=(page>>39)&511,i1=(page>>30)&511,i2=(page>>21)&511,i3=(page>>12)&511;
    if (!(l0[i0]&ARM_VALID)) return 0; auto* l1=reinterpret_cast<uint64_t*>(l0[i0]&~3ull); if (!(l1[i1]&ARM_VALID)) return 0; if (!(l1[i1]&ARM_TABLE)) return (l1[i1]&0x0000ffffc0000000ull)|(virt_addr&0x3fffffff);
    auto* l2=reinterpret_cast<uint64_t*>(l1[i1]&~3ull); if (!(l2[i2]&ARM_VALID)) return 0; if (!(l2[i2]&ARM_TABLE)) return (l2[i2]&0x0000ffffffe00000ull)|(virt_addr&0x1fffff); auto* l3=reinterpret_cast<uint64_t*>(l2[i2]&~3ull); return (l3[i3]&ARM_PHYS_MASK)|(virt_addr&0xfff);
#elif defined(__riscv)
    if (!space || !space->valid) return 0; auto page=virt_addr&~(PAGE_SIZE-1); auto* l2=reinterpret_cast<uint64_t*>(space->root); uint32_t i2=(page>>30)&511,i1=(page>>21)&511,i0=(page>>12)&511; if (!(l2[i2]&RV_V)) return 0; if (l2[i2]&(RV_R|RV_W|RV_X)) return ((l2[i2]>>10)<<12)|(virt_addr&0x3fffffff); auto* l1=reinterpret_cast<uint64_t*>((l2[i2]>>10)<<12); if (!(l1[i1]&RV_V)) return 0; if (l1[i1]&(RV_R|RV_W|RV_X)) return ((l1[i1]>>10)<<12)|(virt_addr&0x1fffff); auto* l0=reinterpret_cast<uint64_t*>((l1[i1]>>10)<<12); return (l0[i0]&~0x3ffull)<<2 | (virt_addr&0xfff);
#else
    (void)space; (void)virt_addr;
#endif
#endif
    return 0;
}

uint32_t VirtualMemoryManager::get_page_flags(const AddressSpace* space, uintptr_t virt_addr) {
#if defined(__x86_64__)
    if (!space || !space->valid) return 0;
    auto* table = x86_table(space->root);
    const uint32_t ix[4] = {static_cast<uint32_t>((virt_addr>>39)&511), static_cast<uint32_t>((virt_addr>>30)&511), static_cast<uint32_t>((virt_addr>>21)&511), static_cast<uint32_t>((virt_addr>>12)&511)};
    for (int level=0; level<3; ++level) { uint64_t e=table[ix[level]]; if (!(e&X86_PRESENT) || (e&X86_HUGE)) return 0; table=x86_table(e); }
    const uint64_t e=table[ix[3]]; if (!(e&X86_PRESENT)) return 0; uint32_t f=PAGE_PRESENT; if(e&X86_WRITABLE)f|=PAGE_WRITABLE; if(e&X86_USER)f|=PAGE_USER; if(!(e&X86_NX))f|=PAGE_EXEC; return f;
#elif defined(__aarch64__)
    if (!space || !space->valid) return 0;
    auto* l0 = reinterpret_cast<uint64_t*>(space->root);
    const uint32_t i0 = (virt_addr >> 39) & 511, i1 = (virt_addr >> 30) & 511,
                   i2 = (virt_addr >> 21) & 511, i3 = (virt_addr >> 12) & 511;
    if (!(l0[i0] & ARM_VALID)) return 0;
    auto* l1 = reinterpret_cast<uint64_t*>(l0[i0] & ~3ull);
    if (!(l1[i1] & ARM_VALID) || !(l1[i1] & ARM_TABLE)) return 0;
    auto* l2 = reinterpret_cast<uint64_t*>(l1[i1] & ~3ull);
    if (!(l2[i2] & ARM_VALID) || !(l2[i2] & ARM_TABLE)) return 0;
    auto* l3 = reinterpret_cast<uint64_t*>(l2[i2] & ~3ull);
    const uint64_t entry = l3[i3];
    if (!(entry & ARM_VALID)) return 0;
    uint32_t flags = PAGE_PRESENT | PAGE_USER;
    const uint64_t ap = (entry >> 6) & 3;
    if (ap == 1) flags |= PAGE_WRITABLE;
    if (!(entry & ARM_XN)) flags |= PAGE_EXEC;
    return flags;
#elif defined(__riscv)
    if (!space || !space->valid) return 0;
    auto* l2 = reinterpret_cast<uint64_t*>(space->root);
    const uint32_t i2 = (virt_addr >> 30) & 511, i1 = (virt_addr >> 21) & 511,
                   i0 = (virt_addr >> 12) & 511;
    if (!(l2[i2] & RV_V) || (l2[i2] & (RV_R | RV_W | RV_X))) return 0;
    auto* l1 = reinterpret_cast<uint64_t*>((l2[i2] >> 10) << 12);
    if (!(l1[i1] & RV_V) || (l1[i1] & (RV_R | RV_W | RV_X))) return 0;
    auto* l0 = reinterpret_cast<uint64_t*>((l1[i1] >> 10) << 12);
    const uint64_t entry = l0[i0];
    if (!(entry & RV_V)) return 0;
    uint32_t flags = PAGE_PRESENT;
    if (entry & RV_U) flags |= PAGE_USER;
    if (entry & RV_W) flags |= PAGE_WRITABLE;
    if (entry & RV_X) flags |= PAGE_EXEC;
    return flags;
#else
    (void)space; (void)virt_addr; return 0;
#endif
}

bool VirtualMemoryManager::set_page_flags(AddressSpace* space, uintptr_t virt_addr, uint32_t flags) {
#if defined(__x86_64__)
    if (!space || !space->valid) return false; auto* table=x86_table(space->root); const uint32_t ix[4]={static_cast<uint32_t>((virt_addr>>39)&511),static_cast<uint32_t>((virt_addr>>30)&511),static_cast<uint32_t>((virt_addr>>21)&511),static_cast<uint32_t>((virt_addr>>12)&511)};
    for(int level=0;level<3;++level){uint64_t e=table[ix[level]];if(!(e&X86_PRESENT)||(e&X86_HUGE))return false;table=x86_table(e);} uint64_t& e=table[ix[3]];if(!(e&X86_PRESENT))return false; const uintptr_t phys=e&X86_ADDR_MASK; e=phys|leaf_flags(flags); if((space->root&X86_ADDR_MASK)==(current_pml4_or_ttbr&X86_ADDR_MASK))asm volatile("invlpg (%0)"::"r"(virt_addr):"memory"); return true;
#elif defined(__aarch64__)
    if (!space || !space->valid) return false;
    auto* l0 = reinterpret_cast<uint64_t*>(space->root);
    const uint32_t i0 = (virt_addr >> 39) & 511, i1 = (virt_addr >> 30) & 511,
                   i2 = (virt_addr >> 21) & 511, i3 = (virt_addr >> 12) & 511;
    if (!(l0[i0] & ARM_VALID)) return false;
    auto* l1 = reinterpret_cast<uint64_t*>(l0[i0] & ~3ull);
    if (!(l1[i1] & ARM_VALID) || !(l1[i1] & ARM_TABLE)) return false;
    auto* l2 = reinterpret_cast<uint64_t*>(l1[i1] & ~3ull);
    if (!(l2[i2] & ARM_VALID) || !(l2[i2] & ARM_TABLE)) return false;
    auto* l3 = reinterpret_cast<uint64_t*>(l2[i2] & ~3ull);
    if (!(l3[i3] & ARM_VALID)) return false;
    l3[i3] = (l3[i3] & ~((3ull << 6) | ARM_XN)) | arm_leaf_attrs(flags);
    asm volatile("dsb ish; tlbi vae1is, %0; dsb ish; isb" : : "r"(virt_addr >> 12) : "memory");
    return true;
#elif defined(__riscv)
    if (!space || !space->valid) return false;
    auto* l2 = reinterpret_cast<uint64_t*>(space->root);
    const uint32_t i2 = (virt_addr >> 30) & 511, i1 = (virt_addr >> 21) & 511,
                   i0 = (virt_addr >> 12) & 511;
    if (!(l2[i2] & RV_V) || (l2[i2] & (RV_R | RV_W | RV_X))) return false;
    auto* l1 = reinterpret_cast<uint64_t*>((l2[i2] >> 10) << 12);
    if (!(l1[i1] & RV_V) || (l1[i1] & (RV_R | RV_W | RV_X))) return false;
    auto* l0 = reinterpret_cast<uint64_t*>((l1[i1] >> 10) << 12);
    if (!(l0[i0] & RV_V)) return false;
    l0[i0] = (l0[i0] & ~((RV_R | RV_W | RV_X | RV_U) | RV_A | RV_D)) | rv_flags(flags);
    asm volatile("sfence.vma %0" : : "r"(virt_addr) : "memory");
    return true;
#else
    (void)space; (void)virt_addr; (void)flags; return false;
#endif
}

bool VirtualMemoryManager::activate(const AddressSpace* space) {
#if defined(__x86_64__)
    if (space == nullptr || !space->valid) return false;
    current_pml4_or_ttbr = space->root;
    asm volatile("mov %0, %%cr3" : : "r"(space->root) : "memory");
    return true;
#else
#if defined(__aarch64__)
    if (!space || !space->valid) return false;
    current_pml4_or_ttbr = space->root;
    asm volatile(
        "msr ttbr0_el1, %0\n"
        "dsb ish\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n"
        "mrs x0, sctlr_el1\n"
        "mov x1, #0x1005\n"
        "orr x0, x0, x1\n"
        "msr sctlr_el1, x0\n"
        "isb\n"
        : : "r"(space->root) : "x0", "x1", "memory");
    return true;
#elif defined(__riscv)
    if (!space || !space->valid) return false; current_pml4_or_ttbr=(8ull<<60)|(space->root>>12); asm volatile("csrw satp, %0; sfence.vma" : : "r"(current_pml4_or_ttbr) : "memory"); return true;
#else
    (void)space; return false;
#endif
#endif
}

} // namespace memory
