#ifndef OMEGA_KERNEL_ELF_LOADER_HPP
#define OMEGA_KERNEL_ELF_LOADER_HPP

#include "std/cstdint.hpp"

namespace process { struct Process; }

namespace elf {

struct Elf64Header {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64ProgramHeader {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

class ElfLoader {
public:
    static bool validate(const uint8_t* elf_data);
    static bool validate(const uint8_t* elf_data, size_t image_size);
    static uintptr_t load(const uint8_t* elf_data, size_t image_size);
    static bool load_into(process::Process* process, const uint8_t* elf_data,
                          size_t image_size, uintptr_t* entry, uintptr_t* stack);
    static bool load_into(process::Process* process, const uint8_t* elf_data,
                          size_t image_size, uintptr_t* entry, uintptr_t* stack,
                          int argc, const char* const* argv, const char* const* envp);
    // Unsized loading cannot be made memory-safe; retained as a compatibility
    // entry that fails closed.
    static uintptr_t load(const uint8_t* elf_data);
};

} // namespace elf

#endif // OMEGA_KERNEL_ELF_LOADER_HPP
