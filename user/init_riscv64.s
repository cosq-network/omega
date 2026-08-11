.section .text
.global _start
_start:
    la a1, message
    li a0, 1
    li a2, message_end-message
    li a7, 64
    ecall
    li a0, 0
    li a7, 93
    ecall
1:  wfi
    j 1b
.section .rodata
message:
    .asciz "Omega userspace init: RISC-V U-mode syscall path is alive\n"
message_end:
