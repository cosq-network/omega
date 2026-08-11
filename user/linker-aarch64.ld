ENTRY(_start)
SECTIONS { . = 0x4000000000; .text : { *(.text*) } .rodata : { *(.rodata*) } }
