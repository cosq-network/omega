#ifndef OMEGA_KERNEL_SCHEDULER_HPP
#define OMEGA_KERNEL_SCHEDULER_HPP

#include "std/cstdint.hpp"

namespace scheduler {

enum ThreadState {
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
};

struct CpuContext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
};

struct Thread {
    uint32_t id;
    ThreadState state;
    uintptr_t stack_ptr;
    void (*entry_point)();
    Thread* next;
};

class Scheduler {
public:
    static void init();
    static Thread* create_thread(void (*entry)());
    static void yield();
    static void schedule();

private:
    static Thread* current_thread;
    static Thread* thread_list_head;
    static uint32_t next_thread_id;
};

} // namespace scheduler

extern "C" void cpu_switch_context(uintptr_t* old_sp, uintptr_t new_sp);

#endif // OMEGA_KERNEL_SCHEDULER_HPP
