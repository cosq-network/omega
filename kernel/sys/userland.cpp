#include "kernel/userland.hpp"
#include "kernel/kprint.hpp"
#include "kernel/syscall.hpp"
#include "kernel/process.hpp"
#include "kernel/scheduler.hpp"

namespace hal {
extern "C" void aarch64_timer_interrupt();
extern "C" void riscv_timer_interrupt();
}

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
    if ((error & 2) && process::Manager::handle_cow_fault(fault_address)) return;
    kernel::kprintf("[PANIC] x86 page fault addr=%x error=%x rip=%x\n",
                    fault_address, error, rip);
}
#endif

#if defined(__aarch64__) || defined(__riscv)
namespace {
alignas(16) static uint8_t exception_stack[16 * 1024];
}
#endif

#if defined(__aarch64__)
extern "C" void aarch64_kernel_sync_fault(uint64_t esr, uint64_t far) {
    kernel::kprintf("[PANIC] AArch64 EL1 sync fault ESR=%x FAR=%x\n", esr, far);
}

extern "C" void aarch64_prepare_exception_stack(uintptr_t);
extern "C" uintptr_t aarch64_exception_handler(uintptr_t* frame) {
    uint64_t esr; asm volatile("mrs %0, esr_el1" : "=r"(esr));
    const uint32_t ec = static_cast<uint32_t>(esr >> 26);
    if (ec == 0x15) {
        frame[0] = static_cast<uint64_t>(syscall::SyscallDispatcher::dispatch6(
            frame[8], frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]));
        uint64_t pc; asm volatile("mrs %0, elr_el1" : "=r"(pc));
        asm volatile("msr elr_el1, %0" : : "r"(pc + 4) : "memory");
    } else if (ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) {
        uint64_t far; asm volatile("mrs %0, far_el1" : "=r"(far));
        if ((ec == 0x24 || ec == 0x25) && process::Manager::handle_cow_fault(far)) return 0;
        kernel::kprintf("[!] AArch64 EL0 %s fault at %x (ESR %x)\n",
                        (ec == 0x20 || ec == 0x21) ? "instruction" : "data", far, esr);
    } else {
        (void)scheduler::Scheduler::timer_tick(reinterpret_cast<uintptr_t>(frame));
        hal::aarch64_timer_interrupt();
    }
    return 0;
}
#elif defined(__riscv)
extern "C" void riscv_prepare_exception_stack(uintptr_t);
extern "C" uintptr_t riscv_exception_handler(uintptr_t* frame) {
    const uint64_t cause = frame[33];
    if ((cause >> 63) != 0 && (cause & 0xfff) == 5) {
        (void)scheduler::Scheduler::timer_tick(reinterpret_cast<uintptr_t>(frame));
        hal::riscv_timer_interrupt();
    } else if (cause == 8) {
        frame[10] = static_cast<uint64_t>(syscall::SyscallDispatcher::dispatch6(
            frame[17], frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]));
        frame[32] += 4;
    } else if (cause == 12 || cause == 13 || cause == 15) {
        if (cause == 15 && process::Manager::handle_cow_fault(frame[34])) return 0;
        kernel::kprintf("[!] RISC-V U-mode %s page fault at %x pc=%x\n",
                        cause == 12 ? "instruction" : cause == 13 ? "load" : "store", frame[34], frame[32]);
    } else {
        kernel::kprintf("[!] RISC-V unexpected user trap cause %u\n", cause);
    }
    return 0;
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

void UserlandManager::init_aarch64_exception_stack() {
#if defined(__aarch64__)
    aarch64_prepare_exception_stack(reinterpret_cast<uintptr_t>(exception_stack) + sizeof(exception_stack));
#endif
}

void UserlandManager::init_riscv_exception_stack() {
#if defined(__riscv)
    riscv_prepare_exception_stack(reinterpret_cast<uintptr_t>(exception_stack) + sizeof(exception_stack));
#endif
}

void UserlandManager::enter_userland(uintptr_t user_entry, uintptr_t user_stack) {
#if defined(__x86_64__)
    kernel::kprintf("[+] Entering x86_64 Ring 3 init (Entry: %x, Stack: %x)\n", user_entry, user_stack);
    x86_enter_userland(user_entry, user_stack);
#elif defined(__aarch64__)
    kernel::kprintf("[+] Entering AArch64 EL0 init (Entry: %x, Stack: %x)\n", user_entry, user_stack);
    aarch64_enter_userland(user_entry, user_stack);
#elif defined(__riscv)
    kernel::kprintf("[+] Entering RISC-V U-mode init (Entry: %x, Stack: %x)\n", user_entry, user_stack);
    riscv_enter_userland(user_entry, user_stack);
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
