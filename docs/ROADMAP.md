# Omega Kernel Implementation Roadmap & Milestone Matrix

## Overview
This document outlines the multi-phase implementation roadmap for **Omega**—a freestanding, cross-platform microkernel core written in C++20. Omega cross-compiles natively on macOS (Apple Silicon M1/M2/M3) using Clang and LLVM (`ld.lld`) for **x86_64**, **AArch64**, and **RISC-V 64 (`rv64gc`)** architectures.

---

## 🚦 Phase Completion Matrix

| Phase / Milestone | Status | Description | Key Subsystems | Verification |
| :--- | :---: | :--- | :--- | :---: |
| **Phase 1: Foundation & Toolchains** | `COMPLETED` | Cross-compilation & early UART serial logging | Clang, CMake toolchains, COM1 & PL011 UART | QEMU Serial Console |
| **Phase 2A: x86_64 Long Mode & Boot** | `COMPLETED` | 64-bit Long Mode identity paging & bootloader note | PML4, PAE, Xen PVH ELF note (`.xen_note`) | QEMU `-kernel` |
| **Phase 2B: AArch64 Exception Levels** | `COMPLETED` | EL2 to EL1 drop & vector base register configuration | `CurrentEL`, `VBAR_EL1`, `SP_EL1` stack | QEMU `-M virt` |
| **Phase 2C: RISC-V 64 S-Mode & Traps** | `COMPLETED` | Supervisor Mode (S-mode) boot & trap vector setup | `satp` (Sv39), `stvec`, OpenSBI console `ecall` | QEMU `-M virt -bios default` |
| **Phase 3A: Physical Memory (PMM)** | `COMPLETED` | 4KiB Bitmap frame allocator | `alloc_frame` / `free_frame` | Physical Bitmap Matrix |
| **Phase 3B: Virtual Memory (VMM)** | `COMPLETED` | Architectural page table mapping engine | `CR3` (x86), `TTBR0_EL1` (ARM), `satp` (RISC-V) | `vmm_map_page` |
| **Phase 3C: Dynamic Kernel Heap** | `COMPLETED` | Free-list block header allocator | `kmalloc` / `kfree` with block coalescing | Static Heap Buffer |
| **Phase 3D: Interrupt Drivers** | `COMPLETED` | Hardware interrupt gate setup | 256-entry IDT (x86), VBAR (ARM), STVEC/PLIC (RISC-V) | Hardware Traps |
| **Phase 4A: Preemptive Multi-threading**| `COMPLETED` | Round-robin thread scheduler | Thread Control Blocks (TCB), stack allocation | Cooperative Yields |
| **Phase 4B: System Call ABI Dispatcher**| `COMPLETED` | Formal System Call ABI dispatcher (`docs/ABI.md`) | `SYS_YIELD`, `SYS_WRITE`, `SYS_EXIT` | Syscall Dispatcher |
| **Phase 5A: Virtual Filesystem (VFS)** | `COMPLETED` | VFS node tree & root directory mount | POSIX node operations, `/` mount | `vfs::open("/")` |
| **Phase 5B: RAM Disk Initrd** | `COMPLETED` | Initial RAM disk memory file driver | Memory file header reader | `initrd::init()` |
| **Phase 5.1: Userland Privilege Mode**| `COMPLETED` | Ring 3 / EL0 / U-Mode privilege manager | Userland stack setup & mode jump | `enter_userland()` |
| **Phase 5.2: ELF 64-bit Binary Parser**| `COMPLETED` | Executable binary header & segment parser | `Elf64Header` & `PT_LOAD` loader | `ElfLoader::load()` |
| **Phase 5.3: POSIX Syscall Expansion** | `COMPLETED` | Process file descriptor table & core syscalls | `sys_open`, `sys_read`, `sys_close`, `sys_fork`, `sys_execve` | `fd_table[16]` |
| **Phase 5.4: PCI Bus Scanner** | `COMPLETED` | PCI bus configuration space reader | I/O Ports `0xCF8`/`0xCFC` scanning | Device Vendor Scan |
| **Phase 5.5: VirtIO Network Stack** | `COMPLETED` | VirtIO-Net packet driver & TCP/IP stack | L2 Ethernet, L3 IPv4, L4 UDP/TCP headers | Frame RX Reader |
| **Phase 6A: Bootable Disk Generator** | `COMPLETED` | UEFI/GPT multi-format disk generator | RAW, QCOW2, VMDK, VDI with FAT32 payloads | `create_bootable_disk.sh` |
| **Phase 6B: Firmware Compatibility** | `COMPLETED` | U-Boot (`bootefi`/`booti`) & Coreboot (EDK2/GRUB) | Embedded `/EFI/BOOT/` & `/boot/omega.elf` | `docs/FIRMWARE_BOOT.md` |
| **Phase 6C: OVD Emulator & GUI** | `COMPLETED` | Omega Virtual Device Manager & Tcl/Tk GUI | CLI manager, runner, Tcl/Tk GUI application | `ovd_gui.tcl` |
| **Phase 6D: Containerization & CI/CD** | `COMPLETED` | Alpine Dockerfile, DevContainers, GitHub Actions | DevContainers, Codespaces, GitHub Actions CI/CD | `.github/workflows/ci.yml` |

---

## 🗺️ Future Architectural Expansion & Phase 7 Roadmap

### Phase 7.1: VirtIO Block Storage Device Driver
- Implement VirtIO-Block PCI/MMIO driver for high-performance block read/write I/O.
- Mount ext2 / FAT32 filesystems on VirtIO block storage.

### Phase 7.2: Graphical Framebuffer Console (VESA / VirtIO-GPU)
- Implement linear framebuffer display driver for hardware graphical consoles.
- Integrate font rendering engine for graphical shell interface.

### Phase 7.3: Native SMP Multi-Core Symmetric Multiprocessing
- Implement SMP multi-core initialization (APIC ICR on x86_64, PSCI on AArch64, OpenSBI IPI on RISC-V 64).
- Per-CPU scheduler queues and inter-processor interrupt (IPI) locking.
