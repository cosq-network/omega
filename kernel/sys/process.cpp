#include "kernel/process.hpp"
#include "kernel/heap.hpp"
#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"
#include "kernel/scheduler.hpp"

namespace process {

namespace {
// Keep user mappings in PML4 slots separate from the low kernel identity
// map.  This allows a new address space to retain all kernel/device mappings
// while dropping inherited user mappings.
#if defined(__riscv)
constexpr uintptr_t USER_MMAP_BASE = 0x0000000040000000ull;
constexpr uintptr_t USER_MMAP_LIMIT = 0x0000000070000000ull;
#elif defined(__aarch64__)
constexpr uintptr_t USER_MMAP_BASE = 0x0000004000000000ull;
constexpr uintptr_t USER_MMAP_LIMIT = 0x0000007000000000ull;
#else
constexpr uintptr_t USER_MMAP_BASE = 0x0000400000000000ull;
constexpr uintptr_t USER_MMAP_LIMIT = 0x0000700000000000ull;
#endif
constexpr uint32_t PROT_WRITE = 2;
constexpr uint32_t PROT_EXEC = 4;
[[maybe_unused]] constexpr uint32_t PROT_READ = 1;
constexpr uint32_t MAP_FIXED = 0x10;
constexpr int64_t ERR_EINVAL = 22;
constexpr int64_t ERR_ENOMEM = 12;
constexpr int64_t ERR_EEXIST = 17;

Process* current_process = nullptr;
pid_t next_pid = 1;

static uintptr_t align_up(uintptr_t value) {
    if (value > ~(static_cast<uintptr_t>(0)) - (memory::PAGE_SIZE - 1)) return 0;
    return (value + memory::PAGE_SIZE - 1) & ~(memory::PAGE_SIZE - 1);
}

static uint32_t page_flags(uint32_t prot) {
    uint32_t flags = memory::PAGE_PRESENT | memory::PAGE_USER;
    if (prot & PROT_WRITE) flags |= memory::PAGE_WRITABLE;
    if (prot & PROT_EXEC) flags |= memory::PAGE_EXEC;
    return flags;
}

static bool overlaps(const Process* process, uintptr_t address, size_t length) {
    if (length == 0 || address > ~(static_cast<uintptr_t>(0)) - length) return true;
    const uintptr_t end = address + length;
    for (uint32_t i = 0; i < process->mapping_count; ++i) {
        if (process->mappings[i].address > ~(static_cast<uintptr_t>(0)) - process->mappings[i].length) return true;
        const uintptr_t map_end = process->mappings[i].address + process->mappings[i].length;
        if (address < map_end && end > process->mappings[i].address) return true;
    }
    return false;
}

static bool add_mapping(Process* process, uintptr_t address, size_t length) {
    if (process->mapping_count >= 32) return false;
    process->mappings[process->mapping_count++] = {address, length, false};
    return true;
}
}

void Manager::init() {
    if (current_process != nullptr) return;
    current_process = create();
    if (current_process != nullptr) {
        security::Manager::bind(&current_process->credentials);
        (void)memory::VirtualMemoryManager::activate(&current_process->address_space);
        kernel::kprintf("[+] Linux-compatible process/address-space manager initialized (PID %d).\n",
                        current_process->pid);
    } else {
        kernel::kprintf("[!] Process/address-space manager unavailable on this architecture.\n");
    }
}

Process* Manager::current() { return current_process; }

uintptr_t Manager::tls_base() {
    return current_process != nullptr ? current_process->tls_base : 0;
}

Process* Manager::create() {
    auto* process = reinterpret_cast<Process*>(kmalloc(sizeof(Process)));
    if (process == nullptr) return nullptr;
    process->pid = next_pid++;
    process->address_space = {0, false};
    process->next_mmap = USER_MMAP_BASE;
    // Keep the initial brk below the high anonymous-mmap window and close to
    // the user image, so musl mallocng's fixed guard pages use the same user
    // page-table range on every ISA.
    process->program_break = USER_MMAP_BASE - 0x01000000ull;
    process->alive = false;
    process->exited = false; process->exit_status = 0; process->parent = nullptr; process->child_count = 0;
    for (auto*& child : process->children) child = nullptr;
    process->credentials = security::Manager::current();
    for (auto& fd : process->fd_table) { fd.node = nullptr; fd.offset = 0; fd.flags = 0; }
    process->mapping_count = 0;
    process->user_entry = 0;
    process->user_stack = 0;
    process->tls_base = 0;
    process->cwd[0] = '/';
    process->cwd[1] = '\0';
    if (!memory::VirtualMemoryManager::create_address_space(&process->address_space)) {
        kfree(process);
        return nullptr;
    }
    process->alive = true;
    return process;
}

int64_t Manager::mmap(uintptr_t address, size_t length, uint32_t prot,
                      uint32_t flags, int32_t fd, uint64_t offset) {
    (void)offset;
    if (current_process == nullptr || length == 0) return -ERR_EINVAL;
    if (!(flags & 0x20) && fd < 0) return -ERR_EINVAL; // MAP_ANONYMOUS
    length = align_up(length);
    if (length == 0 || length > USER_MMAP_LIMIT - USER_MMAP_BASE) return -ERR_EINVAL;
    kernel::kprintf("[mmap] addr=%x len=%x prot=%x flags=%x\n", address, length, prot, flags);

    if (!(flags & MAP_FIXED)) address = current_process->next_mmap;
    address = address & ~(memory::PAGE_SIZE - 1);
    // musl mallocng uses a MAP_FIXED guard page immediately above its brk
    // arena. Anonymous non-fixed mappings stay in the high per-process
    // window, but fixed user mappings are also valid in the low heap range.
    if ((flags & MAP_FIXED) == 0) {
        if (address < USER_MMAP_BASE || address > USER_MMAP_LIMIT - length) return -ERR_EINVAL;
    } else if (address < 0x100000ull || address > USER_MMAP_LIMIT - length) {
        return -ERR_EINVAL;
    }
    if (overlaps(current_process, address, length)) return -ERR_EEXIST;

    size_t mapped = 0;
    for (; mapped < length; mapped += memory::PAGE_SIZE) {
        const uintptr_t frame = memory::PhysicalMemoryManager::alloc_frame();
        if (frame == 0 || !memory::VirtualMemoryManager::map_page(
                &current_process->address_space, address + mapped, frame, page_flags(prot))) {
            for (size_t rollback = 0; rollback < mapped; rollback += memory::PAGE_SIZE) {
                const uintptr_t old_frame = memory::VirtualMemoryManager::get_physical_address(
                    &current_process->address_space, address + rollback);
                memory::VirtualMemoryManager::unmap_page(&current_process->address_space, address + rollback);
                if (old_frame != 0) memory::PhysicalMemoryManager::free_frame(old_frame);
            }
            if (frame != 0) memory::PhysicalMemoryManager::free_frame(frame);
            return -ERR_ENOMEM;
        }
        // Bring-up page tables are identity-mapped in the kernel, so clear
        // the physical frame through its kernel alias, not the new user VA.
        auto* page = reinterpret_cast<uint8_t*>(frame);
        for (size_t i = 0; i < memory::PAGE_SIZE; ++i) page[i] = 0;
    }

    if (!add_mapping(current_process, address, length)) {
        for (size_t rollback = 0; rollback < length; rollback += memory::PAGE_SIZE) {
            const uintptr_t old_frame = memory::VirtualMemoryManager::get_physical_address(
                &current_process->address_space, address + rollback);
            memory::VirtualMemoryManager::unmap_page(&current_process->address_space, address + rollback);
            if (old_frame != 0) memory::PhysicalMemoryManager::free_frame(old_frame);
        }
        return -ERR_ENOMEM;
    }
    if (address + length > current_process->next_mmap) current_process->next_mmap = address + length;
    return static_cast<int64_t>(address);
}

int64_t Manager::munmap(uintptr_t address, size_t length) {
    if (current_process == nullptr || length == 0 || (address & (memory::PAGE_SIZE - 1))) return -ERR_EINVAL;
    length = align_up(length);
    for (uint32_t i = 0; i < current_process->mapping_count; ++i) {
        Mapping& mapping = current_process->mappings[i];
        if (mapping.address == address && mapping.length == length) {
            for (size_t offset = 0; offset < length; offset += memory::PAGE_SIZE) {
                const uintptr_t frame = memory::VirtualMemoryManager::get_physical_address(
                    &current_process->address_space, address + offset);
                memory::VirtualMemoryManager::unmap_page(&current_process->address_space, address + offset);
                if (frame != 0) memory::PhysicalMemoryManager::free_frame(frame);
            }
            current_process->mappings[i] = current_process->mappings[--current_process->mapping_count];
            return 0;
        }
    }
    return -ERR_EINVAL;
}

int64_t Manager::brk(uintptr_t address) {
    if (current_process == nullptr) return -ERR_ENOMEM;
    if (address == 0) return static_cast<int64_t>(current_process->program_break);
    if (address < 0x100000ull || address >= USER_MMAP_BASE) return -ERR_EINVAL;
    const uintptr_t old_break = current_process->program_break;
    const uintptr_t old_page_end = align_up(old_break);
    const uintptr_t new_page_end = align_up(address);
    if (new_page_end > old_page_end) {
        // Grow: map fresh zeroed pages from old_page_end up to new_page_end.
        for (uintptr_t va = old_page_end; va < new_page_end; va += memory::PAGE_SIZE) {
            const uintptr_t frame = memory::PhysicalMemoryManager::alloc_frame();
            if (frame == 0) return -ERR_ENOMEM;
            auto* page = reinterpret_cast<uint8_t*>(frame);
            for (size_t i = 0; i < memory::PAGE_SIZE; ++i) page[i] = 0;
            if (!memory::VirtualMemoryManager::map_page(&current_process->address_space, va, frame,
                                                        memory::PAGE_PRESENT | memory::PAGE_USER | memory::PAGE_WRITABLE)) {
                memory::PhysicalMemoryManager::free_frame(frame);
                return -ERR_ENOMEM;
            }
            if (!add_mapping(current_process, va, memory::PAGE_SIZE)) return -ERR_ENOMEM;
        }
    } else if (new_page_end < old_page_end) {
        // Shrink: unmap pages that fall outside the new break.
        for (uintptr_t va = new_page_end; va < old_page_end; va += memory::PAGE_SIZE) {
            for (uint32_t i = 0; i < current_process->mapping_count; ++i) {
                if (current_process->mappings[i].address == va &&
                    current_process->mappings[i].length == memory::PAGE_SIZE) {
                    const uintptr_t frame = memory::VirtualMemoryManager::get_physical_address(
                        &current_process->address_space, va);
                    memory::VirtualMemoryManager::unmap_page(&current_process->address_space, va);
                    if (frame != 0) memory::PhysicalMemoryManager::free_frame(frame);
                    current_process->mappings[i] =
                        current_process->mappings[--current_process->mapping_count];
                    break;
                }
            }
        }
    }
    current_process->program_break = address;
    return static_cast<int64_t>(address);
}

int64_t Manager::fork() {
    if (!current_process) return -ERR_ENOMEM;
    Process* parent=current_process; Process* child=create();
    if (!child || parent->child_count>=8) return -ERR_ENOMEM;
    child->parent=parent;
    for (size_t i = 0; i < sizeof(child->cwd); ++i) child->cwd[i] = parent->cwd[i];
    // Children inherit the parent's open file descriptors.
    for (uint32_t i = 0; i < FD_TABLE_SIZE; ++i) child->fd_table[i] = parent->fd_table[i];
    for(uint32_t i=0;i<parent->mapping_count;++i){
        const Mapping&m=parent->mappings[i];
        for(size_t off=0;off<m.length;off+=memory::PAGE_SIZE){
            const uintptr_t va=m.address+off,phys=memory::VirtualMemoryManager::get_physical_address(&parent->address_space,va);
            if(!phys)return -ERR_ENOMEM;
            uint32_t flags=memory::VirtualMemoryManager::get_page_flags(&parent->address_space,va);
            if(flags&memory::PAGE_WRITABLE){
                flags&=~memory::PAGE_WRITABLE;
                if(!memory::VirtualMemoryManager::set_page_flags(&parent->address_space,va,flags)) return -ERR_ENOMEM;
            }
            memory::PhysicalMemoryManager::retain_frame(phys);
            if(!memory::VirtualMemoryManager::map_page(&child->address_space,va,phys,flags))return -ERR_ENOMEM;
        }
        child->mappings[child->mapping_count++]={m.address,m.length,true};parent->mappings[i].cow=true;
    }
    parent->children[parent->child_count++]=child; return child->pid;
}

int64_t Manager::exit(int32_t status) {
    if (!current_process) return -ERR_EINVAL;
    current_process->alive=false; current_process->exited=true; current_process->exit_status=status;
    kernel::kprintf("[+] PID %d exited with status %d\n", current_process->pid,status); return 0;
}

int64_t Manager::wait4(pid_t pid, int32_t* status) {
    if(!current_process)return -ERR_EINVAL;
    for(uint32_t i=0;i<current_process->child_count;++i){Process*child=current_process->children[i];if(child&&child->exited&&(pid==-1||pid==child->pid)){if(status)*status=child->exit_status;pid_t result=child->pid;release_mappings(child);current_process->children[i]=current_process->children[--current_process->child_count];kfree(child);return result;}}
    return -11;
}

void Manager::release_mappings(Process* process) {
    if (process == nullptr) return;
    for (uint32_t i = 0; i < process->mapping_count; ++i) {
        const Mapping& mapping = process->mappings[i];
        for (size_t offset = 0; offset < mapping.length; offset += memory::PAGE_SIZE) {
            const uintptr_t va = mapping.address + offset;
            const uintptr_t frame = memory::VirtualMemoryManager::get_physical_address(
                &process->address_space, va);
            memory::VirtualMemoryManager::unmap_page(&process->address_space, va);
            if (frame != 0) memory::PhysicalMemoryManager::free_frame(frame);
        }
    }
    process->mapping_count = 0;
}

bool Manager::handle_cow_fault(uintptr_t address) {
    if (!current_process) return false;
    address &= ~(memory::PAGE_SIZE - 1);
    for (uint32_t i = 0; i < current_process->mapping_count; ++i) {
        Mapping& mapping = current_process->mappings[i];
        if (!mapping.cow || address < mapping.address || address >= mapping.address + mapping.length) continue;
        const uintptr_t old = memory::VirtualMemoryManager::get_physical_address(
            &current_process->address_space, address);
        const uintptr_t fresh = memory::PhysicalMemoryManager::alloc_frame();
        if (!old || !fresh) return false;
        auto* destination = reinterpret_cast<uint8_t*>(fresh);
        auto* source = reinterpret_cast<const uint8_t*>(old);
        for (size_t j = 0; j < memory::PAGE_SIZE; ++j) destination[j] = source[j];
        const uint32_t flags = memory::VirtualMemoryManager::get_page_flags(
            &current_process->address_space, address);
        if (!memory::VirtualMemoryManager::unmap_page(&current_process->address_space, address)) return false;
        if (!memory::VirtualMemoryManager::map_page(&current_process->address_space, address,
                                                     fresh, flags | memory::PAGE_WRITABLE)) return false;
        memory::PhysicalMemoryManager::free_frame(old);
        return true;
    }
    return false;
}

int64_t Manager::self_test() {
    if (current_process == nullptr) return -ERR_ENOMEM;
    Process* first_process = current_process;
    const int64_t first = mmap(0, memory::PAGE_SIZE, PROT_READ | PROT_WRITE, 0x22, -1, 0);
    if (first < 0) return first;
    const uintptr_t first_phys = memory::VirtualMemoryManager::get_physical_address(
        &first_process->address_space, static_cast<uintptr_t>(first));
    Process* second_process = create();
    if (second_process == nullptr || first_phys == 0) {
        return -ERR_ENOMEM;
    }
    current_process = second_process;
    const int64_t second = mmap(static_cast<uintptr_t>(first), memory::PAGE_SIZE,
                                PROT_READ | PROT_WRITE, 0x32, -1, 0);
    const uintptr_t second_phys = second < 0 ? 0 : memory::VirtualMemoryManager::get_physical_address(
        &second_process->address_space, static_cast<uintptr_t>(second));
    const bool isolated = second >= 0 && second_phys != 0 && second_phys != first_phys;
    if (second >= 0) (void)munmap(static_cast<uintptr_t>(second), memory::PAGE_SIZE);
    current_process = first_process;
    const int64_t cleanup = munmap(static_cast<uintptr_t>(first), memory::PAGE_SIZE);
    if (cleanup != 0 || !isolated) {
        return -ERR_EINVAL;
    }
    kernel::kprintf("[TEST][PASS] Isolated process address-space map/unmap\n");
    kernel::kprintf("[frames] after self-test part 1: free=%u\n", memory::PhysicalMemoryManager::get_free_frames());
    const int64_t cow_address = mmap(0, memory::PAGE_SIZE, PROT_READ | PROT_WRITE, 0x22, -1, 0);
    if (cow_address < 0) return cow_address;
    const uintptr_t original = memory::VirtualMemoryManager::get_physical_address(
        &first_process->address_space, static_cast<uintptr_t>(cow_address));
    const int64_t child_pid = fork();
    if (child_pid < 0 || first_process->child_count == 0) return -ERR_ENOMEM;
    Process* child = first_process->children[first_process->child_count - 1];
    if (memory::VirtualMemoryManager::get_physical_address(&child->address_space,
            static_cast<uintptr_t>(cow_address)) != original ||
        !handle_cow_fault(static_cast<uintptr_t>(cow_address))) return -ERR_EINVAL;
    const uintptr_t private_copy = memory::VirtualMemoryManager::get_physical_address(
        &first_process->address_space, static_cast<uintptr_t>(cow_address));
    if (private_copy == 0 || private_copy == original) return -ERR_EINVAL;
    if (!activate(child) || munmap(static_cast<uintptr_t>(cow_address), memory::PAGE_SIZE) != 0) return -ERR_EINVAL;
    (void)exit(17);
    if (!activate(first_process)) return -ERR_EINVAL;
    int32_t status = 0;
    if (wait4(child_pid, &status) != child_pid || status != 17 ||
        munmap(static_cast<uintptr_t>(cow_address), memory::PAGE_SIZE) != 0) return -ERR_EINVAL;
    kernel::kprintf("[TEST][PASS] COW fork, write fault, exit, and wait/reap\n");
    const int64_t protected_address = mmap(0, memory::PAGE_SIZE, PROT_READ, 0x22, -1, 0);
    if (protected_address < 0) return protected_address;
    const uintptr_t protected_va = static_cast<uintptr_t>(protected_address);
    const uint32_t read_only_flags = memory::VirtualMemoryManager::get_page_flags(
        &first_process->address_space, protected_va);
    if ((read_only_flags & (memory::PAGE_WRITABLE | memory::PAGE_EXEC)) != 0 ||
        !memory::VirtualMemoryManager::set_page_flags(
            &first_process->address_space, protected_va,
            memory::PAGE_PRESENT | memory::PAGE_USER | memory::PAGE_EXEC) ||
        !(memory::VirtualMemoryManager::get_page_flags(
            &first_process->address_space, protected_va) & memory::PAGE_EXEC) ||
        munmap(protected_va, memory::PAGE_SIZE) != 0) return -ERR_EINVAL;
    kernel::kprintf("[TEST][PASS] Read/write/execute page protection matrix\n");
    return 0;
}

bool Manager::activate(Process* process) {
    if (process == nullptr || !process->alive || !process->address_space.valid) return false;
    current_process = process;
    security::Manager::bind(&process->credentials);
    return memory::VirtualMemoryManager::activate(&process->address_space);
}

} // namespace process
