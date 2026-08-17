/* Multiboot2 Header Constants */
.set MULTIBOOT2_MAGIC,     0xe85250d6
.set MULTIBOOT2_ARCH_X86,  0
.set HEADER_LENGTH,        (multiboot_header_end - multiboot_header)
.set CHECKSUM,             -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_X86 + HEADER_LENGTH)
.set MULTIBOOT2_BOOT_MAGIC, 0x36d76289

.section .multiboot2, "a"
.align 8
multiboot_header:
    .long MULTIBOOT2_MAGIC
    .long MULTIBOOT2_ARCH_X86
    .long HEADER_LENGTH
    .long CHECKSUM

    /* Entry Address Tag */
    .short 3
    .short 0
    .long 12
    .long _start

    /* Framebuffer request tag (type 5) */
    .align 8
    .short 5
    .short 0
    .long 20
    .long 1024
    .long 768
    .long 32

    /* End Tag */
    .short 0
    .short 0
    .long 8
multiboot_header_end:

/* Xen PVH ELF Note for QEMU Direct Kernel Boot */
.section .xen_note, "a", @note
.align 4
pvh_note_start:
    .long 4                    /* Name length ("Xen\0") */
    .long 4                    /* Desc length (4 bytes) */
    .long 18                   /* XEN_ELFNOTE_PHYS32_ENTRY (18) */
    .asciz "Xen"
    .align 4
    .long _start               /* 32-bit Physical Entry Address */

/* Initial Page Tables — identity map first 4 GiB via 2 MiB huge pages */
.section .bss
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096
pd1:
    .skip 4096
pd2:
    .skip 4096
pd3:
    .skip 4096

.global multiboot_info_ptr
multiboot_info_ptr:
    .skip 8
.global multiboot_boot_magic
multiboot_boot_magic:
    .skip 4

.align 16
stack_bottom:
    .skip 16384 /* 16 KiB Boot Stack */
stack_top:

/* Early GDT for 64-bit Long Mode Transition */
.section .rodata
.align 8
.global gdt64
gdt64:
    .quad 0 # Null Descriptor
    .quad 0x00af9a000000ffff # 64-bit Code Segment (Kernel)
    .quad 0x00cf92000000ffff # 64-bit Data Segment (Kernel)
    .quad 0x00affa000000ffff # 64-bit Code Segment (User, DPL3)
    .quad 0x00cff2000000ffff # 64-bit Data Segment (User, DPL3)
    .quad 0                    # 64-bit TSS descriptor (filled by C++)
    .quad 0
gdt64_pointer:
    .word . - gdt64 - 1
    .quad gdt64

/* 32-bit Protected Mode Entry */
.section .text.boot
.code32
.global _start
.type _start, @function
_start:
    cli
    mov $stack_top, %esp

    /* Preserve Multiboot2 info pointer when a Multiboot2 bootloader is used. */
    cmpl $MULTIBOOT2_BOOT_MAGIC, %eax
    jne 1f
    mov %ebx, (multiboot_info_ptr)
    mov %eax, (multiboot_boot_magic)
1:

    /* pml4[0] -> pdpt */
    mov $pdpt, %eax
    or $0x3, %eax
    mov %eax, pml4

    xor %ecx, %ecx              /* gb = 0..3 (each gigabyte) */
0:
    cmp $4, %ecx
    jge paging_done

    mov $pd, %ebx
    mov %ecx, %eax
    shl $12, %eax             /* gb * 4096 -> next page directory */
    add %eax, %ebx

    mov %ebx, %eax
    or $0x3, %eax
    mov %eax, pdpt(,%ecx,8)

    xor %edx, %edx            /* i = 0..511 */
2:
    mov %ecx, %esi
    shl $9, %esi              /* gb * 512 */
    add %edx, %esi
    mov %esi, %eax
    shl $21, %eax
    or $0x83, %eax
    mov %eax, (%ebx,%edx,8)
    inc %edx
    cmp $512, %edx
    jl 2b

    inc %ecx
    jmp 0b

paging_done:
    /* Load PML4 to CR3 */
    mov $pml4, %eax
    mov %eax, %cr3

    /* Enable PAE in CR4 */
    mov %cr4, %eax
    or $0x20, %eax
    /* Enable SSE: CR4.OSFXSR (bit 9) + OSXMMEXCPT (bit 10). Required for
       libc code (musl emits SSE for memcpy/strings) to run in userspace. */
    or $0x600, %eax
    mov %eax, %cr4

    /* Set Long Mode Bit in EFER MSR (0xC0000080) */
    mov $0xC0000080, %ecx
    rdmsr
    /* Enable long mode and make NX page permissions architectural. */
    or $0x900, %eax
    wrmsr

    /* Enable Paging in CR0, plus MP (bit 1) for SSE co-existence. */
    mov %cr0, %eax
    or $0x80000003, %eax
    mov %eax, %cr0

    /* Load 64-bit GDT */
    lgdt gdt64_pointer

    /* Far jump to 64-bit Long Mode code segment */
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    /* Reload 64-bit data segment registers */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* Pass the validated Multiboot information pointer to C++. */
    mov multiboot_info_ptr(%rip), %edi
    call kernel_main

1:  hlt
    jmp 1b
