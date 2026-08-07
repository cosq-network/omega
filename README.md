# Omega Kernel

**Omega** is a lightweight, high-performance, freestanding C++20 Unix-like microkernel core designed to cross-compile natively on macOS (Apple Silicon M1/M2/M3) using Clang and LLVM (`ld.lld`) for **x86_64** (x64), **AArch64** (ARM64), and **RISC-V 64 (`rv64gc`)** target architectures.

---

## 🌟 Key Features & Subsystem Overview

- **Natively Cross-Compiled on macOS (Apple Silicon M1/M2/M3)**: Built using Homebrew LLVM (`clang++` + `ld.lld`) and CMake toolchain integration without external GCC cross-compiler dependencies.
- **Triple Architecture Support**:
  - **x86_64 (x64)**: 64-bit Long Mode entry, PAE paging, PML4 4-level page tables (2MB Huge Pages identity mapping), GDT loading, Xen PVH ELF note (`.xen_note`) for direct QEMU booting, and **Standard VGA** output (VGA text mode + Bochs VBE linear framebuffer).
  - **AArch64 (ARM64)**: Dynamic Exception Level transition (`EL2 -> EL1`), 2048-byte aligned `VBAR_EL1` vector table, and `SP_EL1` stack setup.
  - **RISC-V 64 (`rv64gc`)**: Supervisor Mode (S-mode) boot entry, `Sv39` 3-level page tables, `stvec` trap vector, OpenSBI console `ecall` interface, `satp` register mapping, and validated OpenSBI-to-`kernel_main()` handoff.
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
- **System Display Module (x86_64 Standard VGA)**: Layered display HAL with VGA text mode (80×25 at `0xB8000`), Bochs VBE linear framebuffer (1024×768×32 via DISPI), Multiboot2 framebuffer handoff, 8×16 bitmap font, and a kernel graphical console. `kprintf` output is mirrored to serial (COM1) and the active display backend concurrently.
- **AArch64/RISC-V Display Integration**: Shared FDT walker and boot-pointer handoff, Device Tree `simple-framebuffer` HALs, pixel-format metadata, portable framebuffer console routing, and safe serial fallback. An opt-in VirtIO-GPU MMIO 2D bring-up path is included with `-DENABLE_EXPERIMENTAL_VIRTIO_GPU=ON`.
- **Cross-Architecture Storage Foundation**: Common block requests, device lifecycle and driver registration, DMA mapping, GPT/MBR parsing, writable/read-only policy, flush/barrier handling, and a synthetic block backend verified on x86_64, AArch64, and RISC-V.
- **Experimental VirtIO-Block Path**: Opt-in VirtIO-MMIO block discovery and read/write/flush request encoding with AArch64/RISC-V cross-build coverage. Enable with `-DENABLE_EXPERIMENTAL_VIRTIO_BLOCK=ON` while queue completion validation continues.
- **Firmware & Bootloader Compatibility**: Compatible with **UEFI/GPT**, **U-Boot** (`bootefi` / `booti`), and **Coreboot** (TianoCore / GRUB).
- **Multi-Format Virtual Disk Image Generator**: Generates RAW (`.img`), QCOW2 (`.qcow2`), VMDK (`.vmdk`), and VDI (`.vdi`) disk images with embedded FAT32 payloads (`/EFI/BOOT/` and `/boot/omega.elf`).
- **Omega Virtual Device (OVD) Manager & GUI**: Android-like virtual device manager CLI (`emulator/ovd_manager.sh`), Standard VGA/SimpleFb launcher, selectable QEMU storage profiles (`virtio`, `ahci`, `usb`, `sd`, `optical`, `none`), and Tcl/Tk GUI application (`emulator/ovd_gui.tcl`).
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

### 2. System Display Module

| Document | Architecture | Phase | Status |
| :--- | :--- | :--- | :--- |
| [`docs/VGA_DISPLAY_PLAN.md`](docs/VGA_DISPLAY_PLAN.md) | x86_64 Standard VGA | 7.2 | **Implemented** |
| [`docs/DISPLAY_AARCH64_RISCV_PLAN.md`](docs/DISPLAY_AARCH64_RISCV_PLAN.md) | AArch64 & RISC-V 64 | 7.2b | **SimpleFb integrated; guarded VirtIO-GPU foundation** |
| [`docs/STORAGE_ARCHITECTURE_PLAN.md`](docs/STORAGE_ARCHITECTURE_PLAN.md) | All architectures | 7.1+ | **Storage architecture and implementation plan** |

**x86_64 (Phase 7.2 — done):** VGA text mode, Bochs VBE linear FB, Multiboot2 handoff, dual serial+display console.

**AArch64 / RISC-V (Phase 7.2b — in progress):** Shared FDT parsing, boot-time DT pointer handoff, SimpleFb HALs, serial fallback, and portable framebuffer-console integration are implemented. VirtIO-GPU and UEFI GOP handoff remain planned.

**Storage:** The architecture and initial implementation are specified in [`docs/STORAGE_ARCHITECTURE_PLAN.md`](docs/STORAGE_ARCHITECTURE_PLAN.md). The common layer is implemented and tested; GPT/MBR parsing, synthetic writes/flushes, and guarded VirtIO-Block request paths are available. NVMe, AHCI/SATA/ATAPI, SDHCI, USB Mass Storage, filesystem mounting, and hardware-specific writes remain subsequent milestones.

| Layer | Location | Role |
| :--- | :--- | :--- |
| **HAL** | `kernel/include/arch/display.hpp`, `kernel/arch/x86_64/{display,vga_text,vga_regs,bochs_vbe,boot_fb}.cpp` | Backend selection: BootFramebuffer → BochsVbe → VgaText |
| **Architecture HALs** | `kernel/arch/{aarch64,riscv64}/display.cpp` | SimpleFb probe, identity-map bring-up, serial fallback; guarded VirtIO-GPU hook |
| **Console** | `kernel/sys/display_console.cpp`, `kernel/sys/framebuffer.cpp`, `kernel/sys/font.cpp` | Dual-target console (serial + VGA text + FB), font rendering |
| **Integration** | `kernel/init/main.cpp`, `kernel/sys/kprint.cpp`, `kernel/sys/fdt.cpp` | All architectures initialize the display HAL after PMM/VMM; `kprintf` routes through the shared console |

**QEMU launch (x86_64):**

```bash
./scripts/run_qemu.sh              # headless Bochs VBE: -vga std -display none
./scripts/run_qemu.sh --gui        # graphical window (SDL / Cocoa)
./scripts/run_qemu.sh --text       # VGA text fallback: -vga none
./scripts/run_qemu.sh --storage virtio --dry-run  # inspect x86_64 VirtIO disk wiring
```

### 3. System Call ABI Conventions (`docs/ABI.md`)
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
│   ├── VGA_DISPLAY_PLAN.md        # SDM — x86_64 Standard VGA (Phase 7.2)
│   ├── DISPLAY_AARCH64_RISCV_PLAN.md  # SDM — AArch64/RISC-V extension (Phase 7.2b)
│   ├── STORAGE_ARCHITECTURE_PLAN.md   # Cross-architecture storage architecture
│   └── COMPLETION_REPORT.md       # Final Verification Report
├── emulator/
│   ├── README.md                  # OVD manager, storage profiles, and tests
│   ├── ovd_gui.tcl                # Tcl/Tk Omega Virtual Device Manager GUI
│   ├── ovd_manager.sh             # OVD Device Creator & Registry CLI
│   ├── ovd_run.sh                 # OVD Launcher (display + storage profiles)
│   ├── test_ovd_unit.sh           # OVD config and dry-run command unit tests
│   ├── test_ovd.sh                # OVD lifecycle, storage transport, and boot tests
│   └── test_ovd_gui.tcl           # Tcl/Tk GUI Unit Test Suite
├── scripts/
│   ├── README.md                  # Script catalog, usage, and verification guide
│   ├── create_bootable_disk.sh    # UEFI GPT Disk Image Generator
│   ├── run_qemu.sh                # Quick x86_64 QEMU launcher (VGA modes)
│   ├── test.sh                    # Multi-Arch QEMU Integration Tests (+ display)
│   ├── test_display.sh            # VGA / System Display Module test matrix
│   ├── test_display_aarch64.sh    # AArch64 display HAL/fallback smoke test
│   ├── test_storage_unit.sh       # Host storage API/partition unit tests
│   ├── test_storage.sh            # Storage unit + all-ISA QEMU integration tests
│   ├── test_scripts_unit.sh       # Shell-script syntax and launcher unit tests
│   └── test_disk_images.sh        # Disk Image Verification Test Suite
└── kernel/
    ├── arch/
    │   ├── x86_64/                # Boot, serial, idt, pci, VGA/display, linker.ld
    │   ├── aarch64/               # Boot, vectors, uart, gic, pci, SimpleFb display, linker.ld
    │   └── riscv64/               # Boot, trap, uart, plic, pci, SimpleFb display, linker.ld
    ├── include/                   # Kernel HAL & subsystem headers (display, console, storage, DMA)
    ├── init/main.cpp              # Kernel entry point
    └── sys/                       # PMM, VMM, heap, scheduler, syscall, VFS, storage, display_console, …
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

# Or launch from CLI (x86_64 includes Standard VGA):
./emulator/ovd_manager.sh create --name <device> --arch x86_64 --storage virtio
./emulator/ovd_run.sh run --name <device> --gpu --storage virtio
./emulator/ovd_run.sh run --name <device> --no-gpu --storage virtio
./emulator/ovd_run.sh run --name <device> --no-gpu --storage usb --dry-run
```

OVD storage profiles are selectable at launch: `virtio`, `ahci`, `usb`,
`sd`, `optical`, or `none`. The profile controls the QEMU transport and
device model while the shared `userdata.img` remains the backing image.
Use `--dry-run` to inspect the generated command without starting QEMU.

### 5. Run Automated Test Suites
```bash
./scripts/test.sh               # Multi-arch integration tests (includes test_display.sh)
./scripts/test_display.sh     # VGA display matrix: Bochs VBE, VgaText fallback, self-tests
./scripts/test_display_aarch64.sh # AArch64 display HAL and serial-fallback smoke test
./scripts/test_storage_unit.sh # Host storage API and partition unit tests
./scripts/test_storage.sh      # Storage tests on x86_64, AArch64, and RISC-V
./scripts/test_scripts_unit.sh # Shell-script and dry-run launcher unit tests
./scripts/test_disk_images.sh   # Bootable disk image tests
./emulator/test_ovd_unit.sh     # OVD configuration and storage profile unit tests
./emulator/test_ovd.sh          # OVD lifecycle, storage profiles, and display verification
tclsh emulator/test_ovd_gui.tcl # OVD Tcl/Tk GUI unit tests
```

---

## 📜 License
This project is open-source under the MIT License.
