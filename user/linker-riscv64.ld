ENTRY(_start)
SECTIONS {
    . = 0x40000000;
    .text ALIGN(0x1000) : { *(.text*) }
    .rodata ALIGN(0x1000) : { *(.rodata*) }
    .data ALIGN(0x1000) : { *(.data*) *(.sdata*) *(.ldata*) }
    .bss ALIGN(0x1000) : { *(.bss*) *(.sbss*) *(.lbss*) *(COMMON) }
}
