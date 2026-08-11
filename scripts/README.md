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
| `run_qemu.sh` | Build-if-needed and launch the x86_64 kernel with display, storage, network, initrd, and lifecycle options | No, except build output when missing |
| `create_bootable_disk.sh` | Build selected kernels and generate RAW/FAT32, QCOW2, VMDK, and VDI images | Yes: configured build/image roots |
| `test_disk_images.sh` | Generate and validate bootable images and embedded payload paths | Yes: test images in `disk_images/` |
| `test.sh` | Full multi-architecture, display, storage, script, and OVD regression suite | Yes: build/test logs and temporary OVD state |
| `test_display.sh` | x86_64 Bochs VBE and VGA text-mode matrix | Build output and temporary QEMU logs |
| `test_display_aarch64.sh` | AArch64 display HAL and serial fallback smoke test | Build output and temporary QEMU logs |
| `test_display_riscv64.sh` | RISC-V display HAL and serial fallback smoke test | Build output and temporary QEMU logs |
| `test_display_gpu.sh` | Experimental VirtIO-GPU build and safe-probe integration test | Build output and temporary QEMU logs |
| `test_storage_unit.sh` | Host-side storage API, write-policy, and partition parser unit tests | `build/storage-tests/` |
| `test_input_unit.sh` | Host-side input ABI, queue, HID boot decoder, and PS/2 decoder unit tests | `build/input-tests/` |
| `test_display_unit.sh` | Host-side framebuffer pixel-format and bounds unit tests | `build/display-tests/` |
| `test_boot_framebuffer_unit.sh` | Host-side Multiboot framebuffer handoff parser unit tests | `build/boot-framebuffer-tests/` |
| `test_storage.sh` | Storage unit tests, all-ISA boot tests, x86_64 transitional VirtIO-Block runtime completion, and AArch64/RISC-V VirtIO-Block builds | Build output and temporary QEMU logs |
| `test_process.sh` | x86_64 process page-table creation and independent anonymous mapping test | `build/process-x86_64/` |
| `build_user_init.sh` | Build the freestanding static `/init` ELF for x86_64, AArch64, or RISC-V 64 | `build/userland-<arch>/` |
| `create_initrd.py` | Pack a static userspace ELF into the Omega initrd format | Configured output path |
| `test_userland.sh` | Build `/init`, pack an initrd, and verify x86_64 PT_LOAD mapping, Ring 3 entry, and `syscall` output in QEMU | `build/userland-x86_64/` |
| `test_security.sh` | Linux UID/GID, supplementary-group, mode, and VFS permission tests | `build/security-tests/` |
| `test_elf_loader.sh` | Linux ELF64 executable/shared-object validation tests | `build/elf-tests/` |
| `test_scripts_unit.py` | Python emulator manager, profile, archive, snapshot, readiness, VNC, GUI-import, and dry-run tests | Temporary files only |
| `../emulator/test_profile_catalog.py` | Python profile schema/catalog and deterministic rendering tests | Temporary files only |
| `../emulator/test_profile_ext4_integration.py` | Python profile-backed ext4 policy tests | Temporary files only |

These scripts are stateless by design. Named device lifecycle, snapshots,
QMP state, and GUI controls are provided by [`../emulator/README.md`](../emulator/README.md).

The profile catalog is tested independently with:

```sh
python3 -m unittest emulator.test_profile_catalog
```

It does not require QEMU or filesystem-image tooling. Native profile artifact
dry-runs report the latest-kernel path, matching manifest, ext4 image path, and
required host tools without creating placeholder images.

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
./scripts/run_qemu.sh --storage-image /tmp/test.img --readonly --dry-run
./scripts/run_qemu.sh --network user --initrd build/initrd.img --dry-run
./scripts/run_qemu.sh --ephemeral --qmp
./scripts/run_qemu.sh --no-build --dry-run
```

Additional launcher options are `--network none|user|socket`, `--initrd
FILE`, `--readonly`, `--ephemeral`, `--qmp`, `--no-build`, and
`--dry-run`. The quick launcher is intentionally stateless; use the OVD
manager for named devices, PID/log state, snapshots, and import/export.

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

The launcher also accepts `--gui`, `--text`, `--headless`, `--storage-image`,
`--network`, `--initrd`, `--readonly`, `--ephemeral`, `--qmp`, `--dry-run`,
and `--no-build`. It does not maintain PID or log state; use the OVD manager
for managed `start`, `stop`, `status`, and `logs` operations.

## Disk image generation

```sh
./scripts/create_bootable_disk.sh
./scripts/create_bootable_disk.sh --arch aarch64 --size 128
./scripts/create_bootable_disk.sh --output-dir /tmp/omega-images --no-build
./scripts/create_bootable_disk.sh --dry-run
./scripts/test_disk_images.sh
```

The generator builds each selected architecture and creates a configurable
size FAT32-compatible
raw image containing the architecture-specific EFI payload under
`EFI/BOOT/` and the kernel under `boot/omega.elf`. If `qemu-img` is
available, QCOW2, VMDK, and VDI conversions are also generated. If `mtools`
is unavailable, the raw image is still created but FAT32 population and
payload verification are limited by the host environment.

Generator options are `--arch ARCH`, `--size MB`, `--output-dir DIR`,
`--no-build`, and `--dry-run`. Existing build directories are reused instead
of deleted. Selected image filenames are overwritten, so use `--output-dir`
for experiments.

## Storage verification

```sh
./scripts/test_storage_unit.sh
./scripts/test_storage.sh
./scripts/test_process.sh
./scripts/test_security.sh
./scripts/test_elf_loader.sh
./scripts/test_userland.sh
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
checks the storage-core pass markers. It additionally boots an x86_64 QEMU PC
with a transitional `virtio-blk-pci` device and asserts read, write/read, and
flush completion markers. The opt-in VirtIO-Block configuration is still
cross-built for AArch64 and RISC-V; their MMIO runtime completion remains
experimental. The concrete NVMe, AHCI, SDHCI, USB MSC, ATAPI, and filesystem
drivers remain planned milestones.

Profile-backed ext4 integration is run independently with:

```sh
python3 -m unittest emulator.test_profile_ext4_integration
```

It always validates the artifact dry-run contract. If `mke2fs` or `mkfs.ext4`
is installed, it also builds the current-kernel ext4 image, creates an OVD from
the x86_64 profile, verifies the manifest/copy, and checks the generated QEMU
command. Without either tool it reports an explicit skip rather than creating
an invalid placeholder image.

QEMU test processes are tracked and cleaned up on exit. The tests reuse build
directories instead of deleting them, making them safer for local development
and parallel workflows.

## Full regression suite

```sh
./scripts/test.sh
```

The full suite runs architecture boot checks, display tests, storage tests,
Python emulator unit tests, and OVD lifecycle tests. QEMU
processes are deliberately terminated after the expected boot markers are
observed; `Killed: 9` from the shell is therefore expected during these
bounded idle-loop tests.

The OVD unit suite additionally covers safe-name/resource validation, schema
compatibility, daemon state, fake-QEMU lifecycle, snapshots, clone,
export/import, and GUI contracts. Use `OMEGA_OVD_ROOT` and
`OMEGA_BUILD_ROOT` to isolate state in automation.

The normal test order is:

1. Multi-architecture kernel builds and boot assertions.
2. x86_64 VGA and AArch64 display suites.
3. Storage unit/integration tests and experimental VirtIO-Block builds.
4. Python emulator manager and profile unit tests.
5. OVD Tkinter GUI-import, VNC protocol, and lifecycle tests.

Run a narrow suite while diagnosing failures:

```sh
python3 scripts/test_scripts_unit.py
./scripts/test_storage.sh
./scripts/test_display.sh
python3 -m emulator.ovd_gui
```

`test_disk_images.sh` regenerates disk images and should normally be run with
an isolated `OMEGA_IMAGE_ROOT`.

## Conventions and safety

- Use `bash script.sh` when the executable bit is unavailable.
- Scripts fail fast with `set -e` or `set -euo pipefail`.
- Unit tests use temporary directories and clean them with traps.
- Image-generation scripts intentionally write under `OMEGA_BUILD_ROOT` and
  `OMEGA_IMAGE_ROOT`; review those paths before running in automation.
- Set `OMEGA_BUILD_ROOT` and `OMEGA_IMAGE_ROOT` to isolate builds and images:

  ```sh
  export OMEGA_BUILD_ROOT="$PWD/.omega-test/build"
  export OMEGA_IMAGE_ROOT="$PWD/.omega-test/images"
  ```

- `test.sh` no longer removes existing build directories.
- No script enables experimental kernel drivers in the default build. Opt-in
  VirtIO-Block builds are isolated under dedicated build directories by
  `test_storage.sh`.

## Troubleshooting

Check dependencies:

```sh
command -v cmake
command -v qemu-system-x86_64
command -v qemu-system-aarch64
command -v qemu-system-riscv64
command -v qemu-img
command -v mformat
```

`qemu-img` is required for converted image formats. `mtools` is required to
populate and inspect FAT32 payloads. Raw images can still be generated
without either optional dependency.

If a kernel ELF is missing, build directly with full output:

```sh
cmake -S . -B build/x86_64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-toolchain.cmake \
  -DARCH=x86_64
cmake --build build/x86_64
```

Use `--no-build` when the launcher should fail immediately instead of
configuring a build. Use `--dry-run` to inspect QEMU arguments without
executing QEMU.

Common outputs include:

```text
build/<arch>/omega.elf
build/<arch>_test.log
build/<arch>_storage_test.log
build/x86_64/display_test.log
build/storage-tests/
disk_images/omega-*-bootable.*
```

The full test suite intentionally terminates QEMU after expected boot
markers. Shell messages such as `Killed: 9` are expected for these bounded
idle-loop tests. Never point image-generation or test scripts at production
disks; use `OMEGA_BUILD_ROOT` and `OMEGA_IMAGE_ROOT` for isolated workflows.
