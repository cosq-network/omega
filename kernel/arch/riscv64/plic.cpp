#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

namespace hal {

extern "C" void trap_entry();

void interrupts_init() {
    // Set Supervisor Trap Vector Base Address Register (stvec)
    uint64_t stvec = reinterpret_cast<uint64_t>(trap_entry);
    asm volatile("csrw stvec, %0" : : "r"(stvec));

    kernel::kprintf("[+] RISC-V 64 Trap Vector (stvec) Initialized.\n");
    kernel::kprintf("    stvec Base Address: %x\n", stvec);
}

void interrupts_enable() {
    asm volatile("csrs sstatus, 2"); // Set SIE bit in sstatus
}

void interrupts_disable() {
    asm volatile("csrc sstatus, 2"); // Clear SIE bit in sstatus
}

} // namespace hal
