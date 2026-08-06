.section .text
.global _start
.type _start, @function

_start:
    /* Disable Supervisor Interrupts */
    csrc sstatus, 2

    /* Set up Supervisor Trap Vector (stvec) */
    la t0, trap_entry
    csrw stvec, t0

    /* Set up Stack Pointer */
    la sp, stack_top

    /* Jump to C++ Kernel Main */
    call kernel_main

1:  wfi
    j 1b

.section .bss
.align 16
stack_bottom:
    .skip 16384 /* 16 KiB Boot Stack */
stack_top:
