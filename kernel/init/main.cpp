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
#include "kernel/storage.hpp"
#include "kernel/virtio_blk.hpp"
#include "kernel/ext4.hpp"
#include "kernel/process.hpp"
#include "kernel/security.hpp"
#include "kernel/input.hpp"
#include "arch/input.hpp"

// Static Heap Allocation Buffer (1 MB) to guarantee physical memory availability across architectures
static uint8_t kernel_heap_buffer[1024 * 1024] __attribute__((aligned(8)));
#if !defined(__x86_64__)
static uint8_t pmm_bitmap_buffer[4096] __attribute__((aligned(4096)));
#endif

#if defined(OMEGA_ENABLE_SCHEDULER_SELF_TEST) && defined(__x86_64__)
static volatile uint32_t scheduler_test_a = 0;
static volatile uint32_t scheduler_test_b = 0;
static volatile bool scheduler_test_yield_seen = false;
static volatile bool scheduler_test_preempt_logged = false;
static volatile bool scheduler_test_context_logged = false;
static volatile bool scheduler_test_tick_logged = false;

static void scheduler_test_thread_a() {
    for (;;) {
        scheduler_test_a = scheduler_test_a + 1;
        if (scheduler_test_a == 1) {
            scheduler::Scheduler::yield();
            scheduler_test_yield_seen = true;
        }
        if (!scheduler_test_preempt_logged && scheduler_test_a >= 5 && scheduler_test_b >= 5 && scheduler_test_yield_seen) {
            scheduler_test_preempt_logged = true;
            kernel::kprintf("[TEST][PASS] PIT timer preempted two kernel threads\n");
        }
        if (!scheduler_test_tick_logged && scheduler::Scheduler::tick_count() >= 20) {
            scheduler_test_tick_logged = true;
            kernel::kprintf("[TEST][PASS] Timer tick rate observed (%u ticks)\n",
                            static_cast<uint32_t>(scheduler::Scheduler::tick_count()));
        }
        asm volatile("hlt");
    }
}

static void scheduler_test_thread_b() {
    for (;;) {
        scheduler_test_b = scheduler_test_b + 1;
        if (!scheduler_test_context_logged && scheduler_test_a >= 5 && scheduler_test_b >= 5 && scheduler_test_yield_seen) {
            scheduler_test_context_logged = true;
            kernel::kprintf("[TEST][PASS] Timer context-switch state preserved\n");
        }
        asm volatile("hlt");
    }
}
#endif

// Synthetic Minimal ELF Header for Testing
static const uint8_t mock_elf_binary[] __attribute__((aligned(8))) = {
    0x7F, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, // e_ident (64-bit)
    2, 0,                                                   // e_type (EXEC)
#if defined(__x86_64__)
    62, 0,                                                  // e_machine (x86_64)
#elif defined(__aarch64__)
    183, 0,                                                 // e_machine (AArch64)
#else
    243, 0,                                                 // e_machine (RISC-V)
#endif
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
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // p_filesz (empty synthetic payload)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // p_memsz (empty synthetic payload)
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00          // p_align (4KiB)
};

extern "C" void kernel_main(uintptr_t boot_fdt) {
    fdt::set_boot_pointer(boot_fdt);
    // Serial first for early trap/debug output before display is ready.
    hal::uart_init();
#if defined(__aarch64__)
    // Some direct AArch64 QEMU loaders do not populate x0. Search the reserved
    // low-RAM handoff window, accepting only a structurally valid DTB.
    if (boot_fdt == 0) {
        for (uintptr_t candidate = 0x40000000ull; candidate < 0x48000000ull; candidate += 0x1000) {
            if (fdt::is_valid_blob(candidate)) { boot_fdt = candidate; break; }
        }
    }
#endif

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

    security::Manager::init();
    if (security::Manager::self_test() == 0) {
        kernel::kprintf("[TEST][PASS] Linux UID/GID/group and mode permission matrix\n");
    }

    process::Manager::init();
    if (process::Manager::self_test() == 0) {
        kernel::kprintf("[TEST][PASS] Process address-space map/unmap path\n");
    }

    // Initialize the common storage layer before hardware drivers and VFS
    // mounts. The memory-backed device validates the request/lifecycle API.
    storage::Manager::init();
    if (storage::memory_block::init() == storage::Status::Success && storage::Manager::self_test()) {
        kernel::kprintf("[TEST][PASS] Storage core memory block path\n");
        kernel::kprintf("[TEST][PASS] Storage write and flush policy\n");
    } else {
        kernel::kprintf("[TEST][FAIL] Storage core memory block path\n");
    }
    virtio_blk::init();

    // Initialize Hardware Interrupt System
    hal::interrupts_init();
    input::Manager::init();
    hal::input_init();
    if (input::Manager::self_test()) {
        kernel::kprintf("[TEST][PASS] Input ABI and boot keyboard decoder\n");
    } else {
        kernel::kprintf("[TEST][FAIL] Input ABI and boot keyboard decoder\n");
    }

    // Initialize Preemptive Thread Scheduler
    scheduler::Scheduler::init();
#if defined(__x86_64__)
    hal::timer_init(100);
#if defined(OMEGA_ENABLE_SCHEDULER_SELF_TEST)
    scheduler::Scheduler::create_thread(scheduler_test_thread_a);
    scheduler::Scheduler::create_thread(scheduler_test_thread_b);
#endif
#endif
#if defined(__aarch64__) || defined(__riscv)
    hal::timer_init(100);
#endif

    // Initialize System Call Engine & POSIX Surface
    syscall::SyscallDispatcher::init();

    // Initialize Virtual Filesystem (VFS)
    vfs::VirtualFilesystem::init();
    if (ext4::mount(storage::Manager::find_by_name("virtio0"), nullptr) == storage::Status::Success) {
        kernel::kprintf("[TEST][PASS] ext4 root filesystem mounted\n");
    }

    // QEMU places initrd images in the platform RAM window.
#if defined(__riscv)
    initrd::Initrd::init(0x81000000ull);
#elif defined(__aarch64__)
    initrd::Initrd::init(0x44000000ull);
#else
    initrd::Initrd::init(0x600000ull);
#endif

    // Scan PCI Bus Devices
    hal::PciBus::scan();

    // Initialize VirtIO Network Stack
    net::NetworkStack::init();

    // Initialize Userland Privilege System
    userland::UserlandManager::init();

    // Replace the bring-up mock with the first real Omega userspace process.
    // Every reference architecture consumes the same /init ELF and enters
    // its native least-privileged execution level.
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
    auto* init_file = initrd::Initrd::find("/init");
    auto* init_process = process::Manager::current();
    uintptr_t user_entry = 0;
    uintptr_t user_stack = 0;
    if (init_file != nullptr && init_process != nullptr &&
        elf::ElfLoader::load_into(init_process,
                                  reinterpret_cast<const uint8_t*>(init_file->fs_data),
                                  init_file->size, &user_entry, &user_stack) &&
        process::Manager::activate(init_process)) {
        kernel::kprintf("[+] ELF PT_LOAD segments mapped for /init.\n");
        kernel::kprintf("[TEST][PASS] PID 1 userspace address space activated\n");
        userland::UserlandManager::init_x86_syscall_stack();
#if defined(__aarch64__)
        userland::UserlandManager::init_aarch64_exception_stack();
#elif defined(__riscv)
        userland::UserlandManager::init_riscv_exception_stack();
#endif
        userland::UserlandManager::enter_userland(user_entry, user_stack);
    } else {
        kernel::kprintf("[!] /init unavailable; staying in kernel idle loop.\n");
        uintptr_t elf_entry = elf::ElfLoader::load(mock_elf_binary, sizeof(mock_elf_binary));
        if (elf_entry) kernel::kprintf("[+] ELF Executable Binary Successfully Parsed & Loaded!\n");
    }
#endif

    kernel::kprintf("[+] System online. Entering idle loop...\n");

    // Leave the completed boot screen branded with the Omega mark. The
    // framebuffer path is optional; serial and text-mode boots are unchanged.
    display::draw_omega_logo();
    hal::Display::flush();

#if defined(__x86_64__)
    hal::interrupts_enable();
#endif

    while (1) {
        hal::input_poll();
        // The x86 PIT drives timer_tick() while the idle thread sleeps.
        // Other architectures retain their existing bring-up idle behavior.
#if defined(__x86_64__)
        asm volatile("hlt");
#endif
    }
}
