ENTRY(_start)

SECTIONS
{
    . = 0x40080000; /* RAM Base for QEMU virt board */

    .text : ALIGN(4K)
    {
        KEEP(*(.text.boot))
        *(.text)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    .pmm_bitmap : ALIGN(4K)
    {
        *(.pmm_bitmap)
    }

    .boot_stack (NOLOAD) : ALIGN(16)
    {
        *(.boot_stack)
    }
}
