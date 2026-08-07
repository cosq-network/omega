# Run scripts for QEMU testing of Omega Kernel

## x86_64 Emulation (Standard VGA / Bochs VBE)

Phase 7.2 display is fully implemented on x86_64. See `docs/VGA_DISPLAY_PLAN.md`.

```bash
# Build
mkdir -p build/x86_64 && cd build/x86_64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../.. && make

# Run — headless Bochs VBE (serial log mirrors kprintf)
./scripts/run_qemu.sh

# Run — graphical window (SDL on Linux, Cocoa on macOS)
./scripts/run_qemu.sh --gui

# Run — VGA text mode fallback only (no linear FB)
./scripts/run_qemu.sh --text

# Manual QEMU (equivalent to headless script)
qemu-system-x86_64 \
  -kernel build/x86_64/omega.elf \
  -serial stdio \
  -display none \
  -vga std
```

**Expected serial markers:** `[+] Display: BochsVbe 1024x768x32`, `[TEST][PASS] Bochs VBE linear framebuffer pixel`

**Automated tests:**

```bash
./scripts/test_display.sh    # Bochs VBE, -device VGA, -vga none fallback
./scripts/test.sh            # Full multi-arch suite (includes display tests)
./scripts/test_storage.sh    # Storage unit tests and all-ISA QEMU storage-core tests
```

---

## AArch64 Emulation

Display is **not yet implemented** (Phase 7.2b). Serial console only today. See `docs/DISPLAY_AARCH64_RISCV_PLAN.md`.

```bash
# Build
mkdir -p build/aarch64 && cd build/aarch64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/aarch64-toolchain.cmake -DARCH=aarch64 ../.. && make

# Run — serial only (current)
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel build/aarch64/omega.elf
```

**Planned (Phase 7.2b) — SimpleFb via Device Tree:**

```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic \
  -kernel build/aarch64/omega.elf
# Expected future marker: [+] Display: SimpleFb 1024x768x32
```

**Planned (Phase 7.2b) — VirtIO-GPU with GUI:**

```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 \
  -kernel build/aarch64/omega.elf \
  -serial stdio \
  -device virtio-gpu-pci \
  -display sdl
# Expected future marker: [+] Display: VirtioGpu
```

---

## RISC-V 64 Emulation

Display is **not yet implemented** (Phase 7.2b). OpenSBI firmware loads; kernel `main` handoff is still being completed. See `docs/DISPLAY_AARCH64_RISCV_PLAN.md`.

```bash
# Build
mkdir -p build/riscv64 && cd build/riscv64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/riscv64-toolchain.cmake -DARCH=riscv64 ../.. && make

# Run — serial / OpenSBI (current)
qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic \
  -kernel build/riscv64/omega.elf
```

**Planned (Phase 7.2b):** Same SimpleFb and VirtIO-GPU paths as AArch64 once DT pointer capture and kernel boot are fixed.

---

## Omega Virtual Device (OVD)

```bash
# Create and launch an x86_64 virtual device with Standard VGA
./emulator/ovd_manager.sh create --name phone --arch x86_64 --ram 1024 --disk 64
./emulator/ovd_run.sh run --name phone --gpu       # GUI window
./emulator/ovd_run.sh run --name phone --no-gpu    # headless Bochs VBE

# AArch64 / RISC-V — serial only today; VirtIO-GPU planned for --gpu
./emulator/ovd_manager.sh create --name tablet --arch aarch64 --ram 512 --disk 32
./emulator/ovd_run.sh run --name tablet --no-gpu
```

OVD storage profiles select the QEMU transport for `userdata.img`:

```bash
./emulator/ovd_run.sh run --name phone --storage virtio --dry-run
./emulator/ovd_run.sh run --name phone --storage ahci
./emulator/ovd_run.sh run --name phone --storage usb
./emulator/ovd_run.sh run --name phone --storage sd
./emulator/ovd_run.sh run --name phone --storage optical
./emulator/ovd_run.sh run --name phone --storage none
```

New OVDs default to `virtio`: x86_64 uses `virtio-blk-pci`, while AArch64 and
RISC-V use `virtio-blk-device`. `--dry-run` prints the generated QEMU command
without launching it. This emulator wiring is independent of the kernel
VirtIO-Block driver, which remains opt-in during validation.

## Storage Architecture

Storage support is being implemented according to
[`docs/STORAGE_ARCHITECTURE_PLAN.md`](STORAGE_ARCHITECTURE_PLAN.md). The first
QEMU storage target is VirtIO-Block, followed by NVMe and AHCI/SATA. SD/microSD,
USB Mass Storage, and optical media are separate protocol drivers above the
same block-device and filesystem layers.

Planned QEMU examples after the corresponding drivers land:

```bash
# VirtIO-Block reference path
qemu-system-x86_64 \
  -kernel build/x86_64/omega.elf \
  -drive file=disk_images/omega-x86_64-bootable.img,format=raw,if=virtio \
  -serial stdio -display none

# NVMe path
qemu-system-x86_64 \
  -kernel build/x86_64/omega.elf \
  -drive file=disk_images/omega-x86_64-bootable.img,format=raw,if=none,id=nvm \
  -device nvme,drive=nvm,serial=omega-nvme \
  -serial stdio -display none
```

These commands describe planned interfaces and are not advertised as passing
storage-driver tests until the corresponding milestones are complete.

---

## Related Documentation

| Document | Scope |
| :--- | :--- |
| `docs/VGA_DISPLAY_PLAN.md` | x86_64 Standard VGA (Phase 7.2 — implemented) |
| `docs/DISPLAY_AARCH64_RISCV_PLAN.md` | AArch64 & RISC-V display extension (Phase 7.2b — planned) |
| `docs/STORAGE_ARCHITECTURE_PLAN.md` | Cross-architecture storage architecture and driver roadmap |
| `docs/ROADMAP.md` | Full milestone matrix |
