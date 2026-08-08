# Omega Virtual Device Emulator

The Omega Virtual Device (OVD) manager is implemented entirely in Python. It
uses `pathlib`, `tarfile`, `socket`, `subprocess.Popen(shell=False)`, and the
standard-library `tkinter` GUI. The emulator manager does not require Bash,
Zsh, Tcl source files, `nc`, `awk`, `sed`, `cp`, `tar`, or shell pipelines.

QEMU is the optional direct-process backend for running a guest. The manager
constructs an argument vector and launches it with `shell=False`; it never
passes the command through a shell. Dry-runs, profile inspection,
configuration management, archives, and snapshot copies work with Python and
the standard library alone.

## Requirements

Python 3.10 or newer is recommended. The GUI additionally requires a Python
installation that includes Tkinter. QEMU is required only for an actual guest
launch, and the matching executable must be available on `PATH`:

| Architecture | QEMU executable |
| --- | --- |
| `x86_64` | `qemu-system-x86_64` |
| `aarch64` | `qemu-system-aarch64` |
| `riscv64` | `qemu-system-riscv64` |
| `armv7` | `qemu-system-arm` (conditional board profiles) |

The Python manager itself is cross-platform on Windows, Linux, and macOS.
QEMU display backends and Omega kernel builds remain host prerequisites.

## Entry points

From the repository root:

```text
python3 -m emulator.ovd_cli --help
python3 -m emulator --help
python3 -m emulator.ovd_manager --help
python3 -m emulator.ovd_run run --help
python3 -m emulator.ovd_gui
```

`ovd_manager.py`, `ovd_run.py`, and `profile_catalog.py` can also be executed
directly with Python. On Windows, use `py -m emulator` or `py -m
emulator.ovd_gui` when the Python launcher is installed.

## Configuration roots

The defaults are repository-local:

| Purpose | Default | Override |
| --- | --- | --- |
| OVD instances | `emulator/ovd/` | `OMEGA_OVD_ROOT` |
| Kernel builds | `build/` | `OMEGA_BUILD_ROOT` |
| Profile images | `disk_images/` | `OMEGA_IMAGE_ROOT` |

All paths are resolved with `pathlib`. OVD names and configured image paths are
validated against traversal and absolute-path escapes before use.

## Create and manage an OVD

```text
python3 -m emulator.ovd_cli create \
  --name phone --arch x86_64 --ram 1024 --disk 64 \
  --storage virtio --network none

python3 -m emulator.ovd_cli list --json
python3 -m emulator.ovd_cli show --name phone
python3 -m emulator.ovd_cli validate --name phone
python3 -m emulator.ovd_cli status --name phone
python3 -m emulator.ovd_cli logs --name phone
python3 -m emulator.ovd_cli stop --name phone
python3 -m emulator.ovd_cli delete --name phone
```

`create` creates a managed OVD directory and a raw, sparse-sized backing file.
That generic file is not automatically formatted as ext4. Use
`create-from-profile` for a profile-backed ext4 image, or provide and manage
the required filesystem image through the project image-generation workflow.
When `disk_images/omega-<arch>-bootable.img` exists, generic creation copies it
into the OVD automatically. A specific bootable image can be selected with
`--boot-image`; otherwise creation records `ovd.bootable=false` rather than
claiming that a blank disk is bootable. For intentionally blank test media,
use `--allow-blank` explicitly.

QEMU machine models are discovered from the installed QEMU binaries:

```text
python3 -m emulator machines --arch x86_64
python3 -m emulator machines --arch aarch64 --json
python3 -m emulator machines --arch riscv64
python3 -m emulator create --name custom-arm --arch aarch64 --machine virt
```

The discovery command is the Python equivalent of `qemu-system-<arch>
-machine help`. It uses a direct subprocess argument vector and does not
parse a shell command. Machine names are stored in each OVD configuration and
are also selectable in the GUI.

Supported architectures are `x86_64`, `aarch64`, `armv7`, and `riscv64`. Storage
profiles are `auto`, `virtio`, `ahci`, `usb`, `sd`, `optical`, and `none`.
Network profiles are `none`, `user`, and `socket`.

## Predefined profiles

The catalog includes directly launchable generic device classes for the three
supported 64-bit ISAs:

| Profile | Device class | QEMU machine | Default storage |
| --- | --- | --- | --- |
| `x86_64-desktop-q35` | Modern desktop/workstation | `q35` | VirtIO raw image |
| `x86_64-desktop-pc-legacy` | Legacy desktop | `pc` | VirtIO raw image |
| `x86_64-laptop-q35` | Laptop/workstation | `q35` | VirtIO raw image |
| `aarch64-virt-development` | ARM64 board/tablet development | `virt` | VirtIO-MMIO raw image |
| `aarch64-raspi4b-qemu` | Raspberry Pi 4B experimental board | `raspi4b` | SD raw image |
| `riscv64-virt-development` | RV64 development platform | `virt` | VirtIO-MMIO raw image |
| `riscv64-virt-minimal` | Minimal RV64/headless device | `virt` | VirtIO-MMIO raw image |

The following ARMv7 board profiles are catalogued for machine selection and
future board-kernel integration, but are conditional rather than native Omega
profiles because the repository does not yet contain an ARMv7 kernel/toolchain:

- `armv7-raspi1ap-qemu` — Raspberry Pi A+ (`raspi1ap`)
- `armv7-raspi0-qemu` — Raspberry Pi Zero (`raspi0`)
- `armv7-bananapi-m2u-qemu` — Banana Pi M2U (`bpim2u`)
- `armv7-orangepi-pc-qemu` — Orange Pi PC (`orangepi-pc`)

Android AArch64 phone/tablet and Apple VMApple entries are also catalogued,
but remain external-adapter profiles because they require vendor-managed
system images or firmware.

```text
python3 -m emulator.ovd_cli profiles list --json
python3 -m emulator.ovd_cli profiles show \
  --profile aarch64-virt-development --json
python3 -m emulator.ovd_cli profiles validate --json
python3 -m emulator.ovd_cli profiles render \
  --profile riscv64-virt-minimal --json
python3 -m emulator.ovd_cli profiles artifacts \
  --profile x86_64-desktop-q35 --dry-run --json

python3 -m emulator.ovd_cli create-from-profile \
  --profile x86_64-desktop-q35 --name desktop-q35
```

Native Omega profiles require ext4 system images and raw storage containers.
Android AVD and VMApple profiles are classified as external adapters and are
reported without pretending that their proprietary artifacts are native Omega
images. `profiles artifacts` reports whether the expected current kernel and
profile image exist. Optional `<artifact>.sha256` sidecars are checked and a
digest mismatch is reported explicitly; no fake image is created when an
artifact is missing. A profile-backed OVD can be created only after its ext4
image exists.

## Launching

```text
python3 -m emulator.ovd_cli start --name phone --dry-run
python3 -m emulator.ovd_cli start --name phone --no-gpu --daemon
python3 -m emulator.ovd_cli start --name phone --storage usb --dry-run
python3 -m emulator.ovd_cli start --name phone --network user --dry-run
python3 -m emulator.ovd_cli start --name phone --ephemeral --dry-run
python3 -m emulator.ovd_cli start --name phone --vnc 1 --gpu --dry-run
```

`start` writes the exact argument vector to `state/command.json`. Actual
execution uses `subprocess.Popen(..., shell=False)` and records a PID,
executable/command identity, lifecycle state, and QEMU output under the OVD's
`state/` directory. Stop operations verify process identity and terminate the
complete QEMU process session where supported. The `ovd_run` compatibility
entry point maps `run` to the same Python start implementation:

```text
python3 -m emulator.ovd_run run --name phone --no-gpu --dry-run
```

The command requires both the OVD image and the architecture-specific Omega
kernel. Use `--dry-run` to inspect the complete argument vector without
starting QEMU. `--daemon` is accepted for compatibility with managed-launch
workflows; the manager always records the process PID and returns without
creating a service or background shell job.

`--vnc DISPLAY` adds QEMU's local RFB server at `127.0.0.1:5900+DISPLAY`.
The built-in GUI's “Launch with VNC” and “Connect VNC” actions use the
dependency-free Tkinter RFB viewer. It supports long-lived idle sessions,
raw framebuffer updates, composited partial updates, defensive CopyRect and
DesktopSize handling, clipboard text in both directions, mouse
movement/buttons/wheel, and keyboard events. VNC connection attempts run off
the Tkinter event loop and retry while QEMU is starting. Clipboard and HID
devices are attached to QEMU by default; use
`--no-clipboard` when required. A VNC session requests a graphical QEMU
device even for a normally headless profile, because VNC cannot display a
serial-only console.

## Snapshots, cloning, and archives

Snapshots are portable file copies and therefore work for raw images without
`qemu-img`:

```text
python3 -m emulator.ovd_cli snapshot create --name phone --snapshot clean
python3 -m emulator.ovd_cli snapshot list --name phone
python3 -m emulator.ovd_cli snapshot apply --name phone --snapshot clean
python3 -m emulator.ovd_cli clone --name phone --new-name phone-copy
python3 -m emulator.ovd_cli export --name phone --output phone.tar.gz
python3 -m emulator.ovd_cli import --archive phone.tar.gz --name phone-imported
```

Archives are inspected for absolute paths, traversal components, links,
unsupported members, and multiple top-level roots before extraction. The
extractor works on Python 3.10+ without relying on the newer
`TarFile.extractall(filter=...)` API. Imported devices are validated in a
temporary location before being published.

## GUI OVD Manager

The GUI OVD Manager is a Tkinter application for creating, configuring,
launching, monitoring, and stopping Omega Virtual Devices. It uses the same
`OVDManager` backend as the CLI, so GUI-created devices can also be managed
from the command line.

### Getting started

From the repository root:

```text
python3 -m emulator.ovd_gui
```

On Windows, the equivalent command is:

```text
py -m emulator.ovd_gui
```

Before launching the GUI:

1. Install Python 3.10 or newer with Tkinter support.
2. Install the QEMU executable for each architecture you want to run.
3. Make sure the QEMU executables are available on `PATH`.
4. Build or provide the matching Omega kernel and disk image.

The GUI uses these environment variables, exactly like the CLI:

| Variable | Purpose | Default |
| --- | --- | --- |
| `OMEGA_OVD_ROOT` | Persistent OVD definitions and state | `emulator/ovd/` |
| `OMEGA_BUILD_ROOT` | Architecture-specific Omega kernels | `build/` |
| `OMEGA_IMAGE_ROOT` | Bootable and profile-backed images | `disk_images/` |

Example first-run workflow:

```text
python3 -m emulator.ovd_gui
```

Then:

1. Select an architecture in the kernel selector.
2. Click `Build / Refresh Kernel` if the kernel is missing or outdated.
3. Open `Create / Configure`.
4. Select a predefined profile, or leave `(generic OVD)` selected.
5. Enter a unique device name.
6. Review the machine, CPU, RAM, disk, storage, and network settings.
7. Create the OVD.
8. Return to `Devices` and select the new device.
9. Click `Readiness` and resolve any failed checks.
10. Use `Launch Headless`, `Launch GUI`, or `Launch VNC`.

### GUI tabs

#### Devices

The Devices tab provides:

- OVD inventory for multiple devices;
- profile, architecture, QEMU machine, RAM, disk, and storage columns;
- bootable-image status;
- stopped/running state;
- device selection;
- configuration validation;
- readiness diagnostics;
- exact QEMU command preview;
- headless launch;
- graphical launch;
- VNC launch;
- graceful stop;
- force stop;
- deletion with confirmation;
- periodic state refresh;
- selected-device readiness details.

`Readiness` verifies the Omega kernel, disk image, QEMU executable, and
selected machine model before a launch. Unsupported machine combinations,
missing artifacts, and external-only profiles are reported as actionable
errors.

#### Create / Configure

The Create / Configure tab supports:

- predefined profile selection;
- generic OVD creation;
- architecture selection;
- QEMU machine selection from installed QEMU capabilities;
- QEMU CPU selection;
- RAM configuration;
- disk-size configuration;
- storage transport selection;
- network-mode selection;
- VNC display-number configuration;
- form reset;
- architecture-specific kernel status;
- profile-default population.

The available machine list is discovered from the installed QEMU binary using
the equivalent of `qemu-system-<arch> -machine help`. Discovery runs in a
worker thread so a missing or slow QEMU installation does not freeze the GUI.

Native profile creation requires a compatible Omega kernel and bootable image.
External and conditional profiles are visible for inspection but their native
creation action is disabled when they require an external adapter, vendor
firmware, or an unavailable architecture-specific kernel.

#### Console / Diagnostics

The Console / Diagnostics tab provides:

- latest QEMU output;
- refreshable logs;
- generated QEMU command display;
- command copying to the host clipboard;
- launch and failure diagnostics;
- state information for the selected device.

Kernel builds run asynchronously, and errors are shown in the GUI rather than
being lost in a terminal process.

### Supported device and architecture management

The GUI can inspect and configure profiles for:

- x86_64 Q35 desktops and laptops;
- x86_64 legacy PC/i440FX systems;
- AArch64 `virt` development systems;
- AArch64 Raspberry Pi 4B experimental targets;
- RISC-V 64 `virt` development systems;
- RISC-V 64 minimal/headless systems;
- ARMv7 Raspberry Pi A+ and Zero profiles;
- ARMv7 Banana Pi M2U;
- ARMv7 Orange Pi PC;
- Android phone/tablet external profiles;
- Apple Silicon VMApple external profiles.

The directly launchable native targets are the profiles with a supported
Omega kernel, bootable image, QEMU executable, and compatible machine model.
Board and external-adapter profiles remain explicitly classified instead of
being presented as fully validated native devices.

### Launch modes and VNC

The GUI supports:

- headless serial-console launch;
- host graphical QEMU launch where the host display backend is available;
- local VNC launch and connection;
- GPU-enabled launch;
- GPU-disabled launch;
- configurable VNC display numbers;
- clipboard-enabled VNC input;
- clipboard-disabled launch when required.

The integrated VNC viewer requires no external VNC application. It provides:

- raw framebuffer display;
- composited incremental updates;
- keyboard input;
- function and special keys;
- Shift, Control, and Alt modifiers;
- mouse movement;
- left, middle, and right buttons;
- mouse-wheel events;
- host-to-guest clipboard paste;
- guest-to-host clipboard transfer;
- Control-V and Command-V paste shortcuts;
- connection retries while QEMU starts;
- idle-session stability after the initial connection timeout;
- framebuffer, coordinate, socket, and clipboard-size safety limits;
- background connection and reader threads that do not block the GUI;
- graceful socket and Tkinter-window shutdown.

VNC is bound to `127.0.0.1` by default. It is intended for local development;
the built-in viewer supports QEMU's unauthenticated local security mode but
does not provide VNC password authentication or TLS. It intentionally requests
raw framebuffer encoding and rejects unsupported encodings safely.

### Lifecycle and safety behavior

The manager persists runtime information under each OVD's `state/` directory:

- `command.json` — exact QEMU argument vector;
- `qemu.log` — QEMU output;
- `qemu.pid` — compatibility PID record;
- `process.json` — process identity and command digest;
- `lifecycle.json` — starting/running lifecycle metadata;
- QMP and network endpoint state where configured.

Stop operations verify the persisted process identity before sending signals.
QEMU is launched in a separate process session, allowing the manager to stop
the process group on supported hosts. Runtime state is excluded from exported
OVD archives.

The GUI also protects users by:

- validating OVD names and paths;
- preventing path traversal;
- refusing unsupported profile combinations;
- refusing missing runtime artifacts for real launches;
- confirming deletion;
- distinguishing normal stop from force stop;
- avoiding shell commands and shell pipelines.

### GUI and CLI interoperability

The GUI does not create a separate device format. All devices are stored in
the normal OVD directory structure and can be inspected with:

```text
python3 -m emulator.ovd_cli list --json
python3 -m emulator.ovd_cli show --name DEVICE_NAME
python3 -m emulator.ovd_cli status --name DEVICE_NAME
python3 -m emulator.ovd_cli validate --name DEVICE_NAME
python3 -m emulator.ovd_cli logs --name DEVICE_NAME
```

This makes it possible to create a device in the GUI, preview or launch it
from the CLI, and inspect the same logs and lifecycle state from either
interface.

### Current GUI boundaries

The backend already supports snapshots, cloning, archive import/export, and
QMP state management. Dedicated GUI workflows for those operations are still
being expanded. Live QEMU boot, VNC interaction, and full Windows/macOS
process behavior require host-specific integration testing with a desktop
session and installed guest artifacts.

On systems where Python was installed without Tkinter, install a Python build
that includes Tk support. No `wish`, Tcl source file, or platform-specific
window command is used by Omega.

## Python tests

```text
python3 -m unittest emulator.test_ovd_unit \
  emulator.test_profile_catalog \
  emulator.test_profile_ext4_integration \
  emulator.test_vnc emulator.test_gui_module -v
python3 scripts/test_scripts_unit.py
```

The tests exercise validation, traversal protection, deterministic profile
rendering, dry-run command vectors, cloning, archive import/export, snapshots,
and ext4 profile policy, direct-CMake build invocation, process identity,
runtime artifact checks, VNC handshake, idle-session behavior, Bell parsing,
framebuffer conversion, clipboard limits, keyboard/mouse encoding, and
headless GUI imports.
QEMU launches are intentionally tested separately
because they require an installed architecture-specific QEMU binary.

The GUI module is importable in headless CI, but constructing the `Tk()` window
requires an available desktop display. This keeps the core manager testable on
Windows services, Linux CI workers, and macOS terminal sessions without a GUI.

## Python API

```python
from emulator.ovd_core import OVDManager

manager = OVDManager()
manager.create("demo", arch="x86_64", ram=1024, disk=64)
command = manager.start("demo", dry_run=True)
print(command)
```

The primary reusable classes are `OVDManager`, `ProfileCatalog`, `QemuBackend`,
and `EmulatorError` in `emulator/ovd_core.py`.
