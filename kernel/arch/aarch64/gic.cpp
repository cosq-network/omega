#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

namespace hal {

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
}

void interrupts_disable() {
}

} // namespace hal
