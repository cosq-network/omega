#include "kernel/scheduler.hpp"
#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"

namespace scheduler {

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

    thread_list_head = current_thread;

    kernel::kprintf("[+] Preemptive Multi-threading Scheduler Initialized.\n");
    kernel::kprintf("    Idle Thread ID: 0 Running.\n");
}

Thread* Scheduler::create_thread(void (*entry)()) {
    Thread* new_thread = reinterpret_cast<Thread*>(kmalloc(sizeof(Thread)));
    new_thread->id = next_thread_id++;
    new_thread->state = READY;
    new_thread->entry_point = entry;

    // Allocate 16 KiB Thread Stack
    uintptr_t stack = reinterpret_cast<uintptr_t>(kmalloc(16384)) + 16384;
    new_thread->stack_ptr = stack;

    // Insert into circular thread list
    new_thread->next = thread_list_head->next;
    thread_list_head->next = new_thread;

    kernel::kprintf("[+] Created Thread ID: %u at entry: %x\n", new_thread->id, reinterpret_cast<uintptr_t>(entry));
    return new_thread;
}

void Scheduler::yield() {
    schedule();
}

void Scheduler::schedule() {
    if (!current_thread || !current_thread->next) return;

    Thread* old_thread = current_thread;
    current_thread = current_thread->next;

    if (old_thread != current_thread) {
        current_thread->state = RUNNING;
        if (old_thread->state == RUNNING) {
            old_thread->state = READY;
        }
    }
}

} // namespace scheduler
