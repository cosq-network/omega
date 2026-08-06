#include "kernel/elf_loader.hpp"
#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"

namespace elf {

bool ElfLoader::validate(const uint8_t* elf_data) {
    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(elf_data);
    // Check ELF Magic Bytes: 0x7F, 'E', 'L', 'F'
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
        return false;
    }
    // 64-bit ELF check (class 2)
    return header->e_ident[4] == 2;
}

uintptr_t ElfLoader::load(const uint8_t* elf_data) {
    if (!validate(elf_data)) {
        kernel::kprintf("[!] Invalid ELF Binary Header Magic Bytes!\n");
        return 0;
    }

    const Elf64Header* header = reinterpret_cast<const Elf64Header*>(elf_data);
    kernel::kprintf("[+] Valid 64-bit ELF Binary Detected.\n");
    kernel::kprintf("    ELF Entry Point: %x, Program Headers: %u\n", header->e_entry, header->e_phnum);

    const Elf64ProgramHeader* ph = reinterpret_cast<const Elf64ProgramHeader*>(elf_data + header->e_phoff);
    for (uint16_t i = 0; i < header->e_phnum; ++i) {
        if (ph[i].p_type == 1) { // PT_LOAD segment
            kernel::kprintf("    --> PT_LOAD Segment [%u]: Virt %x, Memory Size: %u bytes\n",
                            i, ph[i].p_vaddr, ph[i].p_memsz);
        }
    }

    return header->e_entry;
}

} // namespace elf
