# Cross-Platform Freestanding C++ Kernel Architecture Specification

## 1. Overview & Objective
This document specifies the architectural design, build pipeline, and boot abstractions for **Omega**, a freestanding Unix/Linux-like microkernel core written in C++20. The kernel cross-compiles natively on macOS (Apple Silicon M1/M2/M3) using Clang and LLVM (`lld`) for **x86_64** (x64), **AArch64** (ARM64), and **RISC-V 64 (`rv64gc`)**.

---

## 2. Directory Structure

```text
omega/
├── .docs/
│   └── ARCHITECTURE.md          # Architecture & design documentation (this file)
├── build/                       # Build outputs (binaries, objects, ISO images)
├── cmake/                       # Cross-compilation toolchain files
│   ├── x86_64-toolchain.cmake
│   └── aarch64-toolchain.cmake
├── CMakeLists.txt               # Main build script
├── kernel/
│   ├── arch/
│   │   ├── x86_64/              # x86_64 specific assembly, GDT, IDT, paging, serial
│   │   │   ├── boot.s
│   │   │   ├── gdt.cpp
│   │   │   ├── idt.cpp
│   │   │   ├── paging.cpp
│   │   │   └── serial.cpp
│   │   └── aarch64/             # AArch64 specific vector table, MMU, UART
│   │       ├── boot.s
│   │       ├── vectors.s
│   │       ├── mmu.cpp
│   │       └── uart.cpp
│   ├── include/
│   │   ├── arch/                # Hardware Abstraction Layer (HAL) interfaces
│   │   │   ├── cpu.hpp
│   │   │   ├── interrupts.hpp
│   │   │   ├── mmu.hpp
│   │   │   └── uart.hpp
│   │   ├── kernel/
│   │   │   ├── kprint.hpp
│   │   │   ├── memory.hpp
│   │   │   └── scheduler.hpp
│   │   └── std/                 # Freestanding standard type definitions
│   │       ├── cstddef.hpp
│   │       └── cstdint.hpp
│   ├── init/
│   │   └── main.cpp             # Architecture-agnostic kernel entry point
│   └── sys/
│       ├── kprint.cpp
│       └── memory.cpp
└── scripts/
    ├── run-x86_64.sh            # Run in QEMU x86_64
    └── run-aarch64.sh           # Run in QEMU AArch64
```

---

## 3. Boot Protocol & Entry Points

### 3.1 x86_64 (x64) Boot Sequence
* **Protocol**: Multiboot2 specification (compliant with GRUB2 / QEMU `-kernel`).
* **Entry Point**: `_start` in `kernel/arch/x86_64/boot.s`.
* **Execution Flow**:
  1. Bootloader loads kernel ELF in 32-bit protected mode.
  2. `boot.s` verifies Multiboot2 magic number (`0x36d37189`).
  3. Sets up initial 4-level paging (PML4 -> PDPT -> PD -> PT) mapping lower 2MB / 1GB identity pages.
  4. Enables PAE (Physical Address Extension) and Long Mode in EFER MSR.
  5. Loads 64-bit GDT (Global Descriptor Table) and performs far jump to 64-bit code segment.
  6. Sets up 64-bit stack and calls `kernel_main()`.

### 3.2 AArch64 (ARM64) Boot Sequence
* **Protocol**: Direct ELF / QEMU `-kernel` boot protocol (starts at EL1 or EL2).
* **Entry Point**: `_start` in `kernel/arch/aarch64/boot.s`.
* **Execution Flow**:
  1. Bootloader drops CPU into `_start`.
  2. Check current Exception Level (`CurrentEL`). If EL2, configure `HCR_EL2` and drop down to EL1.
  3. Initialize vector table base address register (`VBAR_EL1`).
  4. Enable Floating Point Unit / SIMD registers (`CPACR_EL1`).
  5. Set up initial identity translation tables (TTBR0_EL1 / TTBR1_EL1) and configure `TCR_EL1` / `MAIR_EL1`.
  6. Enable MMU and data/instruction caches via `SCTLR_EL1`.
  7. Set up stack pointer (`SP_EL1`) and jump to `kernel_main()`.

---

## 4. Hardware Abstraction Layer (HAL) Interface

The architecture-independent code in `kernel/init/main.cpp` interacts strictly with HAL interfaces:

```cpp
namespace hal {
    // Serial / Console I/O
    void uart_init();
    void uart_putc(char c);

    // System Display (Phase 7.2 x86_64; Phase 7.2b ARM/RISC-V)
    // See docs/VGA_DISPLAY_PLAN.md and docs/DISPLAY_AARCH64_RISCV_PLAN.md
    class Display;  // init(), framebuffer(), text_putc(), run_self_tests()

    // CPU & Interrupt Control
    void interrupts_enable();
    void interrupts_disable();
    void halt_cpu();

    // Memory Management
    void mmu_init();
}
```

---

## 5. C++ Freestanding Constraints & Runtime Rules

## 5A. Storage Architecture Boundary

Storage support is protocol-oriented and is specified in
[`STORAGE_ARCHITECTURE_PLAN.md`](STORAGE_ARCHITECTURE_PLAN.md). Architecture
code supplies bus discovery, MMIO, interrupts, DMA mapping, cache
synchronization, and reset hooks. Shared storage code supplies request
validation, device identity, queueing, protocol commands, partitions, and
filesystem clients.

The first implementation keeps drivers in the kernel because Omega IPC and
process isolation are not complete. The common storage device and request
interfaces must remain suitable for a later userspace `storaged` server. No
filesystem code may depend directly on PCI, USB, SDHCI, AHCI, NVMe, or VirtIO
registers.

To compile C++ without an underlying OS host, the following flags and rules are enforced:

### Compiler Flags (Clang)
* `-ffreestanding`: Disables assumptions about host OS C standard library availability.
* `-nostdlib` / `-nostdinc`: Prevents linking host libraries or headers.
* `-fno-exceptions`: Disables C++ stack unwinding and exception handling (`throw`/`catch`).
* `-fno-rtti`: Disables Run-Time Type Information (`dynamic_cast`/`typeid`).
* `-fno-threadsafe-statics`: Disables implicit mutex locks around static local variable initializers.
* `-fno-use-cxa-atexit`: Disables dynamic destructor registration on shutdown.

### Required Freestanding Built-ins
The compiler may implicitly generate calls to basic memory routines. The kernel implements these in `kernel/sys/memory.cpp`:
```cpp
extern "C" {
    void* memcpy(void* dest, const void* src, size_t n);
    void* memset(void* s, int c, size_t n);
    void* memmove(void* dest, const void* src, size_t n);
    int memcmp(const void* s1, const void* s2, size_t n);
}
```

---

## 6. Build Toolchain Configuration (macOS Host)

### Prerequisites on Apple Silicon (M1/M2/M3)
```bash
brew install llvm cmake ninja qemu
```

### Compiler Target Triples
* **x86_64**: `clang++ --target=x86_64-unknown-none-elf`
* **AArch64**: `clang++ --target=aarch64-unknown-none-elf`
* **RISC-V 64**: `clang++ --target=riscv64-unknown-none-elf`
* **Linker**: `ld.lld` (LLVM linker from `brew --prefix llvm`/bin/ld.lld)

---

## 7. Execution & Emulation (QEMU)

### x86_64 Target
```bash
qemu-system-x86_64 -kernel build/x86_64/kernel.elf -serial stdio -display none
```

### AArch64 Target
```bash
qemu-system-aarch64 -machine virt -cpu cortex-a57 -nographic -kernel build/aarch64/kernel.elf
```

### RISC-V 64 Target
```bash
qemu-system-riscv64 -machine virt -bios default -nographic -kernel build/riscv64/omega.elf
```

The userspace process ABI, initial stack layout, per-ISA address ranges, and
static ELF/COW verification status are defined in [`ABI.md`](ABI.md).
