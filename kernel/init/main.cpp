#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"
#include "kernel/vmm.hpp"
#include "kernel/heap.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/syscall.hpp"
#include "kernel/vfs.hpp"
#include "kernel/initrd.hpp"
#include "kernel/userland.hpp"
#include "arch/uart.hpp"
#include "arch/interrupts.hpp"

static void simulated_user_program() {
    const char msg[] = "    [Userland Application] Hello from Ring 3 / EL0!\n";
    sys_call(syscall::SYS_WRITE, reinterpret_cast<uint64_t>(msg), sizeof(msg) - 1, 0);
    sys_call(syscall::SYS_EXIT, 0, 0, 0);
}

extern "C" void jump_to_userland(uintptr_t user_entry, uintptr_t user_stack) {
    (void)user_stack;
    // Execute simulated userland program entry point
    auto fn = reinterpret_cast<void(*)()>(user_entry);
    fn();
}

extern "C" void kernel_main() {
    // Initialize UART hardware
    hal::uart_init();

    // Print welcome Banner
    kernel::kprintf("\n==========================================\n");
    kernel::kprintf("      Welcome to Omega Kernel v0.1        \n");
    kernel::kprintf("  Freestanding C++ Microkernel Architecture \n");
    kernel::kprintf("==========================================\n\n");

    kernel::kprintf("[+] UART Serial Console Initialized successfully.\n");
    kernel::kprintf("[+] Kernel Entry Point Reached.\n");

#if defined(__x86_64__)
    kernel::kprintf("[+] Architecture Identified: x86_64 (64-bit)\n");
#elif defined(__aarch64__)
    kernel::kprintf("[+] Architecture Identified: AArch64 (ARM 64-bit)\n");
#else
    kernel::kprintf("[!] Architecture: Unknown\n");
#endif

    // Initialize Physical Memory Manager (Simulating 32MB physical RAM at 2MB offset)
    memory::PhysicalMemoryManager::init(0x200000, 32 * 1024 * 1024);

    // Initialize Virtual Memory Manager
    memory::VirtualMemoryManager::init();

    // Initialize Heap Allocator (1MB Heap Buffer at 0x500000)
    memory::HeapAllocator::init(0x500000, 1024 * 1024);

    // Initialize Hardware Interrupt System
    hal::interrupts_init();

    // Initialize Preemptive Thread Scheduler
    scheduler::Scheduler::init();

    // Initialize System Call Engine
    syscall::SyscallDispatcher::init();

    // Initialize Virtual Filesystem (VFS)
    vfs::VirtualFilesystem::init();

    // Initialize Initrd RAM Disk at 0x600000
    initrd::Initrd::init(0x600000);

    // Initialize Userland Privilege System
    userland::UserlandManager::init();

    // Test Jump to Userland Execution Space
    uintptr_t user_stack = reinterpret_cast<uintptr_t>(kmalloc(16384)) + 16384;
    userland::UserlandManager::enter_userland(reinterpret_cast<uintptr_t>(simulated_user_program), user_stack);

    kernel::kprintf("[+] System online. Entering idle loop...\n");

    while (1) {
        // CPU Halt loop
    }
}
