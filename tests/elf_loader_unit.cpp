#include "kernel/elf_loader.hpp"

namespace kernel { void kprintf(const char*, ...) {} }

int main() {
    alignas(8) uint8_t image[8192]{};
    auto* header = reinterpret_cast<elf::Elf64Header*>(image);
    header->e_ident[0] = 0x7F; header->e_ident[1] = 'E'; header->e_ident[2] = 'L'; header->e_ident[3] = 'F';
    header->e_ident[4] = 2; header->e_ident[5] = 1; header->e_ident[6] = 1;
    header->e_type = 2;
#if defined(__x86_64__)
    header->e_machine = 62;
#elif defined(__aarch64__)
    header->e_machine = 183;
#else
    header->e_machine = 243;
#endif
    header->e_version = 1;
    header->e_ehsize = sizeof(elf::Elf64Header);
    header->e_phoff = sizeof(elf::Elf64Header);
    header->e_phentsize = sizeof(elf::Elf64ProgramHeader);
    header->e_phnum = 1;
    auto* segment = reinterpret_cast<elf::Elf64ProgramHeader*>(image + header->e_phoff);
    segment->p_type = 1; segment->p_offset = 0x1000; segment->p_vaddr = 0x400000;
    segment->p_filesz = 4; segment->p_memsz = 0x1000; segment->p_align = 0x1000;
    if (!elf::ElfLoader::validate(image, sizeof(image))) return 1;

    segment->p_type = 3;
    if (elf::ElfLoader::validate(image, sizeof(image))) return 2;
    segment->p_type = 1;
    header->e_type = 3;
    segment->p_type = 2;
    if (elf::ElfLoader::validate(image, sizeof(image))) return 3;
    return 0;
}
