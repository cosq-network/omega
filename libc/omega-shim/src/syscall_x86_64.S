// Omega syscall shim for musl — x86_64.
// musl's internal __syscall() is redirected here. Omega uses Linux syscall
// numbers and the System V AMD64 calling convention: number in rax, args in
// rdi/rsi/rdx/r10/r8/r9, result in rax (negative = -errno).
.section .text

// long __omega_syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
.global __omega_syscall6
.type __omega_syscall6, @function
__omega_syscall6:
    movq %rdi, %rax      // syscall number
    movq %rsi, %rdi      // a1
    movq %rdx, %rsi      // a2
    movq %rcx, %rdx      // a3
    movq %r8,  %r10      // a4
    movq %r9,  %r8       // a5
    movq 8(%rsp), %r9    // a6 (stack)
    syscall
    ret

// long __omega_syscall3(long nr, long a1, long a2, long a3)
.global __omega_syscall3
.type __omega_syscall3, @function
__omega_syscall3:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    syscall
    ret

.section .note.GNU-stack,"",@progbits
