ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* Keep the image outside the kernel's inherited low-4GiB huge pages. */
    . = 0x400000000000;
    .text ALIGN(0x1000) : { *(.text*) } :text
    .rodata ALIGN(0x1000) : { *(.rodata*) } :text
    .data ALIGN(0x1000) : { *(.data*) *(.sdata*) *(.ldata*) } :data
    .bss ALIGN(0x1000) : { *(.bss*) *(.sbss*) *(.lbss*) *(COMMON) } :data
}
