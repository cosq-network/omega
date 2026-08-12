.section .text
.global omega_syscall6
.type omega_syscall6, @function
omega_syscall6:
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    mv a3, a4
    mv a4, a5
    mv a5, a6
    ecall
    ret
