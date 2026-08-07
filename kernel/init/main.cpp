#include "kernel/kprint.hpp"
#include "kernel/memory.hpp"
#include "kernel/vmm.hpp"
#include "kernel/heap.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/syscall.hpp"
#include "kernel/vfs.hpp"
#include "kernel/initrd.hpp"
#include "kernel/userland.hpp"
#include "kernel/elf_loader.hpp"
#include "kernel/net.hpp"
#include "arch/uart.hpp"
#include "arch/display.hpp"
#include "arch/interrupts.hpp"
#include "arch/pci.hpp"
#include "kernel/console.hpp"
#include "kernel/framebuffer.hpp"
#include "kernel/fdt.hpp"

// Static Heap Allocation Buffer (1 MB) to guarantee physical memory availability across architectures
static uint8_t kernel_heap_buffer[1024 * 1024] __attribute__((aligned(8)));
#if !defined(__x86_64__)
static uint8_t pmm_bitmap_buffer[4096] __attribute__((aligned(4096)));
#endif

// Synthetic Minimal ELF Header for Testing
static const uint8_t mock_elf_binary[] __attribute__((aligned(8))) = {
    0x7F, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, // e_ident (64-bit)
    2, 0,                                                   // e_type (EXEC)
    62, 0,                                                  // e_machine (x86_64)
    1, 0, 0, 0,                                             // e_version
    0x00, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,         // e_entry (0x401000)
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // e_phoff (64)
    0, 0, 0, 0, 0, 0, 0, 0,                                 // e_shoff
    0, 0, 0, 0,                                             // e_flags
    64, 0,                                                  // e_ehsize
    56, 0,                                                  // e_phentsize
    1, 0,                                                   // e_phnum (1 segment)
    0, 0, 0, 0, 0, 0,                                       // section sizes
    // Program Header (PT_LOAD)
    1, 0, 0, 0,                                             // p_type (PT_LOAD)
    5, 0, 0, 0,                                             // p_flags (R+X)
    0, 0, 0, 0, 0, 0, 0, 0,                                 // p_offset
    0x00, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,         // p_vaddr (0x401000)
    0x00, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,         // p_paddr
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // p_filesz (128B)
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // p_memsz (128B)
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00          // p_align (4KiB)
};

extern "C" void kernel_main(uintptr_t boot_fdt) {
    fdt::set_boot_pointer(boot_fdt);
    // Serial first for early trap/debug output before display is ready.
    hal::uart_init();

    // PMM/VMM must be ready before framebuffer mapping (may lie above 1 GiB).
    // The early RISC-V/AArch64 QEMU handoff does not yet establish a broad
    // physical identity map. Keep the PMM bitmap in kernel-owned RAM until
    // the real VMM is active; x86 retains its historical low-memory address.
#if defined(__x86_64__)
    memory::PhysicalMemoryManager::init(0x200000, 32 * 1024 * 1024);
#else
    memory::PhysicalMemoryManager::init(reinterpret_cast<uintptr_t>(pmm_bitmap_buffer), 32 * 1024 * 1024);
#endif
    memory::VirtualMemoryManager::init();


    hal::Display::init();
    display::Console::init();
    kernel::kprint_enable_console_routing();
    if (hal::Display::capabilities().linear_framebuffer) {
        const auto* fb = hal::Display::framebuffer();
        kernel::kprintf("[+] Display: %s %ux%ux%u\n", hal::Display::backend_name(),
                        fb != nullptr ? fb->width : 0, fb != nullptr ? fb->height : 0,
                        fb != nullptr ? fb->bpp : 0);
    } else {
        kernel::kprintf("[!] Display: No framebuffer backend found (serial only)\n");
    }

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
#elif defined(__riscv)
    kernel::kprintf("[+] Architecture Identified: RISC-V 64-bit (rv64gc)\n");
#else
    kernel::kprintf("[!] Architecture: Unknown\n");
#endif

    if (!hal::Display::run_self_tests()) {
        kernel::kprintf("[!] Display self-tests reported failures\n");
    }
    if (!display::Console::self_test()) {
        kernel::kprintf("[!] Console self-test failed\n");
    } else {
        kernel::kprintf("[TEST][PASS] Display console write path\n");
    }
    if (display::framebuffer_active() && !display::framebuffer_self_test()) {
        kernel::kprintf("[!] Framebuffer self-test failed\n");
    } else if (display::framebuffer_active()) {
        kernel::kprintf("[TEST][PASS] Framebuffer draw path\n");
    }

    // Initialize Heap Allocator using static kernel heap buffer
    memory::HeapAllocator::init(reinterpret_cast<uintptr_t>(kernel_heap_buffer), sizeof(kernel_heap_buffer));

    // Initialize Hardware Interrupt System
    hal::interrupts_init();

    // Initialize Preemptive Thread Scheduler
    scheduler::Scheduler::init();

    // Initialize System Call Engine & POSIX Surface
    syscall::SyscallDispatcher::init();

    // Initialize Virtual Filesystem (VFS)
    vfs::VirtualFilesystem::init();

    // Initialize Initrd RAM Disk at 0x600000
    initrd::Initrd::init(0x600000);

    // Scan PCI Bus Devices
    hal::PciBus::scan();

    // Initialize VirtIO Network Stack
    net::NetworkStack::init();

    // Initialize Userland Privilege System
    userland::UserlandManager::init();

    // Parse and Load ELF Binary
    uintptr_t elf_entry = elf::ElfLoader::load(mock_elf_binary);
    if (elf_entry) {
        kernel::kprintf("[+] ELF Executable Binary Successfully Parsed & Loaded!\n");
    }

    kernel::kprintf("[+] System online. Entering idle loop...\n");

    while (1) {
        // CPU Halt loop
    }
}
