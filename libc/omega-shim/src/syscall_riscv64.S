// Omega syscall shim for musl — RISC-V 64.
// Linux RV64 ABI: syscall number in a7, args in a0-a5, result in a0.
.section .text

// long __omega_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
.global __omega_syscall6
.type __omega_syscall6, @function
__omega_syscall6:
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    mv a3, a4
    mv a4, a5
    ld a5, 0(sp)
    ecall
    ret

// long __omega_syscall3(long nr, long a1, long a2, long a3)
.global __omega_syscall3
.type __omega_syscall3, @function
__omega_syscall3:
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    ecall
    ret

.section .note.GNU-stack,"",@progbits
