.section .text

.global x86_enter_userland
.type x86_enter_userland, @function
x86_enter_userland:
    cli
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    pushq $0x23
    pushq %rsi
    /* Keep interrupts masked until a TSS/RSP0 is installed for user IRQs. */
    pushq $0x2
    pushq $0x1b
    pushq %rdi
    iretq

.global x86_syscall_entry
.type x86_syscall_entry, @function
x86_syscall_entry:
    /* GS_BASE points at the current process's kernel syscall stack. */
    swapgs
    mov %rsp, %gs:8
    mov %gs:0, %rsp

    /* syscall places the user return RIP in RCX and RFLAGS in R11. */
    pushq $0x23
    pushq %gs:8
    pushq %r11
    pushq $0x1b
    pushq %rcx
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax
    mov %rsp, %rdi
    call x86_syscall_interrupt
    mov %rax, %rsp

    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    mov %rdx, %gs:16
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    pop %rcx
    add $8, %rsp
    pop %r11
    pop %rdx
    add $8, %rsp
    mov %rdx, %rsp
    mov %gs:16, %rdx
    swapgs
    sysretq

.global x86_page_fault_stub
.type x86_page_fault_stub, @function
x86_page_fault_stub:
    cld
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax
    mov %rsp, %rdi
    call x86_page_fault
    mov %rax, %rsp
    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    add $8, %rsp              /* discard the page-fault error code */
    iretq
