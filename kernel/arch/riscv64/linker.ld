OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80200000; /* RAM base offset above OpenSBI dynamic firmware (0x80000000) */

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

    .boot_stack (NOLOAD) : ALIGN(16)
    {
        *(.boot_stack)
    }
}
