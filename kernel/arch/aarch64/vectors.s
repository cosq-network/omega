/* EL1 vectors with a real lower-EL SVC/data/instruction-fault path. */
.section .text.vectors, "ax"
.align 11
.global el1_vector_table
el1_vector_table:
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b lower_sync
.align 7; b lower_irq
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception
.align 7; b unhandled_exception

lower_sync:
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, elr_el1; str x0, [sp, #248]
    mrs x0, spsr_el1; str x0, [sp, #256]
    mov x0, sp
    bl aarch64_exception_handler
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldr x0, [sp, #248]; msr elr_el1, x0
    ldr x0, [sp, #256]; msr spsr_el1, x0
    add sp, sp, #272
    eret

lower_irq:
    bl aarch64_timer_interrupt
    eret

unhandled_exception:
    wfe
    b unhandled_exception

.section .text
.global aarch64_prepare_exception_stack
aarch64_prepare_exception_stack:
    msr sp_el1, x0
    ret
.global aarch64_enter_userland
aarch64_enter_userland:
    msr sp_el0, x1
    msr elr_el1, x0
    mov x2, #0
    msr spsr_el1, x2
    eret
