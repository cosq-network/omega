# Omega Kernel

**Omega** is a lightweight, high-performance, freestanding C++20 Unix-like microkernel core designed to cross-compile natively on macOS (Apple Silicon M1/M2/M3) using Clang and LLVM (`ld.lld`) for both **x86_64** (x64) and **AArch64** (ARM64) target architectures.

---

## 🌟 Key Features & Subsystem Overview

- **Natively Cross-Compiled on macOS (Apple Silicon M1/M2/M3)**: Built using Homebrew LLVM (`clang++` + `ld.lld`) and CMake toolchain integration without external GCC cross-compiler dependencies.
- **Dual Architecture Support**:
  - **x86_64 (x64)**: 64-bit Long Mode entry, PAE paging, PML4 4-level page tables (2MB Huge Pages identity mapping), GDT loading, and Xen PVH ELF note (`.xen_note`) for direct QEMU booting.
  - **AArch64 (ARM64)**: Dynamic Exception Level transition (`EL2 -> EL1`), 2048-byte aligned `VBAR_EL1` vector table, and `SP_EL1` stack setup.
- **Freestanding C++20 Runtime**: Independent of host C/C++ standard libraries (`-ffreestanding -fno-exceptions -fno-rtti`). Implements freestanding memory primitives (`memcpy`, `memset`, `memmove`, `memcmp`) and vararg printing (`kprintf`).
- **Physical Memory Manager (PMM)**: 4KiB Bitmap Frame Allocator tracking physical page frames (`alloc_frame` / `free_frame`).
- **Virtual Memory Manager (VMM)**: 4-Level Page Table Mapping Engine interacting with architectural base registers (`CR3` on x86_64, `TTBR0_EL1` on AArch64).
- **Dynamic Kernel Heap Allocator**: Free-list block header allocator providing standard C ABI bindings (`kmalloc` / `kfree`) with 8-byte alignment and block coalescing.
- **Hardware Interrupt Architecture**: 256-entry Interrupt Descriptor Table (IDT) for x86_64 and System Vector Base (`VBAR_EL1`) for AArch64.
- **Preemptive Multi-threading Scheduler**: Circular linked list Thread Control Block (TCB) engine executing round-robin thread yields and context switching.
- **System Call ABI Dispatcher**: Kernel system call interface handling register argument passing for `SYS_WRITE`, `SYS_YIELD`, and `SYS_EXIT`.
- **Virtual Filesystem (VFS) & Initrd RAM Disk**: POSIX-like node tree structure (`/` root node) and memory-backed initial RAM disk file reader.

---

## ⚙️ Detailed Technical Implementation Specifications

### 1. Bootstrapping & Architectural Handover
- **x86_64 (`kernel/arch/x86_64/boot.s`)**:
  - The entry point `_start` is executed in 32-bit Protected Mode by the bootloader.
  - Constructs initial 4-level page tables (`pml4`, `pdpt`, `pd`) identity-mapping the first 1 GB of physical address space using 2 MB Huge Pages (Setting bit 7 `0x83`).
  - Enables Physical Address Extension (`CR4.PAE = 1`), activates Long Mode via the `EFER` MSR (`0xC0000080` bit 8 `LME`), and enables paging in `CR0.PG`.
  - Loads a 64-bit Global Descriptor Table (`gdt64`) containing 64-bit Code (`0x08`) and Data (`0x10`) selectors, performs a far jump (`ljmp $0x08, $long_mode_start`), reloads segment registers (`ds`, `es`, `fs`, `gs`, `ss`), sets up the 16 KiB stack, and calls `kernel_main()`.
  - Includes a Xen PVH ELF Note (`.xen_note` section with `PT_NOTE` program header) to enable QEMU direct `-kernel` loading without requiring GRUB/PVH ISO wrapper images.
- **AArch64 (`kernel/arch/aarch64/boot.s` & `vectors.s`)**:
  - Checks CPU Exception Level via `CurrentEL`. If executing in EL2 (hypervisor mode), configures `HCR_EL2.RW = 1` for 64-bit EL1 execution, sets up `SPSR_EL2` to target `EL1h` (SP_EL1), and executes `eret` to transition to EL1.
  - Sets up the Exception Vector Base Address Register (`VBAR_EL1`) pointing to a 2048-byte aligned (`.align 11`) 16-entry exception table (`el1_vector_table`).
  - Enables Floating Point and SIMD register access in EL1 by setting `CPACR_EL1.FPEN = 0b11`.

### 2. Memory Management Subsystems
- **Freestanding C Runtime (`kernel/sys/memory.cpp`)**:
  - Provides compiler required memory primitives: `memcpy`, `memset`, `memmove` (handling overlapping memory regions), and `memcmp`.
- **Physical Memory Manager (`kernel/sys/pmm.cpp`)**:
  - Implements a bitmap-based frame allocator where each bit represents a physical 4KiB page frame (`0 = free`, `1 = allocated`).
  - Functions: `init(mem_start, mem_size)`, `alloc_frame()`, `free_frame(frame_addr)`, `get_free_frames()`, `get_total_frames()`.
- **Virtual Memory Manager (`kernel/sys/vmm.cpp`)**:
  - Interacts directly with target page table registers (`CR3` on x86_64 and `TTBR0_EL1` on AArch64).
  - Handles virtual page to physical frame mapping (`map_page`), unmapping (`unmap_page`), and page flag configuration (`PAGE_PRESENT`, `PAGE_WRITABLE`, `PAGE_USER`).
- **Dynamic Kernel Heap Allocator (`kernel/sys/heap.cpp`)**:
  - Implements a free-list block header allocator (`BlockHeader { size, is_free, next }`) managing a 1 MB heap buffer.
  - Performs 8-byte boundary size alignment, dynamic memory block splitting during allocation (`kmalloc`), and adjacent free block merging during deallocation (`kfree`).

### 3. Interrupts, Scheduler & Syscalls
- **Interrupt Descriptor Table (`kernel/arch/x86_64/idt.cpp`)**:
  - Configures a 256-entry `IdtEntry` array populated with 64-bit Ring 0 interrupt gate attributes (`0x8E`) and loads the `idtr` register via `lidt`.
- **Preemptive Scheduler (`kernel/sys/scheduler.cpp`)**:
  - Tracks processes via `Thread` control blocks (`id`, `state`, `stack_ptr`, `entry_point`, `next`).
  - Allocates 16 KiB independent stacks per thread and maintains a circular linked list for round-robin execution and cooperative yielding (`Scheduler::yield()`).
- **System Call Engine (`kernel/sys/syscall.cpp`)**:
  - Implements a C ABI dispatcher (`sys_call(sys_num, arg1, arg2, arg3)`) mapping syscall numbers: `SYS_YIELD` (1), `SYS_WRITE` (2), `SYS_EXIT` (3).

### 4. Filesystem & Storage
- **Virtual Filesystem (`kernel/sys/vfs.cpp`)**:
  - Defines `VfsNode` with operation function pointers (`read`, `write`, `finddir`) and mounts root node `'/'`.
- **RAM Disk Initrd (`kernel/sys/initrd.cpp`)**:
  - Implements an initial RAM disk memory driver parsing file entry headers (`InitrdHeader` & `InitrdFileHeader`) and providing memory offset file reading (`initrd_read`).

---

## 📁 Repository Directory Structure

```text
omega/
├── CMakeLists.txt                 # Master CMake cross-compilation target script
├── cmake/
│   ├── x86_64-toolchain.cmake     # x86_64 toolchain configuration
│   └── aarch64-toolchain.cmake    # AArch64 toolchain configuration
├── docs/
│   ├── ARCHITECTURE.md            # Architectural Specification
│   ├── ROADMAP.md                 # Multi-Phase Implementation Plan
│   ├── RUNNING.md                 # QEMU Execution & Build Guide
│   ├── COMPLETION_REPORT.md       # Architectural Verification Report
│   └── SUMMARY.md                 # Project Completion Summary
└── kernel/
    ├── arch/
    │   ├── x86_64/
    │   │   ├── boot.s             # Long mode page tables, GDT, Xen PVH note
    │   │   ├── serial.cpp         # COM1 serial UART driver
    │   │   ├── idt.cpp            # Interrupt Descriptor Table driver
    │   │   └── linker.ld          # x86_64 ELF linker script
    │   └── aarch64/
    │       ├── boot.s             # EL2->EL1 drop, VBAR_EL1, stack setup
    │       ├── vectors.s          # 2048-byte aligned vector table
    │       ├── uart.cpp           # PL011 UART driver
    │       ├── gic.cpp            # Vector base driver
    │       └── linker.ld          # AArch64 ELF linker script
    ├── include/
    │   ├── arch/                  # HAL interfaces (uart, interrupts)
    │   ├── kernel/                # Core subsystems (kprint, memory, vmm, heap, scheduler, syscall, vfs, initrd)
    │   └── std/                   # Freestanding C++ type definitions
    ├── init/
    │   └── main.cpp               # C++ Kernel entry point
    └── sys/
        ├── memory.cpp             # Freestanding C routines (memcpy, memset, memmove, memcmp)
        ├── kprint.cpp             # Serial formatted printing (kprintf)
        ├── pmm.cpp                # Bitmap physical frame allocator
        ├── vmm.cpp                # Virtual memory mapping engine
        ├── heap.cpp               # Dynamic kmalloc/kfree allocator
        ├── scheduler.cpp          # Round-robin thread scheduler
        ├── syscall.cpp            # System call ABI dispatcher
        ├── vfs.cpp                # Virtual filesystem node interface
        └── initrd.cpp             # RAM disk memory file driver
```

---

## 🛠️ Prerequisites & Setup (macOS M1/M2/M3)

To compile and emulate the Omega kernel, install LLVM and QEMU via Homebrew:

```bash
brew install llvm cmake ninja qemu
```

---

## 🚀 Building and Running

### 1. Build and Run for x86_64 (x64)
```bash
# Create build directory and compile
mkdir -p build/x86_64 && cd build/x86_64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../..
make

# Run in QEMU
qemu-system-x86_64 -kernel omega.elf -serial stdio -display none
```

### 2. Build and Run for AArch64 (ARM64)
```bash
# Create build directory and compile
mkdir -p build/aarch64 && cd build/aarch64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/aarch64-toolchain.cmake -DARCH=aarch64 ../..
make

# Run in QEMU
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel omega.elf
```

---

## 📊 System Execution Output

When booted in QEMU, the kernel initializes hardware drivers, memory management, interrupts, thread scheduler, syscall ABI, and filesystems:

```text
==========================================
      Welcome to Omega Kernel v0.1        
  Freestanding C++ Microkernel Architecture 
==========================================

[+] UART Serial Console Initialized successfully.
[+] Kernel Entry Point Reached.
[+] Architecture Identified: x86_64 (64-bit)
[+] Physical Memory Manager initialized.
    Total Frames: 8192 (32 MB)
    Bitmap Size: 1024 bytes (1 frames)
[+] Virtual Memory Manager (VMM) initialized.
    Page Table Base Register: 0x106000
[+] Kernel Heap Allocator initialized.
    Heap Start: 0x500000, Total Heap Size: 1048576 bytes
[+] x86_64 Interrupt Descriptor Table (IDT) Initialized.
    IDT Base: 0x10D010, Limit: 4095
[+] Preemptive Multi-threading Scheduler Initialized.
    Idle Thread ID: 0 Running.
[+] System Call ABI Engine Initialized.
[+] Virtual Filesystem (VFS) Initialized.
    Root Node '/' Mounted.
[+] RAM Disk (Initrd) Initialized at location: 0x600000
    Total Ramdisk Files: 0
[+] System online. Entering idle loop...
```

---

## 📜 License
This project is open-source under the MIT License.
