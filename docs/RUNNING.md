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
./scripts/test_storage.sh    # Storage unit tests, all-ISA tests, and x86 VirtIO-Block completion
python3 -m unittest emulator.test_profile_catalog # OVD profile/default/artifact tests
python3 -m unittest emulator.test_profile_ext4_integration # Profile-backed ext4 policy tests
python3 -m emulator.ovd_gui             # Built-in tkinter GUI
```

---

## AArch64 Emulation

SimpleFb display integration and serial fallback are implemented; VirtIO-GPU
remains experimental. See `docs/DISPLAY_AARCH64_RISCV_PLAN.md`.

```bash
# Build
mkdir -p build/aarch64 && cd build/aarch64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/aarch64-toolchain.cmake -DARCH=aarch64 ../.. && make

# Run — serial only (current)
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel build/aarch64/omega.elf
```

**SimpleFb via Device Tree:**

```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic \
  -kernel build/aarch64/omega.elf
# Expected marker: Display console write path
```

**Experimental — VirtIO-GPU with GUI:**

```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 \
  -kernel build/aarch64/omega.elf \
  -serial stdio \
  -device virtio-gpu-pci \
  -display sdl
# The guarded VirtIO-GPU path may fall back to serial output.
```

---

## RISC-V 64 Emulation

SimpleFb display integration, serial fallback, and OpenSBI-to-kernel handoff
are implemented; VirtIO-GPU remains experimental. See
`docs/DISPLAY_AARCH64_RISCV_PLAN.md`.

```bash
# Build
mkdir -p build/riscv64 && cd build/riscv64
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/riscv64-toolchain.cmake -DARCH=riscv64 ../.. && make

# Run — serial / OpenSBI (current)
qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic \
  -kernel build/riscv64/omega.elf
```

**Current status:** The same SimpleFb and guarded VirtIO-GPU paths are
available as AArch64; QEMU framebuffer behavior still depends on the supplied
machine/device-tree configuration.

---

## Omega Virtual Device (OVD)

```bash
# Create and launch an x86_64 virtual device with Standard VGA
python3 -m emulator.ovd_cli create --name phone --arch x86_64 --ram 1024 --disk 64
python3 -m emulator.ovd_cli start --name phone --gpu       # GUI window
python3 -m emulator.ovd_cli start --name phone --no-gpu    # headless Bochs VBE

# AArch64 / RISC-V — SimpleFb/serial fallback; guarded VirtIO-GPU for --gpu
python3 -m emulator.ovd_cli create --name tablet --arch aarch64 --ram 512 --disk 32
python3 -m emulator.ovd_cli start --name tablet --no-gpu
```

OVD storage profiles select the QEMU transport for `userdata.img`:

```bash
python3 -m emulator.ovd_cli start --name phone --storage virtio --dry-run
python3 -m emulator.ovd_cli start --name phone --storage ahci
python3 -m emulator.ovd_cli start --name phone --storage usb
python3 -m emulator.ovd_cli start --name phone --storage sd
python3 -m emulator.ovd_cli start --name phone --storage optical
python3 -m emulator.ovd_cli start --name phone --storage none
```

New OVDs default to `virtio`: x86_64 uses transitional `virtio-blk-pci` with
legacy queue completion validation, while AArch64 and
RISC-V use `virtio-blk-device`. `--dry-run` prints the generated QEMU command
without launching it. This emulator wiring is independent of the kernel
VirtIO-Block driver; x86_64 runtime validation enables it explicitly, while
the AArch64/RISC-V MMIO driver remains opt-in during validation.

### Profile-based OVDs

Predefined device profiles are stored in `emulator/profiles/catalog.json` and
managed through the same CLI and GUI:

```bash
python3 -m emulator.ovd_cli profiles list
python3 -m emulator.ovd_cli profiles validate
python3 -m emulator.ovd_cli profiles show \
  --profile aarch64-virt-development --json
python3 -m emulator.ovd_cli profiles render \
  --profile riscv64-virt-minimal
python3 -m emulator.ovd_cli profiles artifacts \
  --profile aarch64-virt-development --dry-run
python3 -m emulator.ovd_cli create-from-profile \
  --profile aarch64-virt-development --name arm-dev
```

Native profiles resolve the latest compatible `omega.elf` and matching ext4
system image. Existing profile-backed OVDs refresh their local `system.ext4`
when the verified artifact changes. Profile catalog image size is authoritative
and disk overrides must match it. Ext4 creation requires `mke2fs` or
`mkfs.ext4` and fails closed when unavailable.

Launch the profile-aware Python/Tkinter manager with:

```bash
python3 -m emulator.ovd_gui
```

The GUI uses catalog RAM/disk defaults, supports profile inspection and
artifact checks, creates native profiles, manages generic OVDs, and controls
launch, stop, validation, logs, and deletion. Android AVD and VMApple entries
are external-adapter profiles and cannot currently be created as native OVDs.

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
