.section .text.boot
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
    csrw sscratch, sp

    /* OpenSBI passes hartid in a0 and the FDT address in a1. */
    mv s0, a1

    /* Jump to C++ Kernel Main */
    mv a0, s0
    call kernel_main

1:  wfi
    j 1b

.section .boot_stack, "aw", @nobits
.align 16
stack_bottom:
    .skip 16384 /* 16 KiB Boot Stack */
stack_top:
