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
    if (process == nullptr || entry == nullptr || stack == nullptr ||
        !validate(elf_data, image_size)) return false;
    const auto* header = reinterpret_cast<const Elf64Header*>(elf_data);
    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf64ProgramHeader*>(
            elf_data + header->e_phoff + static_cast<size_t>(i) * header->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        if (ph->p_vaddr >= 0x0000800000000000ull || ph->p_vaddr + ph->p_memsz < ph->p_vaddr) return false;
        const uintptr_t first = align_down(static_cast<uintptr_t>(ph->p_vaddr));
        const uintptr_t last = align_up(static_cast<uintptr_t>(ph->p_vaddr + ph->p_memsz));
        if (last == 0 || last <= first) return false;
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

#if defined(__riscv)
    constexpr uintptr_t stack_top = 0x000000006ffff000ull;
#elif defined(__aarch64__)
    constexpr uintptr_t stack_top = 0x0000006fffff0000ull;
#else
    constexpr uintptr_t stack_top = 0x0000400000005000ull;
#endif
    constexpr uintptr_t stack_page = stack_top - memory::PAGE_SIZE;
    const uintptr_t frame = memory::PhysicalMemoryManager::alloc_frame();
    if (frame == 0 || !memory::VirtualMemoryManager::map_page(&process->address_space, stack_page, frame,
                                                               memory::PAGE_PRESENT | memory::PAGE_USER | memory::PAGE_WRITABLE)) {
        kernel::kprintf("[!] ELF stack map failed at %x\n", stack_page);
        return false;
    }
    auto* stack_memory = reinterpret_cast<uint8_t*>(frame);
    for (size_t i = 0; i < memory::PAGE_SIZE; ++i) stack_memory[i] = 0;
    if (process->mapping_count >= 32) return false;
    process->mappings[process->mapping_count++] = {stack_page, memory::PAGE_SIZE, false};
    *entry = static_cast<uintptr_t>(header->e_entry);
    *stack = stack_page + 0x800;
    process->user_entry = *entry;
    process->user_stack = *stack;

    // Omega initial process stack ABI:
    //   argc, argv, envp, auxv; argv[0] points to /init; auxv ends AT_NULL.
    auto* stack_words = reinterpret_cast<uint64_t*>(stack_memory);
    auto* argv = reinterpret_cast<uintptr_t*>(stack_memory + 0x880);
    auto* envp = reinterpret_cast<uintptr_t*>(stack_memory + 0x890);
    auto* auxv = reinterpret_cast<uintptr_t*>(stack_memory + 0x8a0);
    auto* program = reinterpret_cast<char*>(stack_memory + 0x8c0);
    const char init_name[] = "/init";
    for (size_t i = 0; i < sizeof(init_name); ++i) program[i] = init_name[i];
    const uintptr_t user_argv = stack_page + 0x880;
    const uintptr_t user_envp = stack_page + 0x890;
    const uintptr_t user_auxv = stack_page + 0x8a0;
    const uintptr_t user_program = stack_page + 0x8c0;
    argv[0] = user_program;
    argv[1] = 0;
    envp[0] = 0;
    auxv[0] = 0;
    auxv[1] = 0;
    stack_words[0x800 / sizeof(uint64_t)] = 1;
    stack_words[0x808 / sizeof(uint64_t)] = user_argv;
    stack_words[0x810 / sizeof(uint64_t)] = user_envp;
    stack_words[0x818 / sizeof(uint64_t)] = user_auxv;
    return true;
}

uintptr_t ElfLoader::load(const uint8_t* elf_data) {
    (void)elf_data;
    kernel::kprintf("[!] Refusing unsized ELF load; provide the image size.\n");
    return 0;
}

} // namespace elf
