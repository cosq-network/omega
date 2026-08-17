// Omega syscall shim for musl — AArch64.
// Linux AArch64 ABI: syscall number in x8, args in x0-x5, result in x0.
.section .text

// long __omega_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
.global __omega_syscall6
.type __omega_syscall6, %function
__omega_syscall6:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    ldr x5, [sp]
    svc #0
    ret

// long __omega_syscall3(long nr, long a1, long a2, long a3)
.global __omega_syscall3
.type __omega_syscall3, %function
__omega_syscall3:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    svc #0
    ret

.section .note.GNU-stack,"",@progbits
