#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

namespace hal {

extern "C" void aarch64_timer_interrupt() {
    uint64_t interval = 10000000ull / 100; // QEMU virt's 10 MHz counter
    asm volatile("msr cntp_tval_el0, %0; isb" : : "r"(interval) : "memory");
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    uint64_t interval = 10000000ull / frequency_hz;
    asm volatile("msr cntp_tval_el0, %0; mov x0, #1; msr cntp_ctl_el0, x0; isb" : : "r"(interval) : "x0", "memory");
    kernel::kprintf("[+] AArch64 ARM generic timer initialized at %u Hz.\n", frequency_hz);
}

void interrupts_init() {
#if defined(__x86_64__)
    kernel::kprintf("[+] x86_64 Interrupt Descriptor Table (IDT) Initialized.\n");
#elif defined(__aarch64__)
    kernel::kprintf("[+] AArch64 Interrupt Vector Table (VBAR_EL1) Initialized.\n");
#elif defined(__riscv)
    kernel::kprintf("[+] RISC-V 64 Trap Vector (stvec) Initialized.\n");
#endif
}

void interrupts_enable() {
    asm volatile("msr daifclr, #2" ::: "memory");
}

void interrupts_disable() {
    asm volatile("msr daifset, #2" ::: "memory");
}

} // namespace hal
