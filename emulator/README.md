# Omega Virtual Device Emulator

The `emulator/` directory provides the Omega Virtual Device (OVD) manager,
QEMU launcher, graphical manager, and emulator-focused tests. OVDs are local
configuration directories under `emulator/ovd/` containing a configuration
file and a backing `userdata.img` disk image.

## Components

| File | Role |
| --- | --- |
| `ovd_manager.sh` | Create, list, and delete OVD configurations and disk images |
| `ovd_run.sh` | Launch an OVD through QEMU or print its command with `--dry-run` |
| `ovd_gui.tcl` | Tcl/Tk manager for creating, listing, launching, and deleting OVDs |
| `test_ovd_unit.sh` | Non-booting unit tests for configuration and command construction |
| `test_ovd.sh` | QEMU lifecycle and boot integration tests for all three architectures |
| `test_ovd_gui.tcl` | Static Tcl/Tk GUI contract tests |

## Create and manage an OVD

```sh
./emulator/ovd_manager.sh create \
    --name phone \
    --arch x86_64 \
    --ram 1024 \
    --disk 64 \
    --storage virtio

./emulator/ovd_manager.sh list
./emulator/ovd_manager.sh delete --name phone
```

Supported architectures are `x86_64`, `aarch64`, and `riscv64`. New OVDs
default to 1024 MB of RAM, a 64 MB raw disk, and the `virtio` storage
profile. The generated `config.ini` contains:

```ini
ovd.name=phone
ovd.arch=x86_64
ovd.ram=1024
ovd.disk=64
ovd.storage=virtio
ovd.storage.image=userdata.img
ovd.storage.readonly=false
ovd.vga=std
```

The manager validates the architecture, storage profile, and required name.
Deletion removes the selected OVD directory and its backing image.

## Launch an OVD

```sh
./emulator/ovd_run.sh run --name phone --gpu
./emulator/ovd_run.sh run --name phone --no-gpu
./emulator/ovd_run.sh run --name phone --storage virtio --dry-run
```

`--gpu` enables Standard VGA on x86_64 and requests the experimental
VirtIO-GPU device on AArch64/RISC-V. `--no-gpu` uses headless serial output.
The launcher builds the architecture-specific kernel if the expected ELF is
missing.

## Storage transport profiles

The launcher exposes the same `userdata.img` through selectable QEMU device
models:

| Profile | x86_64 | AArch64/RISC-V | QEMU model |
| --- | --- | --- | --- |
| `virtio` | Yes | Yes | `virtio-blk-pci` / `virtio-blk-device` |
| `ahci` | Yes | Rejected | IDE disk attachment for SATA/ATA-style testing |
| `usb` | Yes | Yes | `usb-storage` |
| `sd` | Yes | Yes | SD drive attachment |
| `optical` | Yes | Command construction supported | `ide-cd`, read-only media |
| `none` | Yes | Yes | No storage device |

Select a profile at launch to override the profile stored in `config.ini`:

```sh
./emulator/ovd_run.sh run --name phone --storage usb --no-gpu
./emulator/ovd_run.sh run --name phone --storage optical --dry-run
./emulator/ovd_run.sh run --name phone --storage none --dry-run
```

`--dry-run` is the safest way to inspect the final QEMU arguments. It prints
the command and never starts QEMU. It also makes the launcher unit-testable
without requiring a graphical desktop or a running virtual machine.

These profiles describe QEMU device wiring. They do not imply that the
corresponding kernel driver is complete. The kernel currently has the common
storage API, synthetic writable backend, partition parser, and guarded
experimental VirtIO-Block implementation. NVMe, AHCI, SDHCI, USB Mass
Storage, ATAPI, filesystem mounting, and production hardware writes remain
separate implementation milestones.

## GUI manager

```sh
./emulator/ovd_gui.tcl
```

The GUI requires Tcl/Tk and provides device creation, listing, GUI launch,
headless launch, and deletion. The GUI currently uses the manager defaults,
including the default `virtio` storage profile. Use `ovd_run.sh` directly when
you need to select a different storage profile or inspect a dry-run command.

## Tests

### OVD unit tests

```sh
./emulator/test_ovd_unit.sh
```

This suite does not boot QEMU. It verifies:

- Bash syntax for OVD scripts;
- missing-name and invalid-profile rejection;
- persistence of architecture, disk, and storage configuration;
- OVD listing and cleanup;
- dry-run command generation for VirtIO, AHCI, USB, SD, optical, and none;
- correct QEMU device model for each profile;
- AHCI rejection on AArch64;
- cleanup of temporary OVD state.

### OVD integration tests

```sh
./emulator/test_ovd.sh
```

The integration suite creates temporary x86_64, AArch64, and RISC-V OVDs,
checks storage profile wiring, boots each through QEMU, verifies architecture
and display markers, and deletes the OVDs afterward. QEMU is stopped after
the expected boot output; the resulting `Killed: 9` shell message is normal
for the bounded test.

### GUI tests

```sh
tclsh emulator/test_ovd_gui.tcl
```

The GUI test suite performs static contract checks for SimpleFb text,
VirtIO-GPU wiring, and required GUI controls. It does not open a GUI window.

## Troubleshooting

- If an OVD does not exist, create it with `ovd_manager.sh create` first.
- If the kernel ELF is missing, `ovd_run.sh` attempts an architecture-specific
  CMake build; verify the corresponding toolchain file and LLVM installation.
- If GUI mode fails on Linux, use `--no-gpu` or set `DISPLAY` correctly.
- If a storage profile is not accepted, check the spelling against the six
  supported values above.
- Use `--dry-run` before launching to distinguish QEMU command construction
  errors from kernel-driver or device-completion issues.
