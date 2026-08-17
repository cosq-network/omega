ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* Read + Execute */
    rodata PT_LOAD FLAGS(4); /* Read Only */
    data PT_LOAD FLAGS(6);   /* Read + Write */
    note PT_NOTE FLAGS(4);   /* Read Only Note Header */
}

SECTIONS
{
    . = 1M;

    .xen_note ALIGN(4) :
    {
        *(.xen_note)
    } :note :text

    .text ALIGN(4K) :
    {
        *(.multiboot2)
        KEEP(*(.text.boot))
        *(.text)
    } :text

    .rodata ALIGN(4K) :
    {
        *(.rodata)
    } :rodata

    .data ALIGN(4K) :
    {
        *(.data)
    } :data

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    } :data

    .pmm_bitmap ALIGN(4K) :
    {
        *(.pmm_bitmap)
    } :data
}
