#include "kernel/scheduler.hpp"

extern "C" uintptr_t x86_timer_interrupt(uintptr_t saved_stack) {
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x20)), "Nd"(0x20));
    return scheduler::Scheduler::timer_tick(saved_stack);
}
