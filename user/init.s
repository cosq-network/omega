.section .text
.global _start
.type _start, @function
_start:
    lea message(%rip), %rsi
    mov $1, %rax              /* SYS_write */
    mov $1, %rdi              /* stdout */
    mov $message_end-message, %rdx
    syscall

    mov $60, %rax             /* SYS_exit */
    xor %rdi, %rdi
    syscall

1:
    pause
    jmp 1b

.section .rodata
message:
    .asciz "Omega userspace init: Ring 3 syscall path is alive\n"
message_end:

.section .note.GNU-stack,"",@progbits
