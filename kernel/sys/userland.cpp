#include "kernel/userland.hpp"
#include "kernel/kprint.hpp"
#include "kernel/syscall.hpp"
#include "kernel/process.hpp"

#if defined(__x86_64__)
extern "C" void x86_enter_userland(uintptr_t user_entry, uintptr_t user_stack);
extern "C" void x86_syscall_entry();

namespace {
alignas(16) static uint8_t syscall_stack[16 * 1024];

struct SyscallFrame {
    /* Matches the top-of-stack order produced by user_entry.s. */
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, cs, rflags, rsp, ss;
};

static inline void wrmsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "c"(msr), "a"(static_cast<uint32_t>(value)),
                 "d"(static_cast<uint32_t>(value >> 32)) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low = 0, high = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}
}

extern "C" uintptr_t x86_syscall_interrupt(SyscallFrame* frame) {
    if (frame == nullptr) return reinterpret_cast<uintptr_t>(frame);
    frame->rax = static_cast<uint64_t>(syscall::SyscallDispatcher::dispatch6(
        frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9));
    return reinterpret_cast<uintptr_t>(frame);
}

extern "C" uintptr_t x86_user_kernel_stack_top() {
    return reinterpret_cast<uintptr_t>(syscall_stack) + sizeof(syscall_stack);
}

extern "C" void x86_page_fault(void* raw_frame) {
    uint64_t fault_address = 0;
    asm volatile("mov %%cr2, %0" : "=r"(fault_address));
    auto* words = reinterpret_cast<const uint64_t*>(raw_frame);
    const uint64_t error = words != nullptr ? words[15] : 0;
    const uint64_t rip = words != nullptr ? words[16] : 0;
    kernel::kprintf("[PANIC] x86 page fault addr=%x error=%x rip=%x\n",
                    fault_address, error, rip);
}
#endif

namespace userland {

void UserlandManager::init_x86_syscall_stack() {
#if defined(__x86_64__)
    const uintptr_t stack_top = reinterpret_cast<uintptr_t>(syscall_stack) + sizeof(syscall_stack);
    const uint64_t star = (static_cast<uint64_t>(0x18) << 48) | (static_cast<uint64_t>(0x08) << 32);
    wrmsr(0xC0000101, 0);                // IA32_GS_BASE (user-visible base)
    wrmsr(0xC0000102, stack_top);        // IA32_KERNEL_GS_BASE
    wrmsr(0xC0000081, star);            // IA32_STAR
    wrmsr(0xC0000082, reinterpret_cast<uintptr_t>(&x86_syscall_entry)); // IA32_LSTAR
    wrmsr(0xC0000084, (1ull << 9) | (1ull << 10)); // clear IF and DF on entry
    wrmsr(0xC0000080, rdmsr(0xC0000080) | 1ull);   // IA32_EFER.SCE
    kernel::kprintf("[+] x86_64 native syscall entry installed (STAR/LSTAR/GS stack).\n");
#endif
}

void UserlandManager::init() {
    kernel::kprintf("[+] Userland Mode Manager (Ring 3 / EL0) Initialized.\n");
}

void UserlandManager::enter_userland(uintptr_t user_entry, uintptr_t user_stack) {
#if defined(__x86_64__)
    kernel::kprintf("[+] Entering x86_64 Ring 3 init (Entry: %x, Stack: %x)\n", user_entry, user_stack);
    x86_enter_userland(user_entry, user_stack);
#else
    (void)user_entry;
    (void)user_stack;
    kernel::kprintf("[!] Native userland entry is only enabled on x86_64.\n");
#endif
}

} // namespace userland

extern "C" void jump_to_userland(uintptr_t user_entry, uintptr_t user_stack) {
    userland::UserlandManager::enter_userland(user_entry, user_stack);
}
