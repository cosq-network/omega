/* Omega static userland linker script (x86_64). */
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000000000;
    /* Static binary: crt1 references _DYNAMIC only for the PIE path; a
       static non-PIE build has no dynamic section. Define it at the start
       of .data so the PC32 relocation is in range (never dereferenced). */
    .text : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    .data : {
        _DYNAMIC = .;
        *(.data*) *(.sdata*) *(.ldata*)
    } :data
    .bss : { *(.bss*) *(.sbss*) *(.lbss*) *(COMMON) } :data
}
