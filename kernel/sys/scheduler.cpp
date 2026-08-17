#include "kernel/scheduler.hpp"
#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"
#include "kernel/process.hpp"

namespace scheduler {

namespace {
constexpr uintptr_t STACK_SIZE = 16 * 1024;
#if defined(__x86_64__)
constexpr uint64_t KERNEL_CS = 0x08;
constexpr uint64_t KERNEL_SS = 0x10;
constexpr uint64_t INTERRUPTS_ENABLED = 1ull << 9;
#endif
uint64_t timer_ticks = 0;
}

extern "C" [[noreturn]] void thread_bootstrap() {
    Scheduler::run_current();
    Scheduler::thread_exit();
    for (;;) asm volatile("" ::: "memory");
}

Thread* Scheduler::current_thread = nullptr;
Thread* Scheduler::thread_list_head = nullptr;
uint32_t Scheduler::next_thread_id = 1;

void Scheduler::init() {
    // Create Main Kernel Idle Thread
    current_thread = reinterpret_cast<Thread*>(kmalloc(sizeof(Thread)));
    current_thread->id = 0;
    current_thread->state = RUNNING;
    current_thread->stack_ptr = 0;
    current_thread->entry_point = nullptr;
    current_thread->next = current_thread; // Circular list
    current_thread->switches = 0;
    current_thread->process = nullptr;

    thread_list_head = current_thread;

    kernel::kprintf("[+] Preemptive Multi-threading Scheduler Initialized.\n");
    kernel::kprintf("    Idle Thread ID: 0 Running.\n");
}

Thread* Scheduler::create_thread(void (*entry)()) {
    Thread* new_thread = reinterpret_cast<Thread*>(kmalloc(sizeof(Thread)));
    new_thread->id = next_thread_id++;
    new_thread->state = READY;
    new_thread->entry_point = entry;
    new_thread->switches = 0;
    new_thread->process = nullptr;

    // Allocate 16 KiB Thread Stack
    uintptr_t stack = reinterpret_cast<uintptr_t>(kmalloc(STACK_SIZE)) + STACK_SIZE;
    stack &= ~static_cast<uintptr_t>(0xFul);
#if defined(__x86_64__)
    // timer.s restores 15 general registers, then returns through iretq.
    // Build that exact frame for a thread that has never run.
    auto* frame = reinterpret_cast<uint64_t*>(stack) - 20;
    for (int i = 0; i < 15; ++i) frame[i] = 0;
    frame[15] = reinterpret_cast<uintptr_t>(&thread_bootstrap);
    frame[16] = KERNEL_CS;
    frame[17] = INTERRUPTS_ENABLED;
    frame[18] = stack;
    frame[19] = KERNEL_SS;
    new_thread->stack_ptr = reinterpret_cast<uintptr_t>(frame);
#elif defined(__aarch64__)
    // lower_irq restores this frame and returns with eret.  M=0x5 selects
    // EL1h so a rescue/kernel thread never re-enters EL0 after a user fault.
    auto* frame = reinterpret_cast<uint64_t*>(stack) - 34;
    for (int i = 0; i < 34; ++i) frame[i] = 0;
    frame[32] = reinterpret_cast<uintptr_t>(&thread_bootstrap); // elr_el1
    frame[33] = 0x5; // SPSR_EL1: EL1h
    new_thread->stack_ptr = reinterpret_cast<uintptr_t>(frame);
#elif defined(__riscv)
    // trap_entry restores this frame and sret returns to S-mode (SPP=1).
    auto* frame = reinterpret_cast<uint64_t*>(stack) - 37;
    for (int i = 0; i < 37; ++i) frame[i] = 0;
    frame[0] = stack;
    frame[32] = reinterpret_cast<uintptr_t>(&thread_bootstrap); // sepc
    frame[35] = (1ull << 8) | (1ull << 5); // SPP=1, SPIE=1
    new_thread->stack_ptr = reinterpret_cast<uintptr_t>(frame);
#else
    new_thread->stack_ptr = stack;
#endif

    // Insert into circular thread list
    new_thread->next = thread_list_head->next;
    thread_list_head->next = new_thread;

    kernel::kprintf("[+] Created Thread ID: %u at entry: %x\n", new_thread->id, reinterpret_cast<uintptr_t>(entry));
    return new_thread;
}

void Scheduler::yield() {
#if defined(__x86_64__)
    // Vector 32 uses the same saved-register/iretq frame as a hardware PIT
    // tick, so cooperative and timer-driven switches share one path.
    asm volatile("int $32" ::: "memory");
#else
    schedule();
#endif
}

void Scheduler::schedule() {
    if (!current_thread || !current_thread->next) return;

    Thread* old_thread = current_thread;
    Thread* candidate = current_thread->next;
    while (candidate != old_thread &&
           candidate->state != READY && candidate->state != RUNNING) {
        candidate = candidate->next;
    }
    current_thread = candidate;

    if (old_thread != current_thread) {
        current_thread->switches++;
        current_thread->state = RUNNING;
        if (old_thread->state == RUNNING) {
            old_thread->state = READY;
        }
    }
}

void Scheduler::block_current() {
    if (current_thread == nullptr) return;

    // Never leave the scheduler with no runnable thread. The idle thread is
    // normally the fallback, but this also makes the primitive safe during
    // early bring-up and in single-thread tests.
    Thread* candidate = current_thread->next;
    while (candidate != current_thread &&
           candidate->state != READY && candidate->state != RUNNING) {
        candidate = candidate->next;
    }
    if (candidate == current_thread) return;

    current_thread->state = BLOCKED;
    yield();
}

void Scheduler::wake(Thread* thread) {
    if (thread != nullptr && thread->state == BLOCKED) {
        thread->state = READY;
    }
}

uintptr_t Scheduler::timer_tick(uintptr_t saved_stack) {
    ++timer_ticks;
    if (current_thread == nullptr) return saved_stack;
    current_thread->stack_ptr = saved_stack;
    Thread* old = current_thread;
    schedule();
    if (old != current_thread) {
        if (current_thread->process != nullptr) {
            (void)process::Manager::activate(current_thread->process);
        }
        return current_thread->stack_ptr;
    }
    return saved_stack;
}

void Scheduler::terminate_current() {
    if (current_thread != nullptr) current_thread->state = TERMINATED;
}

void Scheduler::attach_current_process(process::Process* process) {
    if (current_thread != nullptr) current_thread->process = process;
}

void Scheduler::run_current() {
    if (current_thread != nullptr && current_thread->entry_point != nullptr) {
        current_thread->entry_point();
    }
}

[[noreturn]] void Scheduler::thread_exit() {
    if (current_thread == nullptr) {
        for (;;) asm volatile("");
    }
    current_thread->state = TERMINATED;
    yield();
    // A terminated thread must never resume. This also covers the no-runnable
    // fallback on architectures that do not yet have native timer switching.
    for (;;) asm volatile("" ::: "memory");
}

uint64_t Scheduler::tick_count() { return timer_ticks; }

} // namespace scheduler
