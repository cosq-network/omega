# Omega Kernel: Firmware Boot Compatibility (U-Boot & Coreboot)

## Executive Summary
This document describes the firmware boot compatibility matrix and execution procedures for booting the **Omega** kernel using modern, open-source firmware platforms: **U-Boot (Universal Boot Loader)** and **Coreboot**.

---

## 1. Firmware Compatibility Matrix

| Target Architecture | Platform / Target Device | Firmware Engine | Primary Payload Format | Boot Command / Protocol |
| :--- | :--- | :--- | :--- | :--- |
| **x86_64 (AMD64)** | Laptops, Desktops, Chromebooks | **Coreboot** + TianoCore / SeaBIOS | Standard ELF / Multiboot2 | Direct ELF execution or UEFI `BOOTX64.EFI` |
| **AArch64 (ARM64)** | Single Board Computers, Mobile, Tablets | **U-Boot** | EFI Stub / Flat Image Tree (`fitImage`) | `bootefi` or `booti` |
| **RISC-V 64 (`rv64gc`)** | RISC-V SBCs, QEMU `virt` | **U-Boot** + OpenSBI | Standard ELF / EFI Payload | `bootm` / `bootefi` |

---

## 2. Booting Omega via U-Boot (Universal Boot Loader)

U-Boot provides modern UEFI API emulation (`bootefi`) and raw Linux kernel image execution (`booti`/`bootm`) across AArch64 and RISC-V 64 platforms.

### Method A: UEFI Execution (`bootefi`) via FAT Disk Image
U-Boot automatically scans the FAT32 partition of generated disk images (`omega-aarch64-bootable.img` / `omega-riscv64-bootable.img`):

```bash
# Load EFI image from storage drive into RAM
load mmc 0:1 ${kernel_addr_r} EFI/BOOT/BOOTAA64.EFI

# Boot Omega via U-Boot UEFI subsystem
bootefi ${kernel_addr_r} ${fdt_addr}
```

### Method B: Direct Kernel Boot (`booti` / `bootm`)
U-Boot can load the uncompressed `omega.elf` binary directly into memory:

```bash
# Load ELF binary from storage
load mmc 0:1 0x40200000 /boot/omega.elf

# Boot uncompressed image
booti 0x40200000 - ${fdtcontroladdr}
```

---

## 3. Booting Omega via Coreboot

Coreboot is a open-source firmware framework for x86_64 laptops, Chromebooks, and workstations. Coreboot initializes hardware and hands over execution to a **Payload**.

### Payload Configuration Options:

1. **Coreboot + TianoCore (EDK2 UEFI Payload)**:
   - Modern recommended approach.
   - Coreboot initializes silicon, loads TianoCore EDK2, which reads `omega-x86_64-bootable.img` ESP partition and executes `/EFI/BOOT/BOOTX64.EFI`.
2. **Coreboot + SeaBIOS / GRUB Payload**:
   - Coreboot loads GRUB as a payload, which parses `multiboot2 /boot/omega.elf` and executes the kernel in 64-bit Long Mode.
