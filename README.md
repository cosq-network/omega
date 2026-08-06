# Omega Kernel

**Omega** is a lightweight, high-performance, freestanding C++20 Unix-like microkernel core designed to cross-compile natively on macOS (Apple Silicon M1/M2/M3) using Clang and LLVM (`ld.lld`) for **x86_64** (x64), **AArch64** (ARM64), and **RISC-V 64 (`rv64gc`)** target architectures.

---

## 🌟 Key Features & Subsystem Overview

- **Natively Cross-Compiled on macOS (Apple Silicon M1/M2/M3)**: Built using Homebrew LLVM (`clang++` + `ld.lld`) and CMake toolchain integration without external GCC cross-compiler dependencies.
- **Triple Architecture Support**:
  - **x86_64 (x64)**: 64-bit Long Mode entry, PAE paging, PML4 4-level page tables (2MB Huge Pages identity mapping), GDT loading, and Xen PVH ELF note (`.xen_note`) for direct QEMU booting.
  - **AArch64 (ARM64)**: Dynamic Exception Level transition (`EL2 -> EL1`), 2048-byte aligned `VBAR_EL1` vector table, and `SP_EL1` stack setup.
  - **RISC-V 64 (`rv64gc`)**: Supervisor Mode (S-mode) boot entry, `Sv39` 3-level page tables, `stvec` trap vector, OpenSBI console `ecall` interface, and `satp` register mapping.
- **Freestanding C++20 Runtime**: Independent of host C/C++ standard libraries (`-ffreestanding -fno-exceptions -fno-rtti`). Implements freestanding memory primitives (`memcpy`, `memset`, `memmove`, `memcmp`) and vararg printing (`kprintf`).
- **Physical Memory Manager (PMM)**: 4KiB Bitmap Frame Allocator tracking physical page frames (`alloc_frame` / `free_frame`).
- **Virtual Memory Manager (VMM)**: 4-Level Page Table Mapping Engine interacting with architectural base registers (`CR3` on x86_64, `TTBR0_EL1` on AArch64, `satp` on RISC-V 64).
- **Dynamic Kernel Heap Allocator**: Free-list block header allocator providing standard C ABI bindings (`kmalloc` / `kfree`) with 8-byte alignment and block coalescing.
- **Hardware Interrupt Architecture**: 256-entry Interrupt Descriptor Table (IDT) for x86_64, System Vector Base (`VBAR_EL1`) for AArch64, and Supervisor Trap Vector (`stvec`) / PLIC for RISC-V 64.
- **Preemptive Multi-threading Scheduler**: Circular linked list Thread Control Block (TCB) engine executing round-robin thread yields and context switching.
- **System Call ABI Dispatcher**: Formal System Call ABI (`docs/ABI.md`) supporting `SYS_YIELD` (1), `SYS_WRITE` (2), `SYS_EXIT` (3), `SYS_OPEN` (4), `SYS_READ` (5), `SYS_CLOSE` (6), `SYS_FORK` (7), `SYS_EXECVE` (8).
- **Virtual Filesystem (VFS) & Initrd RAM Disk**: POSIX-like node tree structure (`/` root node) and memory-backed initial RAM disk file reader.
- **Userland Mode Manager**: Privilege boundary control (Ring 3 / EL0 / U-Mode) and user stack frame allocation.
- **ELF 64-bit Executable Parser & Loader**: `Elf64Header` and `Elf64ProgramHeader` parser loading `PT_LOAD` segment virtual addresses into memory.
- **PCI Bus Scanner**: Bus configuration space reader (`0xCF8` Address / `0xCFC` Data ports) enumerating vendor/device IDs across 256 PCI buses.
- **VirtIO Network Stack**: VirtIO-Net packet reader, Ethernet L2, IPv4 L3, and UDP/TCP L4 stack headers.
- **Firmware & Bootloader Compatibility**: Compatible with **UEFI/GPT**, **U-Boot** (`bootefi` / `booti`), and **Coreboot** (TianoCore / GRUB).
- **Multi-Format Virtual Disk Image Generator**: Generates RAW (`.img`), QCOW2 (`.qcow2`), VMDK (`.vmdk`), and VDI (`.vdi`) disk images with embedded FAT32 payloads (`/EFI/BOOT/` and `/boot/omega.elf`).
- **Omega Virtual Device (OVD) Manager & GUI**: Android-like virtual device manager CLI (`emulator/ovd_manager.sh`), launcher script (`emulator/ovd_run.sh`), and Tcl/Tk GUI application (`emulator/ovd_gui.tcl`).
- **Containerization & CI/CD**: Minimal Alpine-based `Dockerfile`, VSCode DevContainers/Codespaces (`.devcontainer/devcontainer.json`), and GitHub Actions CI/CD (`.github/workflows/ci.yml`).

---

## ⚙️ Detailed Technical Implementation Specifications

### 1. Bootstrapping & Architectural Handover
- **x86_64 (`kernel/arch/x86_64/boot.s`)**:
  - Entry point `_start` identity-maps initial 1 GB physical memory using 2 MB Huge Pages. Enables PAE, activates Long Mode via `EFER` MSR, loads GDT, and jumps to `kernel_main()`.
  - Includes a Xen PVH ELF Note (`.xen_note` section with `PT_NOTE` program header) to enable QEMU direct `-kernel` loading.
- **AArch64 (`kernel/arch/aarch64/boot.s` & `vectors.s`)**:
  - Configures `HCR_EL2.RW = 1` for 64-bit EL1 execution, sets up `SPSR_EL2` to target `EL1h`, executes `eret` to transition from EL2 to EL1, configures `VBAR_EL1` vector table, and enables FP/SIMD (`CPACR_EL1.FPEN = 0b11`).
- **RISC-V 64 (`kernel/arch/riscv64/boot.s` & `trap.s`)**:
  - Supervisor Mode entry loaded at `0x80200000` above OpenSBI firmware. Clears `sstatus.SIE`, initializes `stvec` supervisor trap vector, sets up 16 KiB boot stack pointer, and branches to `kernel_main()`.

### 2. System Call ABI Conventions (`docs/ABI.md`)
- **x86_64**: `syscall` instruction (Syscall ID in `RAX`, Args in `RDI`, `RSI`, `RDX`, `RCX`, `R8`, `R9`).
- **AArch64**: `svc #0` instruction (Syscall ID in `X8`, Args in `X0`-`X5`).
- **RISC-V 64**: `ecall` instruction (Syscall ID in `A7`, Args in `A0`-`A5`).

---

## 📁 Repository Directory Structure

```text
omega/
├── Dockerfile                     # Minimal Alpine-based cross-compilation environment
├── .devcontainer/                 # VSCode DevContainers and GitHub Codespaces configuration
├── .github/workflows/ci.yml       # GitHub Actions Multi-Arch CI/CD Pipeline
├── CMakeLists.txt                 # Master CMake cross-compilation target script
├── cmake/
│   ├── x86_64-toolchain.cmake     # x86_64 toolchain configuration
│   ├── aarch64-toolchain.cmake    # AArch64 toolchain configuration
│   └── riscv64-toolchain.cmake    # RISC-V 64 toolchain configuration
├── disk_images/                   # Multi-format virtual disk images (RAW, QCOW2, VMDK, VDI)
├── docs/
│   ├── ARCHITECTURE.md            # Architectural Specification
│   ├── ABI.md                     # System Call ABI Specification
│   ├── FIRMWARE_BOOT.md           # U-Boot & Coreboot Firmware Compatibility
│   ├── RISCV64_PLAN.md            # RISC-V 64 Architectural Plan
│   ├── ROADMAP.md                 # Multi-Phase Implementation Roadmap
│   ├── RUNNING.md                 # QEMU Execution & Build Guide
│   └── COMPLETION_REPORT.md       # Final Verification Report
├── emulator/
│   ├── ovd_gui.tcl                # Tcl/Tk Omega Virtual Device Manager GUI
│   ├── ovd_manager.sh             # OVD Device Creator & Registry CLI
│   ├── ovd_run.sh                 # OVD Launcher Script (Headful GUI & Headless)
│   ├── test_ovd.sh                # Automated OVD Integration Test Suite
│   └── test_ovd_gui.tcl           # Tcl/Tk GUI Unit Test Suite
├── scripts/
│   ├── create_bootable_disk.sh    # UEFI GPT Disk Image Generator
│   ├── test.sh                    # Multi-Arch QEMU Kernel Integration Test Suite
│   └── test_disk_images.sh        # Disk Image Verification Test Suite
└── kernel/
    ├── arch/
    │   ├── x86_64/                # x86_64 boot assembly, serial, idt, pci, linker.ld
    │   ├── aarch64/               # AArch64 boot assembly, vectors, uart, gic, pci, linker.ld
    │   └── riscv64/               # RISC-V 64 boot assembly, trap, uart, plic, pci, linker.ld
    ├── include/                   # Kernel HAL & subsystem header files
    ├── init/main.cpp              # Kernel entry point
    └── sys/                       # Subsystem implementations (pmm, vmm, heap, scheduler, syscall, vfs, initrd, userland, elf_loader, net)
```

---

## 🚀 Building and Running

### 1. Build Kernel Binaries (x86_64, AArch64, RISC-V 64)
```bash
# Build x86_64
mkdir -p build/x86_64 && cd build/x86_64 && cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../.. && make

# Build AArch64
mkdir -p build/aarch64 && cd build/aarch64 && cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/aarch64-toolchain.cmake -DARCH=aarch64 ../.. && make

# Build RISC-V 64
mkdir -p build/riscv64 && cd build/riscv64 && cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/riscv64-toolchain.cmake -DARCH=riscv64 ../.. && make
```

### 2. Run Containerized Development Environment (Docker / DevContainers)
```bash
# Build Docker image
docker build -t omega-dev .

# Run interactive container
docker run -it --rm -v $(pwd):/workspace omega-dev
```

### 3. Generate Bootable Virtual Disk Images
```bash
./scripts/create_bootable_disk.sh
```

### 4. Run Omega Virtual Device (OVD) Manager GUI
```bash
./emulator/ovd_gui.tcl
```

### 5. Run Automated Test Suites
```bash
./scripts/test.sh              # QEMU Kernel Integration Tests
./scripts/test_disk_images.sh   # Bootable Disk Image Tests
./emulator/test_ovd.sh          # OVD Device Lifecycle Tests
tclsh emulator/test_ovd_gui.tcl # OVD Tcl/Tk GUI Unit Tests
```

---

## 📜 License
This project is open-source under the MIT License.
