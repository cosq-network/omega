#ifndef OMEGA_KERNEL_SCHEDULER_HPP
#define OMEGA_KERNEL_SCHEDULER_HPP

#include "std/cstdint.hpp"

namespace process { struct Process; }
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
    uint64_t switches;
    process::Process* process;
};

class Scheduler {
public:
    static void init();
    static Thread* create_thread(void (*entry)());
    static void yield();
    static void schedule();
    static void block_current();
    static void wake(Thread* thread);
    static uintptr_t timer_tick(uintptr_t saved_stack);
    static void run_current();
    [[noreturn]] static void thread_exit();
    static uint64_t tick_count();
    static void attach_current_process(process::Process* process);

private:
    static Thread* current_thread;
    static Thread* thread_list_head;
    static uint32_t next_thread_id;
};

} // namespace scheduler

extern "C" void cpu_switch_context(uintptr_t* old_sp, uintptr_t new_sp);

#endif // OMEGA_KERNEL_SCHEDULER_HPP
