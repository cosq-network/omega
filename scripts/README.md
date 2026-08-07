# Omega Scripts

This directory contains reproducible build, launch, image-generation, and
automated verification scripts for Omega. Scripts determine the repository
root from their own location, so they can be invoked from any working
directory:

```sh
./scripts/<script>.sh
```

The scripts use Bash, CMake, Clang/LLVM, QEMU, and optional host utilities
such as `qemu-img`, `mtools`, and `mformat`. Cross-architecture scripts use
the toolchain files in `cmake/` and target `x86_64`, `aarch64`, and `riscv64`.

## Script catalog

| Script | Purpose | Mutates repository artifacts? |
| --- | --- | --- |
| `run_qemu.sh` | Build-if-needed and launch the x86_64 kernel with display and storage options | No, except build output when missing |
| `create_bootable_disk.sh` | Build all kernels and generate RAW/FAT32, QCOW2, VMDK, and VDI images | Yes: `build/` and `disk_images/` |
| `test_disk_images.sh` | Generate and validate bootable images and embedded payload paths | Yes: test images in `disk_images/` |
| `test.sh` | Full multi-architecture, display, storage, script, and OVD regression suite | Yes: build/test logs and temporary OVD state |
| `test_display.sh` | x86_64 Bochs VBE and VGA text-mode matrix | Build output and temporary QEMU logs |
| `test_display_aarch64.sh` | AArch64 display HAL and serial fallback smoke test | Build output and temporary QEMU logs |
| `test_storage_unit.sh` | Host-side storage API, write-policy, and partition parser unit tests | `build/storage-tests/` |
| `test_storage.sh` | Storage unit tests, all-ISA boot tests, and experimental VirtIO-Block builds | Build output and temporary QEMU logs |
| `test_scripts_unit.sh` | Shell syntax, argument validation, dry-run launchers, and script contracts | Temporary files only |

## QEMU launcher

`run_qemu.sh` targets the existing x86_64 build at
`build/x86_64/omega.elf`. If it is missing, the script configures and builds
the kernel first.

```sh
./scripts/run_qemu.sh                 # headless Standard VGA / Bochs VBE
./scripts/run_qemu.sh --gui           # SDL on Linux or Cocoa on macOS
./scripts/run_qemu.sh --text          # VGA text fallback with -vga none
./scripts/run_qemu.sh --storage virtio
./scripts/run_qemu.sh --storage ahci
./scripts/run_qemu.sh --storage usb
./scripts/run_qemu.sh --storage none
./scripts/run_qemu.sh --storage virtio --dry-run
```

`--dry-run` prints the complete QEMU command and does not start QEMU. The
launcher storage profiles are emulator wiring:

| Profile | QEMU model | Intended protocol |
| --- | --- | --- |
| `auto` | QEMU default disk attachment | Compatibility fallback |
| `virtio` | `virtio-blk-pci` | VirtIO block |
| `ahci` | IDE disk attachment | SATA/ATA-style disk model |
| `usb` | `usb-storage` | USB Mass Storage model |
| `none` | No disk argument | Storage-free boot |

The backing image used by the launcher is
`disk_images/omega-x86_64-bootable.img`. Create it with
`create_bootable_disk.sh` when needed.

## Disk image generation

```sh
./scripts/create_bootable_disk.sh
./scripts/test_disk_images.sh
```

The generator builds each architecture and creates a 64 MiB FAT32-compatible
raw image containing the architecture-specific EFI payload under
`EFI/BOOT/` and the kernel under `boot/omega.elf`. If `qemu-img` is
available, QCOW2, VMDK, and VDI conversions are also generated. If `mtools`
is unavailable, the raw image is still created but FAT32 population and
payload verification are limited by the host environment.

## Storage verification

```sh
./scripts/test_storage_unit.sh
./scripts/test_storage.sh
```

The host unit suite exercises the common storage manager with a fake block
device, including:

- aligned reads and writes;
- out-of-bounds and unaligned request rejection;
- read-only device protection;
- writable capability and flush/barrier policy;
- GPT and MBR parsing;
- device registration, lookup, and removal state.

The integration suite boots x86_64, AArch64, and RISC-V kernels in QEMU and
checks the storage-core pass markers. It also cross-builds the opt-in
VirtIO-Block configuration for AArch64 and RISC-V. The default kernels use
the safe synthetic backend; the concrete NVMe, AHCI, SDHCI, USB MSC, ATAPI,
and filesystem drivers remain planned milestones.

## Full regression suite

```sh
./scripts/test.sh
```

The full suite runs architecture boot checks, display tests, storage tests,
shell-script unit tests, OVD unit tests, and OVD lifecycle tests. QEMU
processes are deliberately terminated after the expected boot markers are
observed; `Killed: 9` from the shell is therefore expected during these
bounded idle-loop tests.

## Conventions and safety

- Use `bash script.sh` when the executable bit is unavailable.
- Scripts fail fast with `set -e` or `set -euo pipefail`.
- Unit tests use temporary directories and clean them with traps.
- Image-generation scripts intentionally write under `build/` and
  `disk_images/`; review those paths before running in automation.
- No script enables experimental kernel drivers in the default build. Opt-in
  VirtIO-Block builds are isolated under dedicated build directories by
  `test_storage.sh`.
