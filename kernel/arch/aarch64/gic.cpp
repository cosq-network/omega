#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

namespace hal {

extern "C" void el1_vector_table();

void interrupts_init() {
    // Set Vector Base Address Register (VBAR_EL1)
    uint64_t vbar = reinterpret_cast<uint64_t>(el1_vector_table);
    asm volatile("msr vbar_el1, %0" : : "r"(vbar));

    kernel::kprintf("[+] AArch64 Interrupt Vector Table (VBAR_EL1) Initialized.\n");
    kernel::kprintf("    VBAR_EL1 Address: %x\n", vbar);
}

void interrupts_enable() {
    asm volatile("msr daifclr, #0xf");
}

void interrupts_disable() {
    asm volatile("msr daifset, #0xf");
}

} // namespace hal
