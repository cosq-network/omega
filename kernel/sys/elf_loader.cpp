#include "kernel/elf_loader.hpp"
#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"

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

uintptr_t ElfLoader::load(const uint8_t* elf_data) {
    (void)elf_data;
    kernel::kprintf("[!] Refusing unsized ELF load; provide the image size.\n");
    return 0;
}

} // namespace elf
