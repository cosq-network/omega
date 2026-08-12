# Omega Kernel: RISC-V 64-bit (rv64gc) Architecture Support Specification & Plan

## Executive Summary
This document specifies the architectural plan, boot protocol, translation table hierarchy, interrupt model, and implementation roadmap to add **RISC-V 64-bit (rv64gc)** target support to the **Omega** kernel alongside existing **x86_64 (AMD64)** and **AArch64 (ARM64)** ISAs.

---

## 1. RISC-V 64 Architecture Target Profile

| Specification | Setting | Note |
| :--- | :--- | :--- |
| **ISA Architecture** | `rv64gc` / `rv64imafdc` | 64-bit RISC-V with Atomic, Single/Double Precision Floating Point, Compressed Instructions |
| **ABI** | `lp64d` | 64-bit Pointers, Hardware Double-Precision Floating Point ABI |
| **Target Triple** | `riscv64-unknown-none-elf` | Clang bare-metal target triple |
| **Privilege Levels** | Machine (M-mode), Supervisor (S-mode), User (U-mode) | Kernel executes in S-mode under OpenSBI / QEMU `virt` machine |
| **Paging Scheme** | `Sv39` (3-Level Paging) | 39-bit Virtual Address space, 4KiB base pages, 2MB / 1GB Megapages / Gigapages |
| **Interrupt Model** | PLIC (Platform-Level Interrupt Controller) & CLINT | Hardware IRQ handling via `scause`, `stvec`, `sie`, `sip` registers |
| **Serial UART** | NS16550A UART (`0x10000000`) | Standard QEMU `virt` board serial device |

---

## 2. Directory Layout Expansion

```text
omega/
├── cmake/
│   ├── x86_64-toolchain.cmake
│   ├── aarch64-toolchain.cmake
│   └── riscv64-toolchain.cmake      # RISC-V 64 toolchain configuration
└── kernel/
    ├── arch/
    │   ├── x86_64/
    │   ├── aarch64/
    │   └── riscv64/                 # RISC-V 64 architecture modules
    │       ├── boot.s               # S-mode entry, satp paging, stack setup
    │       ├── trap.s               # Exception & Interrupt Trap Handler (stvec)
    │       ├── plic.cpp             # PLIC Interrupt Controller driver
    │       ├── uart.cpp             # NS16550A UART driver
    │       ├── pci.cpp              # RISC-V PCI bus driver
    │       └── linker.ld            # RISC-V 64 ELF linker script
    └── include/
        └── arch/
            ├── cpu.hpp
            ├── interrupts.hpp
            ├── uart.hpp
            └── pci.hpp
```

---

## 3. Implementation Steps & Technical Subsystems

### Step 1: Toolchain & CMake Infrastructure (`cmake/riscv64-toolchain.cmake`)
- Define target triple: `riscv64-unknown-none-elf`
- Configure Clang ABI options: `-march=rv64gc -mabi=lp64d`
- Enforce freestanding compilation flags: `-ffreestanding -fno-exceptions -fno-rtti -nostdlib -nostdinc`.

### Step 2: Bootstrapping & S-Mode Entry (`kernel/arch/riscv64/boot.s`)
- **Entry Protocol**: QEMU `-kernel` boots at `0x80000000` (RAM base) via OpenSBI in Supervisor Mode (S-Mode).
- **Control Register Configuration**:
  1. Mask all supervisor interrupts (`csrci sstatus, 2`).
  2. Load trap vector base address register (`stvec`) pointing to `trap_entry`.
  3. Construct initial `Sv39` 3-level page tables mapping `0x80000000` (RAM) using 2MB Megapages.
  4. Write page table root physical address to supervisor address translation and protection register (`satp` mode `8` for Sv39) and execute `sfence.vma`.
  5. Initialize kernel stack pointer (`sp`) and jump to C++ entry point `kernel_main()`.

### Step 3: Hardware Exception & Trap Handler (`kernel/arch/riscv64/trap.s`)
- Define 64-byte aligned trap entry handler (`stvec`).
- Save general registers `x1-x31` to kernel stack frame (`TrapFrame`).
- Read `scause` (Supervisor Cause Register) and `stval` (Supervisor Trap Value) to distinguish software traps (`ecall`), timer interrupts, and page faults (`0xc` / `0xd`).
- Execute `sret` to return from trap.

### Step 4: NS16550A UART Driver (`kernel/arch/riscv64/uart.cpp`)
- Interface with NS16550A UART memory-mapped registers at base address `0x10000000`.
- Implement `uart_init()`, `uart_putc(c)`, `uart_puts(str)`.

### Step 5: PLIC Interrupt Controller (`kernel/arch/riscv64/plic.cpp`)
- Interface with Platform-Level Interrupt Controller at base address `0x0C000000`.
- Set priority thresholds, enable IRQ lines, and implement interrupt claims and completion acknowledgments.

### Step 6: Linker Script (`kernel/arch/riscv64/linker.ld`)
- Set entry address: `ENTRY(_start)`.
- Base RAM load address: `0x80000000`.
- Align sections (`.text`, `.rodata`, `.data`, `.bss`) to 4KiB page boundaries (`ALIGN(4K)`).

---

## 4. QEMU Emulation Command

```bash
# Build
mkdir -p build/riscv64 && cd build/riscv64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/riscv64-toolchain.cmake -DARCH=riscv64 ../..
make

# Run in QEMU RISC-V 64
qemu-system-riscv64 -M virt -cpu rv64 -nographic -kernel build/riscv64/omega.elf
```
