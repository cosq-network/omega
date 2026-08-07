.section .text.boot
.global _start
.type _start, %function

_start:
    /* Preserve the firmware/QEMU DTB pointer from x0 across setup. */
    mov x19, x0
    /* Mask all interrupts (DAIF) */
    msr daifset, #0xf

    /* Check Current Exception Level */
    mrs x0, CurrentEL
    lsr x0, x0, #2
    and x0, x0, #3

    /* If EL1, skip EL2 drop */
    cmp x0, #1
    b.eq .el1_entry

    /* If EL2, drop down to EL1 */
    cmp x0, #2
    b.ne .el_unknown

    /* Enable 64-bit execution in EL1 via HCR_EL2 */
    mov x1, #(1 << 31) /* RW = 1 (AArch64) */
    msr hcr_el2, x1

    /* Set up SPSR_EL2 for ERET to EL1h (SP_EL1) */
    mov x1, #0x3c5 /* M[3:0] = 0b0101 (EL1h), interrupts masked */
    msr spsr_el2, x1

    /* Return address for ERET */
    adr x1, .el1_entry
    msr elr_el2, x1
    eret

.el_unknown:
1:  wfe
    b 1b

.el1_entry:
    /* Set up Exception Vector Table (VBAR_EL1) */
    ldr x0, =el1_vector_table
    msr vbar_el1, x0

    /* Enable FP/SIMD access in EL1 (CPACR_EL1) */
    mov x0, #(3 << 20) /* FPEN = 0b11 */
    msr cpacr_el1, x0

    /* Set up Stack Pointer for EL1 */
    ldr x0, =stack_top
    mov sp, x0

    /* QEMU/firmware passes the Flattened Device Tree in x0. Preserve it
       through the early setup and make it available to the display HAL. */
    mov x0, x19

    /* Jump to C++ Kernel Entry Point */
    bl kernel_main

1:  wfe
    b 1b

.section .boot_stack, "aw", %nobits
.align 16
stack_bottom:
    .skip 16384 /* 16 KiB Boot Stack */
stack_top:
