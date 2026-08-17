.section .text
.align 2
.global trap_entry
.type trap_entry, @function
trap_entry:
    /* sscratch contains the supervisor stack while U-mode owns sp. */
    csrrw sp, sscratch, sp
    addi sp, sp, -296
    sd x1, 8(sp); sd x2, 16(sp); sd x3, 24(sp); sd x4, 32(sp)
    sd x5, 40(sp); sd x6, 48(sp); sd x7, 56(sp); sd x8, 64(sp)
    sd x9, 72(sp); sd x10, 80(sp); sd x11, 88(sp); sd x12, 96(sp)
    sd x13, 104(sp); sd x14, 112(sp); sd x15, 120(sp); sd x16, 128(sp)
    sd x17, 136(sp); sd x18, 144(sp); sd x19, 152(sp); sd x20, 160(sp)
    sd x21, 168(sp); sd x22, 176(sp); sd x23, 184(sp); sd x24, 192(sp)
    sd x25, 200(sp); sd x26, 208(sp); sd x27, 216(sp); sd x28, 224(sp)
    sd x29, 232(sp); sd x30, 240(sp); sd x31, 248(sp)
    csrr t0, sscratch; sd t0, 0(sp)
    csrr t0, sepc; sd t0, 256(sp)
    csrr t0, scause; sd t0, 264(sp)
    csrr t0, stval; sd t0, 272(sp)
    csrr t0, sstatus; sd t0, 280(sp)
    /* Supervisor code must opt in before dereferencing U-mode buffers. */
    csrr t0, sstatus
    li t1, (1 << 18) /* SUM */
    or t0, t0, t1
    csrw sstatus, t0
    mv a0, sp
    call riscv_exception_handler
    mv t6, sp
    ld x1, 8(t6); ld x2, 16(t6); ld x3, 24(t6); ld x4, 32(t6)
    ld x5, 40(t6); ld x6, 48(t6); ld x7, 56(t6); ld x8, 64(t6)
    ld x9, 72(t6); ld x10, 80(t6); ld x11, 88(t6); ld x12, 96(t6)
    ld x13, 104(t6); ld x14, 112(t6); ld x15, 120(t6); ld x16, 128(t6)
    ld x17, 136(t6); ld x18, 144(t6); ld x19, 152(t6); ld x20, 160(t6)
    ld x21, 168(t6); ld x22, 176(t6); ld x23, 184(t6); ld x24, 192(t6)
    ld x25, 200(t6); ld x26, 208(t6); ld x27, 216(t6); ld x28, 224(t6)
    addi t5, t6, 296
    csrw sscratch, t5
    csrr t0, sstatus
    li t1, (1 << 18)
    not t1, t1
    and t0, t0, t1
    csrw sstatus, t0
    ld t0, 256(t6)
    csrw sepc, t0
    ld t0, 280(t6)
    csrw sstatus, t0
    ld x29, 232(t6); ld x30, 240(t6)
    ld sp, 0(t6)
    ld x31, 248(t6)
    sret

.global riscv_prepare_exception_stack
riscv_prepare_exception_stack:
    csrw sscratch, a0
    ret
.global riscv_enter_userland
riscv_enter_userland:
    mv sp, a1
    csrw sepc, a0
    csrr t0, sstatus
    li t1, (1 << 8)
    not t1, t1
    and t0, t0, t1
    ori t0, t0, (1 << 5)
    csrw sstatus, t0
    sret
.global riscv_enter_userland_tls
riscv_enter_userland_tls:
    mv sp, a1
    mv tp, a2
    csrw sepc, a0
    csrr t0, sstatus
    li t1, (1 << 8)
    not t1, t1
    and t0, t0, t1
    ori t0, t0, (1 << 5)
    csrw sstatus, t0
    sret
