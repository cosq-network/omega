#include "kernel/elf_loader.hpp"
#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"
#include "kernel/process.hpp"
#include "kernel/heap.hpp"

namespace elf {

namespace {
constexpr uint8_t ELF_CLASS_64 = 2;
constexpr uint8_t ELF_DATA_LSB = 1;
constexpr uint8_t ELF_VERSION_CURRENT = 1;
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_DYN = 3;
constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PT_DYNAMIC = 2;
constexpr uint32_t PT_INTERP = 3;
constexpr uint32_t PT_TLS = 7;
constexpr uint32_t PF_X = 1;
constexpr uint32_t PF_W = 2;
#if defined(__x86_64__)
constexpr uint16_t EM_TARGET = 62;
#elif defined(__aarch64__)
constexpr uint16_t EM_TARGET = 183;
#elif defined(__riscv)
constexpr uint16_t EM_TARGET = 243;
#endif

static bool add_overflows(uint64_t left, uint64_t right) {
    return right > ~static_cast<uint64_t>(0) - left;
}

static bool supported_machine(uint16_t machine) {
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    return machine == EM_TARGET;
#else
    (void)machine;
    return false;
#endif
}

static uintptr_t align_down(uintptr_t value) { return value & ~(memory::PAGE_SIZE - 1); }
static uintptr_t align_up(uintptr_t value) {
    if (value > ~static_cast<uintptr_t>(0) - (memory::PAGE_SIZE - 1)) return 0;
    return (value + memory::PAGE_SIZE - 1) & ~(memory::PAGE_SIZE - 1);
}
}

bool ElfLoader::validate(const uint8_t* elf_data) {
    if (!elf_data) return false;
    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(elf_data);
    // Check ELF Magic Bytes: 0x7F, 'E', 'L', 'F'
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
        return false;
    }
    return header->e_ident[4] == ELF_CLASS_64 && header->e_ident[5] == ELF_DATA_LSB &&
           header->e_ident[6] == ELF_VERSION_CURRENT && supported_machine(header->e_machine) &&
           (header->e_type == ET_EXEC || header->e_type == ET_DYN);
}

bool ElfLoader::validate(const uint8_t* elf_data, size_t image_size) {
    if (!elf_data || image_size < sizeof(Elf64Header)) return false;
    const auto* header = reinterpret_cast<const Elf64Header*>(elf_data);
    if (!validate(elf_data) || header->e_ehsize != sizeof(Elf64Header) ||
        header->e_phentsize < sizeof(Elf64ProgramHeader) || header->e_phnum == 0) return false;
    if (header->e_phnum > (image_size / header->e_phentsize) ||
        header->e_phoff > image_size ||
        static_cast<uint64_t>(header->e_phnum) * header->e_phentsize > image_size - header->e_phoff) return false;

    bool load_segment = false;
    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto* segment = reinterpret_cast<const Elf64ProgramHeader*>(
            elf_data + header->e_phoff + static_cast<size_t>(i) * header->e_phentsize);
        if (segment->p_type == PT_LOAD) {
            load_segment = true;
            if (segment->p_filesz > segment->p_memsz ||
                add_overflows(segment->p_offset, segment->p_filesz) ||
                segment->p_offset + segment->p_filesz > image_size ||
                add_overflows(segment->p_vaddr, segment->p_memsz)) return false;
            if (segment->p_align > 1 &&
                ((segment->p_align & (segment->p_align - 1)) != 0 ||
                 (segment->p_offset % segment->p_align) != (segment->p_vaddr % segment->p_align))) return false;
        }
        if (segment->p_type == PT_INTERP) {
            // The current Omega loader has no ELF interpreter/dynamic linker.
            return false;
        }
        if (segment->p_type == PT_DYNAMIC) {
            // No dynamic linker is installed yet, for either ET_EXEC or ET_DYN.
            return false;
        }
    }
    return load_segment;
}

uintptr_t ElfLoader::load(const uint8_t* elf_data, size_t image_size) {
    if (!validate(elf_data, image_size)) {
        kernel::kprintf("[!] Invalid ELF Binary Header Magic Bytes!\n");
        return 0;
    }

    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(elf_data);
    kernel::kprintf("[+] Valid 64-bit ELF Binary Detected.\n");
    kernel::kprintf("    ELF Entry Point: %x, Program Headers: %u\n", header->e_entry, header->e_phnum);

    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf64ProgramHeader*>(
            elf_data + header->e_phoff + static_cast<size_t>(i) * header->e_phentsize);
        if (ph->p_type == PT_LOAD) {
            kernel::kprintf("    --> PT_LOAD Segment [%u]: Virt %x, Memory Size: %u bytes\n",
                            i, ph->p_vaddr, ph->p_memsz);
        }
    }

    return header->e_entry;
}

bool ElfLoader::load_into(process::Process* process, const uint8_t* elf_data,
                           size_t image_size, uintptr_t* entry, uintptr_t* stack) {
    // Default: single-arg boot path (argc=1, argv={"init"}).
    static const char* init_argv[] = {"/init", nullptr};
    static const char* init_envp[] = {nullptr};
    return load_into(process, elf_data, image_size, entry, stack, 1, init_argv, init_envp);
}

bool ElfLoader::load_into(process::Process* process, const uint8_t* elf_data,
                           size_t image_size, uintptr_t* entry, uintptr_t* stack,
                           int argc, const char* const* argv, const char* const* envp) {
    if (process == nullptr || entry == nullptr || stack == nullptr ||
        !validate(elf_data, image_size)) return false;
    const auto* header = reinterpret_cast<const Elf64Header*>(elf_data);

    // Parse PT_TLS: allocate a single TLS page (static TLS model: one block
    // per process, laid out with the executable's TLS template copied in).
    uint32_t tls_filesz = 0, tls_memsz = 0, tls_align = 1;
    uintptr_t highest_load_end = 0;
    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf64ProgramHeader*>(
            elf_data + header->e_phoff + static_cast<size_t>(i) * header->e_phentsize);
        if (ph->p_type == PT_TLS) {
            tls_filesz = static_cast<uint32_t>(ph->p_filesz);
            tls_memsz = static_cast<uint32_t>(ph->p_memsz);
            tls_align = static_cast<uint32_t>(ph->p_align ? ph->p_align : 1);
            if (tls_memsz == 0) tls_memsz = tls_filesz;
        }
    }

    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf64ProgramHeader*>(
            elf_data + header->e_phoff + static_cast<size_t>(i) * header->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        if (ph->p_vaddr >= 0x0000800000000000ull || ph->p_vaddr + ph->p_memsz < ph->p_vaddr) return false;
        const uintptr_t first = align_down(static_cast<uintptr_t>(ph->p_vaddr));
        const uintptr_t last = align_up(static_cast<uintptr_t>(ph->p_vaddr + ph->p_memsz));
        if (last == 0 || last <= first) return false;
        if (last > highest_load_end) highest_load_end = last;
        uint32_t flags = memory::PAGE_PRESENT | memory::PAGE_USER;
        if (ph->p_flags & PF_W) flags |= memory::PAGE_WRITABLE;
        if (ph->p_flags & PF_X) flags |= memory::PAGE_EXEC;
        for (uintptr_t address = first; address < last; address += memory::PAGE_SIZE) {
            uintptr_t frame = memory::VirtualMemoryManager::get_physical_address(&process->address_space, address);
            const bool new_page = frame == 0;
            if (new_page) frame = memory::PhysicalMemoryManager::alloc_frame();
            if (frame == 0 || (new_page && !memory::VirtualMemoryManager::map_page(&process->address_space, address, frame, flags))) {
                kernel::kprintf("[!] ELF map failed at %x\n", address);
                return false;
            }
            // If the page was already mapped by an earlier segment (e.g. a
            // shared boundary page between RX text and RW data), upgrade its
            // flags to the union so writable data is actually writable.
            if (!new_page) {
                const uint32_t old_flags = memory::VirtualMemoryManager::get_page_flags(
                    &process->address_space, address);
                const uint32_t combined = old_flags | flags;
                if (!memory::VirtualMemoryManager::set_page_flags(&process->address_space, address, combined)) {
                    kernel::kprintf("[!] ELF flag upgrade failed at %x\n", address);
                    return false;
                }
            }
            auto* destination = reinterpret_cast<uint8_t*>(frame);
            if (new_page) for (size_t j = 0; j < memory::PAGE_SIZE; ++j) destination[j] = 0;
            const uint64_t page_start = address;
            const uint64_t page_end = address + memory::PAGE_SIZE;
            const uint64_t file_start = ph->p_vaddr;
            const uint64_t file_end = ph->p_vaddr + ph->p_filesz;
            const uint64_t copy_start = page_start > file_start ? page_start : file_start;
            const uint64_t copy_end = page_end < file_end ? page_end : file_end;
            for (uint64_t cursor = copy_start; cursor < copy_end; ++cursor)
                destination[cursor - page_start] = elf_data[ph->p_offset + cursor - ph->p_vaddr];
            if (new_page) {
                if (process->mapping_count >= 32) return false;
                process->mappings[process->mapping_count++] = {address, memory::PAGE_SIZE, false};
            }
        }
    }

    // Keep anonymous mmap allocations after the loaded image. The initial
    // mmap cursor is the image base for historical freestanding binaries;
    // musl malloc requires that cursor to advance past a static ELF image.
    if (highest_load_end > process->next_mmap) {
        process->next_mmap = highest_load_end + memory::PAGE_SIZE;
    }

    // Allocate and initialize the TLS block (static TLS: copy template from
    // the PT_TLS segment into a fresh page mapped in the user address space).
    if (tls_memsz != 0) {
        // Choose a fixed per-ISA TLS mapping address below the stack.
#if defined(__riscv)
        const uintptr_t tls_map_va = 0x000000006ffdd000ull;
#elif defined(__aarch64__)
        const uintptr_t tls_map_va = 0x0000006ffffd0000ull;
#else
        const uintptr_t tls_map_va = 0x0000400000001000ull;
#endif
        if (process->mapping_count >= 32) return false;
        uintptr_t tls_frame = memory::VirtualMemoryManager::get_physical_address(&process->address_space, tls_map_va);
        if (tls_frame == 0) {
            tls_frame = memory::PhysicalMemoryManager::alloc_frame();
            if (tls_frame == 0) return false;
            if (!memory::VirtualMemoryManager::map_page(&process->address_space, tls_map_va, tls_frame,
                                                        memory::PAGE_PRESENT | memory::PAGE_USER | memory::PAGE_WRITABLE)) {
                memory::PhysicalMemoryManager::free_frame(tls_frame);
                return false;
            }
            process->mappings[process->mapping_count++] = {tls_map_va, memory::PAGE_SIZE, false};
        }
        auto* tls_mem = reinterpret_cast<uint8_t*>(tls_frame);
        for (size_t j = 0; j < memory::PAGE_SIZE; ++j) tls_mem[j] = 0;
        if (tls_filesz != 0) {
            for (uint16_t i = 0; i < header->e_phnum; ++i) {
                const auto* ph = reinterpret_cast<const Elf64ProgramHeader*>(
                    elf_data + header->e_phoff + static_cast<size_t>(i) * header->e_phentsize);
                if (ph->p_type == PT_TLS) {
                    for (uint32_t j = 0; j < tls_filesz; ++j)
                        tls_mem[j] = elf_data[ph->p_offset + j];
                    break;
                }
            }
        }
        // Static TLS base: end of the aligned TLS block within the page.
        const size_t align = tls_align ? tls_align : 1;
        const size_t tls_size = tls_memsz ? tls_memsz : tls_filesz;
        const uintptr_t tls_base = tls_map_va + ((tls_size + align - 1) / align) * align;
        process->tls_base = tls_base;
        kernel::kprintf("[+] ELF PT_TLS: filesz=%u memsz=%u align=%u base=%x\n",
                        tls_filesz, tls_memsz, tls_align, tls_base);
    }

#if defined(__riscv)
    // RV64: user stack just below the user mmap limit (0x70000000).
    constexpr uintptr_t stack_top = 0x000000006ffff000ull;
#elif defined(__aarch64__)
    // AArch64: user stack just below the user mmap limit (0x7000000000).
    constexpr uintptr_t stack_top = 0x0000006fffff0000ull;
#else
    // x86_64: user stack just below the user mmap limit (0x700000000000).
    // This is well above the ELF base (0x400000000000) so large static
    // binaries (e.g. musl-linked) never collide with the stack page.
    constexpr uintptr_t stack_top = 0x00006ffffffff000ull;
#endif
    constexpr size_t stack_pages = 16;
    constexpr uintptr_t stack_page = stack_top - memory::PAGE_SIZE;
    constexpr uintptr_t stack_base = stack_top - stack_pages * memory::PAGE_SIZE;
    for (size_t page = 0; page < stack_pages; ++page) {
        const uintptr_t frame = memory::PhysicalMemoryManager::alloc_frame();
        const uintptr_t address = stack_base + page * memory::PAGE_SIZE;
        if (frame == 0 || !memory::VirtualMemoryManager::map_page(
                &process->address_space, address, frame,
                memory::PAGE_PRESENT | memory::PAGE_USER | memory::PAGE_WRITABLE)) {
        kernel::kprintf("[!] ELF stack map failed at %x\n", stack_page);
            return false;
        }
        auto* page_memory = reinterpret_cast<uint8_t*>(frame);
        for (size_t i = 0; i < memory::PAGE_SIZE; ++i) page_memory[i] = 0;
    }
    auto* stack_memory = reinterpret_cast<uint8_t*>(
        memory::VirtualMemoryManager::get_physical_address(&process->address_space, stack_page));
    if (stack_memory == nullptr) return false;
    if (process->mapping_count >= 32) return false;
    process->mappings[process->mapping_count++] = {stack_base, stack_pages * memory::PAGE_SIZE, false};
    *entry = static_cast<uintptr_t>(header->e_entry);
    *stack = stack_page + 0x800;
    process->user_entry = *entry;
    process->user_stack = *stack;

    // Omega/Linux initial process stack ABI (Linux-exact, contiguous):
    //   [0x800] argc
    //   [0x808] argv[0..argc] (NULL-terminated)
    //   [0x808 + (argc+1)*8] envp[0..n] (NULL-terminated)
    //   [after envp] auxv[0..] (AT_NULL terminated)
    //   [after auxv] string data
    // musl's __init_libc walks envp to find the auxv, so these MUST be
    // contiguous with no padding.
    auto* stack_words = reinterpret_cast<uint64_t*>(stack_memory);
    const uintptr_t argv_table = stack_page + 0x808;
    uintptr_t cursor = 0x808;

    if (argc < 0) argc = 0;
    if (argc > 64) argc = 64;

    // argv table
    for (int i = 0; i < argc; ++i) {
        stack_words[cursor / sizeof(uint64_t)] = 0;
        cursor += sizeof(uint64_t);
    }
    stack_words[cursor / sizeof(uint64_t)] = 0; // argv NULL terminator
    cursor += sizeof(uint64_t);

    // envp table (empty for now — the boot path passes none)
    const uintptr_t envp_table = stack_page + cursor;
    (void)envp_table;
    int env_count = 0;
    if (envp) {
        while (envp[env_count] && env_count < 64) {
            stack_words[cursor / sizeof(uint64_t)] = 0; // filled after strings
            ++env_count;
            cursor += sizeof(uint64_t);
        }
    }
    stack_words[cursor / sizeof(uint64_t)] = 0; // envp NULL terminator
    cursor += sizeof(uint64_t);

    // auxv: AT_NULL only (musl reads AT_PAGESZ/AT_HWCAP; absent is fine).
    stack_words[cursor / sizeof(uint64_t)] = 0;
    stack_words[cursor / sizeof(uint64_t) + 1] = 0;
    cursor += 2 * sizeof(uint64_t);

    // Strings start after the tables.
    uintptr_t string_cursor = stack_page + cursor;

    // Fill argv strings.
    for (int i = 0; i < argc; ++i) {
        if (argv && argv[i]) {
            stack_words[(argv_table - stack_page) / sizeof(uint64_t) + i] = string_cursor;
            const char* s = argv[i];
            auto* dst = reinterpret_cast<char*>(stack_memory + (string_cursor - stack_page));
            while (*s) { *dst++ = *s++; ++string_cursor; }
            *dst = '\0'; ++string_cursor;
        }
    }

    // Fill envp strings.
    if (envp) {
        for (int i = 0; i < env_count; ++i) {
            stack_words[(envp_table - stack_page) / sizeof(uint64_t) + i] = string_cursor;
            const char* s = envp[i];
            auto* dst = reinterpret_cast<char*>(stack_memory + (string_cursor - stack_page));
            while (*s) { *dst++ = *s++; ++string_cursor; }
            *dst = '\0'; ++string_cursor;
        }
    }

    stack_words[0x800 / sizeof(uint64_t)] = static_cast<uint64_t>(argc);
    *stack = stack_page + 0x800;
    process->user_stack = *stack;
    return true;
}

uintptr_t ElfLoader::load(const uint8_t* elf_data) {
    (void)elf_data;
    kernel::kprintf("[!] Refusing unsized ELF load; provide the image size.\n");
    return 0;
}

} // namespace elf
