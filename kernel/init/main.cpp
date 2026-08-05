#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"
#include "kernel/vmm.hpp"
#include "kernel/heap.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/syscall.hpp"
#include "kernel/vfs.hpp"
#include "kernel/initrd.hpp"
#include "arch/uart.hpp"
#include "arch/interrupts.hpp"

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

    kernel::kprintf("[+] System online. Entering idle loop...\n");

    while (1) {
        // CPU Halt loop
    }
}
