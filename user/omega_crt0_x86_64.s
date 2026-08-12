.section .text
.global _start
.type _start, @function
_start:
    mov %rsp, %rdi
    call omega_start
1:  pause
    jmp 1b
