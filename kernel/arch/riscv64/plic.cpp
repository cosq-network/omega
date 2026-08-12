#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

namespace hal {

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    uint64_t now;
    asm volatile("rdtime %0" : "=r"(now));
    const uint64_t interval = 10000000ull / frequency_hz;
    register uint64_t a0 asm("a0") = now + interval;
    register uint64_t a7 asm("a7") = 0; // SBI legacy set_timer
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    asm volatile("csrs sie, %0" : : "r"(static_cast<uint64_t>(1ull << 5)) : "memory");
    kernel::kprintf("[+] RISC-V SBI timer initialized at %u Hz.\n", frequency_hz);
}

extern "C" void riscv_timer_interrupt() {
    uint64_t now;
    asm volatile("rdtime %0" : "=r"(now));
    register uint64_t a0 asm("a0") = now + 100000ull;
    register uint64_t a7 asm("a7") = 0;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

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
