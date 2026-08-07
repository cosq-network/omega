# Omega Virtual Device Emulator

The Omega Virtual Device (OVD) Emulator provides a consistent way to create,
configure, launch, inspect, test, and remove QEMU-based Omega virtual
devices. It supports the project’s three target architectures:

- `x86_64`
- `aarch64`
- `riscv64`

The emulator is intended for kernel development, architecture bring-up,
storage/display testing, CI, and interactive experimentation. It is not a
full desktop virtualization product; the QEMU device models and kernel
drivers are still under active development.

## Quick start

From the repository root:

```sh
# Create a 1 GiB x86_64 OVD with a 64 MiB VirtIO disk.
./emulator/ovd_manager.sh create \
    --name phone \
    --arch x86_64 \
    --ram 1024 \
    --disk 64 \
    --storage virtio

# Inspect the generated QEMU command without starting QEMU.
./emulator/ovd_run.sh run --name phone --no-gpu --dry-run

# Launch headless.
./emulator/ovd_run.sh run --name phone --no-gpu

# Launch in the background.
./emulator/ovd_manager.sh start --name phone --no-gpu --daemon

# Check and stop it.
./emulator/ovd_manager.sh status --name phone
./emulator/ovd_manager.sh stop --name phone

# Remove the OVD after stopping it.
./emulator/ovd_manager.sh delete --name phone
```

## Requirements

Required for normal use:

- Bash 4+;
- QEMU system binaries:
  - `qemu-system-x86_64`;
  - `qemu-system-aarch64`;
  - `qemu-system-riscv64`;
- CMake;
- Clang/LLVM and the Omega architecture toolchains;
- `make` or a supported CMake build backend.

Optional tools:

- Tcl/Tk for `ovd_gui.tcl`;
- `qemu-img` for snapshots and image conversion;
- `nc` with Unix-socket support for QMP shutdown;
- `mtools` and `qemu-img` for bootable disk-image workflows.

The launcher builds the requested kernel automatically if the expected ELF is
missing. Building in advance usually gives clearer output and faster startup:

```sh
cmake -S . -B build/x86_64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-toolchain.cmake \
  -DARCH=x86_64
cmake --build build/x86_64
```

## OVD layout

By default, OVDs are stored under `emulator/ovd/`:

```text
emulator/ovd/phone/
├── config.ini
├── userdata.img
└── state/
    ├── command.argv
    ├── qemu.log
    ├── qemu.pid
    ├── qmp.sock
    └── network.sock
```

The runtime files are created only when the corresponding features are used.
`state/` must not be copied into a running clone or treated as persistent
device data.

## OVD configuration

New devices use schema version 1:

```ini
ovd.schema=1
ovd.name=phone
ovd.arch=x86_64
ovd.ram_mb=1024
ovd.disk_mb=64
ovd.storage.profile=virtio
ovd.storage.image=userdata.img
ovd.storage.format=raw
ovd.storage.readonly=false
ovd.display.profile=standard-vga
ovd.network.profile=none
ovd.initrd=
```

The manager validates architecture, resource sizes, names, storage profiles,
network profiles, display profiles, and configured image paths. Older configs
using `ovd.ram`, `ovd.disk`, or `ovd.storage` remain readable through
compatibility fallbacks.

Use validation before launching an existing or imported OVD:

```sh
./emulator/ovd_manager.sh validate --name phone
./emulator/ovd_manager.sh show --name phone
```

## Creating devices

```sh
./emulator/ovd_manager.sh create \
  --name laptop \
  --arch x86_64 \
  --ram 4096 \
  --disk 512 \
  --storage virtio \
  --network user \
  --display standard-vga
```

Create an AArch64 or RISC-V device:

```sh
./emulator/ovd_manager.sh create \
  --name tablet \
  --arch aarch64 \
  --ram 2048 \
  --disk 128 \
  --storage virtio \
  --network none \
  --display simplefb
```

Attach an initrd during creation:

```sh
./emulator/ovd_manager.sh create \
  --name recovery \
  --arch x86_64 \
  --initrd build/initrd.img
```

Valid names contain 1–64 letters, numbers, `.`, `_`, or `-`. Path traversal,
slashes, whitespace, and shell metacharacters are rejected.

RAM and disk values are specified in MiB. Current validation limits are:

- RAM: 128–1,048,576 MiB;
- disk: 1–16,777,216 MiB.

## Managing devices

List devices in human-readable form:

```sh
./emulator/ovd_manager.sh list
```

Example machine-readable output:

```sh
./emulator/ovd_manager.sh list --json
```

Inspect state and logs:

```sh
./emulator/ovd_manager.sh status --name laptop
./emulator/ovd_manager.sh logs --name laptop
```

Start and stop devices:

```sh
./emulator/ovd_manager.sh start --name laptop --no-gpu --daemon
./emulator/ovd_manager.sh stop --name laptop
./emulator/ovd_manager.sh stop --name laptop --force
```

`--force` is intended for unresponsive or stale processes. Normal shutdown
uses QMP when available and falls back to signals.

Delete a stopped device:

```sh
./emulator/ovd_manager.sh delete --name laptop
```

Delete a running device only when intentionally discarding it:

```sh
./emulator/ovd_manager.sh delete --name laptop --force
```

## Launching devices

The low-level launcher accepts the full option set:

```sh
./emulator/ovd_run.sh run --name phone [options]
```

Options:

| Option | Description |
| --- | --- |
| `--gpu` | Enable graphical display behavior |
| `--no-gpu` | Force serial/headless behavior |
| `--storage PROFILE` | Override the configured storage profile |
| `--storage-image FILE` | Use an external storage image |
| `--network PROFILE` | Override the configured network profile |
| `--initrd FILE` | Attach an initrd image |
| `--readonly` | Open the storage image read-only |
| `--ephemeral` | Use QEMU snapshot mode and discard disk writes on exit |
| `--qmp` | Enable QMP for a foreground launch |
| `--no-qmp` | Disable QMP, including daemon-mode QMP |
| `--daemon` | Run in background and record lifecycle state |
| `--dry-run` | Print the exact QEMU command without starting it |

Examples:

```sh
./emulator/ovd_run.sh run --name phone --gpu
./emulator/ovd_run.sh run --name phone --no-gpu
./emulator/ovd_run.sh run --name phone --ephemeral --no-gpu
./emulator/ovd_run.sh run --name phone --network user --daemon
./emulator/ovd_run.sh run --name phone --initrd build/initrd.img --dry-run
./emulator/ovd_run.sh run --name phone --storage-image /tmp/test.img --readonly
```

`--dry-run` is the recommended first step when diagnosing QEMU arguments. It
also works without starting a VM and records the generated arguments in
`state/command.argv`.

## Display profiles

| Architecture | Display behavior |
| --- | --- |
| x86_64 | Standard VGA / Bochs VBE with optional SDL or Cocoa output |
| AArch64 | SimpleFb/serial fallback; experimental VirtIO-GPU with `--gpu` |
| RISC-V | SimpleFb/serial fallback; experimental VirtIO-GPU with `--gpu` |

On Linux, graphical mode uses SDL when `DISPLAY` is set. On macOS it uses
Cocoa. If no graphical session is available, use `--no-gpu` or inspect the
command with `--dry-run`.

## Storage profiles

Storage profiles select the QEMU transport for the OVD backing image:

| Profile | x86_64 | AArch64 | RISC-V | QEMU model |
| --- | --- | --- | --- | --- |
| `virtio` | Yes | Yes | Yes | `virtio-blk-pci` / `virtio-blk-device` |
| `ahci` | Yes | No | No | IDE/ATA-style disk attachment |
| `usb` | Yes | Yes | Yes | USB Mass Storage model |
| `sd` | Yes | Yes | Yes | SD drive attachment |
| `optical` | Yes | Yes* | Yes* | Read-only `ide-cd` model |
| `none` | Yes | Yes | Yes | No storage device |

`*` Command construction is supported; runtime availability depends on the
QEMU machine’s controller model.

Examples:

```sh
./emulator/ovd_run.sh run --name phone --storage virtio --dry-run
./emulator/ovd_run.sh run --name phone --storage usb --no-gpu
./emulator/ovd_run.sh run --name phone --storage optical --dry-run
./emulator/ovd_run.sh run --name phone --storage none --dry-run
```

The current kernel storage implementation includes a common storage layer,
synthetic writable backend, GPT/MBR parser, and guarded experimental
VirtIO-Block path. NVMe, AHCI, SDHCI, USB MSC, ATAPI, filesystem mounting,
and production hardware writes remain separate kernel milestones.

## Network profiles

| Profile | Behavior |
| --- | --- |
| `none` | No network device is attached |
| `user` | QEMU user-mode networking with a VirtIO network device |
| `socket` | Unix-socket network endpoint for multi-VM experiments |

Examples:

```sh
./emulator/ovd_run.sh run --name phone --network none --dry-run
./emulator/ovd_run.sh run --name phone --network user --daemon
./emulator/ovd_run.sh run --name phone --network socket --dry-run
```

The network profile controls QEMU wiring. It does not imply that the Omega
kernel network stack has a complete device initialization path for every
architecture or transport.

## Initrd, external images, and ephemeral mode

Attach an initrd:

```sh
./emulator/ovd_run.sh run --name phone --initrd build/initrd.img --dry-run
```

Use an external image explicitly selected by the user:

```sh
./emulator/ovd_run.sh run \
  --name phone \
  --storage-image /tmp/omega-test.img \
  --storage virtio \
  --readonly
```

Use a disposable disk view:

```sh
./emulator/ovd_run.sh run --name phone --ephemeral --no-gpu
```

Ephemeral mode passes QEMU’s `-snapshot` option. It does not create a named
OVD snapshot; use the snapshot commands below for persistent image snapshots.

## Snapshots

Snapshots require `qemu-img` and a compatible image format:

```sh
./emulator/ovd_manager.sh snapshot create \
  --name phone \
  --snapshot clean

./emulator/ovd_manager.sh snapshot list --name phone
./emulator/ovd_manager.sh snapshot apply --name phone --snapshot clean
```

Stop the device before creating or applying a snapshot. Snapshot operations
modify the backing image and should not be interrupted.

## Clone, export, and import

Clone an OVD without copying runtime PID, log, or socket state:

```sh
./emulator/ovd_manager.sh clone \
  --name phone \
  --new-name phone-copy
```

Export and import an OVD:

```sh
./emulator/ovd_manager.sh export \
  --name phone \
  --output phone.tar.gz

./emulator/ovd_manager.sh import \
  --archive phone.tar.gz \
  --name phone-restored
```

Imported archives are validated before the OVD becomes usable. Do not import
archives from untrusted sources without reviewing their contents.

## GUI manager

Launch the Tcl/Tk GUI:

```sh
./emulator/ovd_gui.tcl
```

The GUI supports:

- OVD creation;
- architecture, RAM, and disk selection;
- storage and network profile selection;
- device listing with architecture, RAM, disk, storage, and state;
- graphical and headless launch;
- daemon-backed stop operations;
- log viewing;
- deletion with confirmation;
- periodic state refresh.

Use the CLI for advanced options such as external images, initrds, snapshots,
ephemeral mode, import/export, and dry-run command inspection.

## Environment isolation

Use these variables to keep OVD state outside the repository:

```sh
export OMEGA_OVD_ROOT="$PWD/.omega-test/ovd"
export OMEGA_BUILD_ROOT="$PWD/.omega-test/build"
export OMEGA_IMAGE_ROOT="$PWD/.omega-test/images"
```

This is recommended for CI, parallel worktrees, and destructive test runs.
The current launcher still resolves kernel source and toolchain files from the
repository root.

## Testing

Unit tests do not boot QEMU:

```sh
./emulator/test_ovd_unit.sh
tclsh emulator/test_ovd_gui.tcl
```

The OVD unit suite verifies:

- shell syntax;
- invalid names, profiles, architectures, RAM, and disk sizes;
- schema/configuration persistence;
- human-readable and JSON listing;
- all storage profile dry-runs;
- architecture/profile compatibility;
- daemon startup, status, and stop with fake QEMU;
- clone and validation;
- export/import;
- snapshot command dispatch with fake `qemu-img`;
- cleanup behavior.

The lifecycle suite boots real QEMU instances:

```sh
./emulator/test_ovd.sh
```

It covers x86_64, AArch64, and RISC-V boot markers, display fallback, and
storage command wiring. The broader project suite is:

```sh
./scripts/test.sh
```

The test scripts intentionally terminate QEMU after expected boot markers.
`Killed: 9` messages from those bounded idle-loop tests are expected.

## Troubleshooting

### OVD does not exist

Create or list devices:

```sh
./emulator/ovd_manager.sh list
./emulator/ovd_manager.sh create --name phone --arch x86_64
```

### Configuration is invalid

Run:

```sh
./emulator/ovd_manager.sh validate --name phone
```

Check architecture, RAM, disk size, storage profile, image path, and network
profile.

### QEMU is not found

Install the architecture-specific QEMU system binary and confirm:

```sh
command -v qemu-system-x86_64
command -v qemu-system-aarch64
command -v qemu-system-riscv64
```

### Graphical mode does not open

Use headless mode:

```sh
./emulator/ovd_run.sh run --name phone --no-gpu
```

On Linux, verify `DISPLAY`; on macOS, use Cocoa through the default GUI
launcher.

### Device is reported as already running

Inspect state and stop it:

```sh
./emulator/ovd_manager.sh status --name phone
./emulator/ovd_manager.sh stop --name phone --force
```

Review `state/qemu.pid`, `state/qemu.log`, and `state/command.argv` if the
process is no longer present but stale state remains.

### Storage profile fails at runtime

First inspect the command:

```sh
./emulator/ovd_run.sh run --name phone --storage virtio --dry-run
```

Some profiles are currently command-construction and emulator-model tests;
the corresponding kernel hardware driver may still be experimental or
unimplemented.

## Safety notes

- OVD names are validated before filesystem operations.
- Deleting a running OVD requires `--force`.
- Use isolated roots for automated tests.
- Stop devices before snapshot, clone, export, or delete operations.
- Review imported archives before using them.
- Never use a production disk image as an OVD backing image without a backup.
- The emulator is a development tool; it is not a security boundary.
