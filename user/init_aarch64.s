.section .text
.global _start
_start:
    adr x1, message
    mov x0, #1
    mov x2, #(message_end-message)
    mov x8, #64
    svc #0
    mov x0, #0
    mov x8, #93
    svc #0
1:  wfe
    b 1b
.section .rodata
message:
    .asciz "Omega userspace init: AArch64 EL0 syscall path is alive\n"
message_end:
