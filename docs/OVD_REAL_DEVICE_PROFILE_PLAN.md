# OVD Real-Device Profile Registry and Maintenance Plan

## 1. Purpose

This document defines how Omega should create, validate, publish, execute, and
maintain a predefined catalog of Omega Virtual Device (OVD) profiles that
represent real device classes across x86_64, AArch64, and RISC-V 64.

The catalog is intended to cover:

- desktops and workstations;
- laptops and mobile workstations;
- tablets;
- mobile phones;
- development boards and reference platforms;
- directly emulated QEMU machines;
- Android Emulator/AVD-backed device classes; and
- conditional Apple Virtualization Framework/macOS targets.

### Initial implementation status

The first implementation slice is now present in the repository:

- `emulator/profiles/catalog.json` is the canonical, deterministic catalog;
- `emulator/profiles/schema.json` documents the profile contract;
- `emulator/profile_catalog.py` validates, lists, shows, renders, and performs
  dry-run native artifact resolution;
- `emulator/ovd_manager.sh profiles ...` exposes the catalog operations;
- profile-backed native OVD configuration enforces ext4 and explicit artifact
  policy metadata; and
- `emulator/test_profile_catalog.sh` covers all three native architectures,
  external profile classification, deterministic rendering, and ext4 dry-run
  behavior.

The remaining implementation work is deliberately visible: add external
Android/VMApple adapters and generate CSV/JSON/Markdown derived reports from
the canonical catalog.

The catalog must distinguish a real commercial device from the virtual board
used to approximate it. QEMU can directly model a `q35` PC, `pc` i440FX PC,
ARM `virt`, and RISC-V `virt`; it does not automatically reproduce the
registers, firmware, modem, camera, GPU, or power-management behavior of a
particular Pixel, Galaxy, ThinkPad, or Raspberry Pi. OVD profiles therefore
represent an explicit hardware contract and must not claim physical fidelity
that the backend cannot provide.

## 2. Current baseline and gap

The current OVD implementation supports named device instances with:

- `x86_64`, `aarch64`, and `riscv64` architecture values;
- storage, display, network, RAM, disk, initrd, read-only, ephemeral, and QMP
  options;
- lifecycle commands, snapshots, import/export, JSON listing, logs, and GUI
  state;
- standard VGA, SimpleFb, VirtIO-GPU, and no-display profiles;
- QEMU-backed storage and networking profiles.

The current configuration is an instance-oriented `config.ini`. It does not
yet provide a versioned catalog of hardware profiles with device identity,
firmware requirements, peripherals, display modes, Android AVD metadata,
source references, or compatibility status.

The artifact lifecycle is also incomplete. The native launcher can build a
missing `omega.elf`, but OVD profile creation currently creates a generic raw
zero-filled instance image and does not prove that it contains the latest
kernel or a mounted Omega filesystem. This plan therefore makes artifact
resolution a first-class profile operation.

The planned registry should be additive and backward-compatible:

```text
profile catalog -> validated profile -> OVD instance config -> QEMU/AVD command
```

## 3. Important corrections to the proposed matrix

### 3.1 x86_64 `q35` and `pc`

These are direct QEMU machine targets and are suitable for generic desktop and
laptop classes. They represent virtual PC platforms, not particular Intel,
AMD, Lenovo, HP, or Dell products.

Recommended OVD classifications:

- `direct-qemu`: QEMU machine is directly launchable and validated;
- `generic-device-class`: represents a family such as desktop or laptop;
- `guest-os-candidate`: records possible guest operating systems but does not
  promise that Omega supports them.

macOS should not be listed as a normal `q35` guest target. It has separate
firmware, licensing, and hardware requirements.

### 3.2 Apple `vmapple`

QEMU’s `vmapple` is a conditional macOS-on-Apple-Silicon target. QEMU
documentation requires an Apple-Silicon host running macOS 12 or later and an
already-installed Virtualization Framework macOS virtual machine. The current
QEMU documentation also describes limitations on newer macOS guests.

Therefore the profile must be marked:

```text
backend: qemu-vmapple
status: conditional-external
host: apple-silicon-macos
requires: preinstalled-vf-vm, AVPBooter, UUID, pflash assets
```

It must not be advertised as a generic emulation of an Apple Mac desktop or
as a portable Omega AArch64 target. It is useful for a specialized macOS
guest-validation workflow, not for ordinary CI.

Reference: [QEMU VMApple documentation](https://qemu.readthedocs.io/en/master/system/arm/vmapple.html).

### 3.3 Android phones and tablets

Android Studio’s Emulator is an Android-specific QEMU-based product with AVD
metadata, Android system images, Goldfish/Ranchu device models, graphics
backends, sensors, telephony simulation, and host acceleration. It should not
be modeled as a plain `qemu-system-aarch64 -M virt` profile.

The OVD catalog should represent Android profiles through an external
`android-avd` adapter:

```text
OVD profile -> AVD name/config -> Android emulator executable -> Android image
```

The adapter must report whether it is installed and must not silently fall
back to generic QEMU. Android Emulator can provide predefined phone, tablet,
Wear OS, Automotive, and TV configurations, but these are Android Emulator
profiles rather than faithful commercial-device hardware models.

References: [Android Emulator overview](https://developer.android.com/studio/run/emulator) and [Android Emulator command-line documentation](https://developer.android.com/studio/run/emulator-commandline).

### 3.4 RISC-V `virt`

`qemu-system-riscv64 -M virt` is a direct generic RISC-V virtual platform and
is suitable for RV64 workstation, laptop-class, server-class, and development
profiles. It is not a SiFive, XuanTie, StarFive, or Banana Pi board model.

The catalog should use names such as:

```text
riscv64-virt-generic
riscv64-virt-workstation
riscv64-virt-minimal
```

and record the specific CPU, firmware, memory, disk, virtio devices, and
kernel ABI used by each profile.

Reference: [QEMU RISC-V system emulation](https://qemu.readthedocs.io/en/master/system/target-riscv.html).

## 4. Registry architecture

### 4.1 Source of truth

Use a declarative, reviewable profile registry as the source of truth. The
preferred initial format is YAML or JSON because it is easy to validate,
diff, parse from shell tooling, and consume from future GUI code.

Recommended layout:

```text
emulator/
├── profiles/
│   ├── schema.json
│   ├── catalog.yaml
│   ├── x86_64/
│   │   ├── desktop-q35-linux.yaml
│   │   ├── desktop-pc-legacy.yaml
│   │   ├── laptop-q35.yaml
│   │   └── apple-vmapple-macos.yaml
│   ├── aarch64/
│   │   ├── generic-virt.yaml
│   │   ├── android-phone-avd.yaml
│   │   ├── android-tablet-avd.yaml
│   │   └── apple-vmapple-macos.yaml
│   └── riscv64/
│       ├── virt-generic.yaml
│       └── virt-workstation.yaml
├── profile_catalog.py
└── profile_catalog.lock
```

The existing Python/Pandas dataset can be imported as an analysis artifact,
but Pandas CSV output must not become the runtime source of truth. OVD needs
typed fields, nested capabilities, enum validation, versioning, and backend
parameters that a flat CSV cannot represent safely.

Generate derived artifacts:

- `qemu_64bit_refined_devices.csv` for spreadsheet/reporting use;
- `qemu_64bit_refined_devices.json` for automation;
- Markdown tables for documentation;
- profile index JSON for the GUI and CLI;
- a deterministic lock file containing profile IDs, revisions, and hashes.

### 4.2 Stable identity

Every profile receives:

```text
profile_id: x86_64-desktop-q35-linux
profile_version: 1.0.0
schema_version: 1
family: desktop
architecture: x86_64
backend: qemu
status: supported|experimental|conditional-external|retired
```

The `profile_id` must never be reused for a materially different machine.
Create a new profile ID or major revision when the machine model, firmware
contract, storage topology, display model, or boot semantics change.

## 5. Profile schema

Each profile should contain the following sections.

### 5.1 Identity and classification

```yaml
profile_id: x86_64-desktop-q35-linux
profile_version: 1.0.0
schema_version: 1
display_name: Generic x86_64 Desktop on Q35
form_factor: desktop
architecture: x86_64
cpu_architecture: x86_64
backend: qemu
status: supported
owner: omega-emulator
tags: [desktop, q35, pci, uefi, linux]
```

Allowed form factors:

```text
desktop, laptop, tablet, mobile, workstation, server, development-board
```

### 5.2 QEMU execution contract

```yaml
qemu:
  executable: qemu-system-x86_64
  machine: q35
  cpu: max
  firmware:
    type: uefi
    required: false
    package: ovmf
  acceleration:
    optional: true
    preferred: hvf
  console: serial
  command_policy: generated
```

Do not store arbitrary shell command strings as the primary representation.
Use structured fields and generate an argument array. Raw argument additions
may be supported through a controlled `extra_args` list with validation and an
explicit review flag.

### 5.3 Memory and storage

```yaml
memory:
  default_mb: 2048
  minimum_mb: 512
  maximum_mb: 16384
storage:
  boot:
    kind: virtio-blk
    format: raw
    required: true
  data:
    kind: virtio-blk
    format: qcow2
    optional: true
  removable:
    - kind: usb-storage
    - kind: sd
```

Storage profiles must reference the existing storage abstraction and OVD
profile names. They must not create a second incompatible storage taxonomy.

### 5.4 Display and input

```yaml
display:
  backend: standard-vga
  preferred_modes:
    - [1920, 1080, 32]
    - [1280, 720, 32]
    - [1024, 768, 32]
  outputs: 1
  edid: optional
input:
  mouse: usb-tablet
  keyboard: usb-kbd
  touch: optional
  multitouch: optional
```

For Android AVD profiles, display and input fields should be translated to
AVD configuration rather than directly appended as generic QEMU devices.

### 5.5 Communications and peripherals

```yaml
communications:
  serial:
    - type: uart
      backend: stdio
  network:
    - model: virtio-net
      mode: user
  usb:
    controller: xhci
    devices: [hid, storage]
  wifi:
    mode: synthetic
```

The profile should state whether a feature is:

```text
emulated, synthetic, host-forwarded, passthrough, unavailable, external-adapter
```

For example, a QEMU `user` network is not a physical Ethernet PHY, and a
synthetic Wi-Fi profile is not an RF simulation.

### 5.6 Firmware and guest metadata

```yaml
guest:
  supported_os_families: [omega, linux, freebsd]
  os_claims: informational
firmware:
  boot_protocols: [uefi, multiboot2]
  required_assets: []
  external_dependencies: []
references:
  - title: QEMU machine documentation
    url: https://qemu.readthedocs.io/
  - title: Omega architecture plan
    path: docs/ARCHITECTURE.md
```

`supported_os_families` is descriptive. It must not imply that OVD validates
Windows, macOS, Android, or a BSD guest unless a separate test suite exists.

## 6. Initial profile catalog

### 6.1 Direct QEMU profiles

| Profile ID | Architecture | Form factor | Backend | Status |
| --- | --- | --- | --- | --- |
| `x86_64-desktop-q35` | x86_64 | desktop/workstation | QEMU `q35` | planned |
| `x86_64-desktop-pc-legacy` | x86_64 | desktop/legacy laptop | QEMU `pc` | planned |
| `x86_64-laptop-q35` | x86_64 | laptop | QEMU `q35` | planned |
| `aarch64-virt-development` | AArch64 | development board/tablet class | QEMU `virt` | planned |
| `riscv64-virt-development` | RV64 | workstation/development board | QEMU `virt` | planned |
| `riscv64-virt-minimal` | RV64 | minimal embedded/server | QEMU `virt` | planned |

### 6.2 Conditional or external profiles

| Profile ID | Backend | Purpose | Status |
| --- | --- | --- | --- |
| `aarch64-vmapple-macos` | QEMU `vmapple` | Apple-Silicon/macOS guest workflow | conditional-external |
| `android-aarch64-phone-avd` | Android Emulator | Generic Android phone AVD | external-adapter |
| `android-x86_64-phone-avd` | Android Emulator | Generic x86_64 Android phone AVD | external-adapter |
| `android-aarch64-tablet-avd` | Android Emulator | Generic Android tablet AVD | external-adapter |
| `android-x86_64-tablet-avd` | Android Emulator | Generic x86_64 Android tablet AVD | external-adapter |

These profiles must not be counted as direct OVD/QEMU coverage until their
external dependencies are installed, detected, and tested.

### 6.3 Physical-device approximation profiles

Physical devices should be represented as test intentions, not fake QEMU
machine models:

| Profile ID | Physical family | Emulation mapping |
| --- | --- | --- |
| `physical-rpi5-aarch64` | Raspberry Pi 5 | QEMU `virt` approximation plus hardware test record |
| `physical-visionfive2-riscv64` | VisionFive 2 | QEMU `riscv64 virt` approximation plus hardware test record |
| `physical-framework-x86_64` | Framework Laptop | QEMU `q35` approximation plus UEFI/ACPI hardware record |
| `physical-pinephonepro-aarch64` | PinePhone Pro | Android/Linux mobile external profile; no direct QEMU board claim |
| `physical-pinetab2-aarch64` | PineTab2 | Android/Linux tablet external profile; no direct QEMU board claim |

Each physical approximation must link to a hardware validation record and
state which hardware properties are intentionally not modeled.

## 7. Command generation and adapters

### 7.1 Native QEMU adapter

The native adapter should generate argument arrays for:

- x86_64 `pc` and `q35`;
- AArch64 `virt`;
- RISC-V 64 `virt`;
- serial console and chardevs;
- VirtIO block/network/GPU;
- USB xHCI/EHCI and HID/storage devices;
- display backend and preferred resolution;
- initrd, disk images, read-only, ephemeral, QMP, and networking.

The adapter must validate machine/architecture compatibility before launch and
write the exact generated command to `state/command.argv`.

### 7.2 Apple VMApple adapter

The adapter must:

1. check host architecture and macOS version;
2. check QEMU support for `vmapple`;
3. locate or request the preinstalled Virtualization Framework VM assets;
4. validate UUID, AVPBooter, pflash, and disk paths;
5. refuse to launch when the required external VM is absent;
6. mark the profile as `conditional-external` in JSON status;
7. never silently substitute generic AArch64 `virt`.

### 7.3 Android Emulator adapter

The adapter should use the `emulator` executable and AVD name/configuration,
not reconstruct Goldfish/Ranchu devices inside the generic OVD launcher.

Required checks:

- Android SDK/emulator executable exists;
- requested AVD exists;
- AVD architecture is compatible with the selected profile;
- system image/API level is installed;
- GPU mode and host acceleration are available or intentionally disabled;
- data directory is isolated for tests;
- ports, snapshots, and logs are deterministic;
- `adb` connectivity is available when requested.

The adapter should expose `--android-avd NAME`, `--gpu auto|host|software`,
`--wipe-data`, `--snapshot`, and `--no-window` as separate options, rather
than mixing them with native QEMU options.

## 8. Validation and quality gates

Every profile must pass these gates before becoming `supported`.

### 8.1 Static schema validation

- required fields present;
- profile ID and semantic version valid;
- architecture and backend compatible;
- form factor in the allowed enum;
- machine target valid for executable;
- memory and storage ranges bounded;
- display modes positive and within backend limits;
- all paths relative, approved, or explicitly external;
- no shell metacharacters in structured fields;
- references and ownership metadata present.

### 8.2 Dry-run validation

The profile must generate a deterministic command without launching:

```bash
emulator/profile_catalog.py validate --profile x86_64-desktop-q35
emulator/profile_catalog.py render --profile x86_64-desktop-q35
emulator/ovd_run.sh profile --profile x86_64-desktop-q35 --dry-run
```

The output must include:

- resolved architecture and machine;
- effective RAM/storage/display/network;
- required tools and missing tools;
- external dependencies;
- generated argument array;
- profile version and content hash.

### 8.3 Runtime smoke validation

Native QEMU profiles must verify:

- boot marker and architecture;
- serial output;
- memory and interrupt initialization;
- display backend or documented serial fallback;
- storage discovery;
- network discovery when enabled;
- QMP lifecycle and cleanup;
- no unexpected host-network or privileged access.

Android profiles must additionally verify AVD launch, boot completion, `adb`,
display/input event injection, and clean data-directory cleanup.

VMApple profiles must be tested only on eligible Apple-Silicon/macOS hosts and
must not be required in normal CI.

## 9. Catalog lifecycle and maintenance

### 9.1 Ownership

Each profile has one maintainer and one reviewer. The profile metadata should
record:

```yaml
maintainers: [team-or-person]
reviewers: [team-or-person]
last_verified: 2026-08-07
verification_qemu_version: 11.0.0
verification_host: macos-arm64
```

### 9.2 Version policy

- Patch release: documentation, metadata, or non-behavioral command ordering.
- Minor release: optional device, new supported display mode, or compatible
  default change.
- Major release: machine model, firmware contract, storage topology,
  architecture, boot flow, or command semantics change.

The registry lock file must be regenerated whenever a profile changes.

### 9.3 Deprecation

Profiles should be marked `deprecated` before removal. A deprecated profile
must remain renderable for at least one release cycle, with a replacement
profile and migration note. Retired profiles remain in historical metadata so
old test results can be interpreted.

### 9.4 QEMU release verification

Run a compatibility scan for every supported QEMU release:

- executable exists;
- machine exists;
- required devices exist;
- device properties have expected names/types;
- display and network options remain valid;
- boot smoke tests pass;
- profile output and logs are stable.

Do not assume that a QEMU machine available in one build is available in all
host packages. Profile status should include `qemu_min_version`,
`qemu_max_verified`, and host restrictions.

## 10. CSV and reporting workflow

The user-provided Python list should become a generated report, not a manually
edited runtime catalog.

Recommended workflow:

```text
profile YAML/JSON
        ↓ schema validator
normalized registry JSON
        ↓ generator
CSV + Markdown matrix + GUI index + lock file
        ↓ tests
dry-run and runtime profile verification
```

The generator should use Python’s standard library for CI portability. Pandas
may be an optional analysis dependency for notebooks or ad-hoc reports.

Required CSV columns:

```text
profile_id
profile_version
form_factor
architecture
backend
qemu_machine
qemu_executable
cpu_model
primary_guest_families
display_backend
preferred_resolution
storage_profiles
network_profiles
input_profiles
status
external_dependencies
last_verified
```

CSV generation must be deterministic: stable field order, stable row order,
UTF-8 output, LF line endings, and no timestamps in content hashes.

## 10A. Kernel and disk-artifact resolution

Every launchable native OVD profile must resolve two artifacts before QEMU is
started:

1. the latest compatible Omega kernel for the selected architecture and
   profile configuration; and
2. a matching Omega disk image containing the expected filesystem layout.

The profile must never silently use an unrelated kernel or stale disk image.

### 12A.1 Meaning of “latest Omega kernel”

For OVD, “latest” means the newest kernel artifact built from the current
Omega source tree and the exact profile inputs—not merely the newest file by
modification time.

The artifact identity should include:

```text
architecture
profile_id
profile_version
source revision or source-tree digest
compiler/toolchain identity
CMake cache/options
kernel link configuration
artifact format
```

The default native-kernel locations remain:

```text
build/x86_64/omega.elf
build/aarch64/omega.elf
build/riscv64/omega.elf
```

Each build directory must additionally contain a manifest, for example:

```text
build/aarch64/omega.elf
build/aarch64/omega.artifact.json
```

The manifest should record the ELF SHA-256, architecture, build revision,
toolchain, CMake options, source-tree fingerprint, build time, and profile
compatibility. Build time is informational; it must not be used as the only
freshness criterion.

### 12A.2 Build-if-missing and rebuild-if-stale policy

The profile runner should support these policies:

| Policy | Behavior |
| --- | --- |
| `require` | Fail if the exact kernel/image manifest is absent or mismatched |
| `build-if-missing` | Build missing artifacts; fail on stale artifacts unless rebuild is allowed |
| `build-if-stale` | Rebuild when source, profile, toolchain, or options changed |
| `always-build` | Force a clean compatible build before launch |
| `reuse-verified` | Reuse only an artifact whose manifest and digest match the profile lock |

The default development policy should be `build-if-stale`. CI should use
`build-if-stale` or `always-build` for release candidates. Reproducible
regression jobs may use `reuse-verified` with a committed or downloaded lock
manifest.

The resolution sequence is:

```text
load profile
    ↓
resolve architecture/toolchain/options
    ↓
check omega.elf and artifact manifest
    ↓ missing/stale
configure and build latest Omega kernel
    ↓
validate ELF and manifest
    ↓
resolve matching disk-image manifest
    ↓ missing/stale
build disk image using the current kernel and filesystem policy
    ↓
validate partition/filesystem/kernel payload
    ↓
launch QEMU or report a precise failure
```

No launcher should silently fall back to an older architecture’s kernel, a
different profile’s image, or a zero-filled image when the profile requires a
bootable Omega system disk.

### 12A.3 Profile artifact fields

Add an explicit artifact section to native profiles:

```yaml
artifacts:
  policy: build-if-stale
  kernel:
    kind: omega-kernel
    path: build/${architecture}/omega.elf
    required: true
    build_target: omega.elf
  disk:
    kind: omega-system-image
    path: disk_images/${profile_id}.img
    required: true
    format: raw
    filesystem: ext4
    build_target: omega-system-image
  manifest:
    required: true
    path: disk_images/${profile_id}.artifact.json
```

Path templates must be resolved through the validated project/build/image
roots. They must not allow `..`, absolute paths, shell substitutions, or
writes outside approved roots without explicit user approval.

### 12A.4 Exact kernel/image matching

The disk-image manifest must reference the exact kernel artifact used to build
it:

```json
{
  "profile_id": "aarch64-virt-development",
  "profile_version": "1.0.0",
  "architecture": "aarch64",
  "kernel_sha256": "...",
  "source_revision": "...",
  "filesystem": "ext4",
  "boot_filesystem": "fat32",
  "image_format": "raw",
  "image_sha256": "...",
  "created_by": "omega-image-builder",
  "schema_version": 1
}
```

Before launch, OVD verifies:

- kernel manifest architecture matches the profile;
- disk manifest architecture/profile matches the profile;
- disk manifest `kernel_sha256` matches the selected kernel;
- filesystem policy matches the profile;
- image digest matches the manifest when verification is enabled;
- image is not currently used by another non-ephemeral OVD instance.

## 10B. Default Omega filesystem policy: ext4

The default filesystem for a booted Omega system disk must be **ext4**.

This default applies to the Omega system/root filesystem, not necessarily to
every boot-support partition or removable-media format.

### 12B.1 Required image layout

The preferred UEFI-capable native system image layout is:

```text
GPT disk
├── EFI System Partition (FAT32, small boot partition)
│   └── EFI/BOOT/<architecture-loader-or-kernel>
└── Omega system partition (ext4)
    ├── boot/omega.elf
    ├── etc/
    ├── system/
    ├── var/
    └── home/      (optional development profile)
```

For direct QEMU `-kernel` profiles, the ext4 image remains the canonical
system/data image even when firmware is bypassed. The direct-kernel launch
must record that the image is attached as a block device but that bootloader
execution was not tested.

For legacy BIOS or firmware experiments that require a single FAT32 image,
the profile must explicitly opt into `boot-only-fat32` compatibility mode. It
must not change the global Omega default away from ext4.

### 12B.2 Filesystem fields

Replace ambiguous storage-only fields with explicit filesystem metadata:

```yaml
storage:
  transport: virtio
  container_format: raw
filesystem:
  system: ext4
  boot: fat32
  root_mount: /
  read_only: false
  journal: true
  label: OMEGA_ROOT
```

`container_format` describes the outer image format (`raw`, `qcow2`, `vmdk`,
or `vdi`). `filesystem.system` describes the filesystem inside the image.
These fields must never be conflated.

### 12B.3 Ext4 implementation dependency

The current repository’s boot-image generator creates a FAT32-compatible image
and the filesystem roadmap lists ext4 mounting and writeback as incomplete.
Therefore this OVD plan requires an explicit prerequisite milestone:

1. add host-side ext4 image creation and inspection;
2. add an ext4 system-image layout with a small FAT32 ESP when UEFI is needed;
3. add Omega ext4 read-only mounting;
4. add ext4 write support only after journal-aware write tests exist;
5. update image tests to verify the ext4 superblock, root directory, label,
   UUID, and kernel payload;
6. keep the existing FAT32 generator as a documented compatibility path until
   ext4 images are the default supported system images.

The initial ext4 image builder may use host tools such as `mke2fs`/`mkfs.ext4`
and a deterministic staging directory. It must not require privileged mounts
in CI. A later native image builder may replace host tooling when Omega’s
filesystem tools are mature.

### 12B.4 Image naming

Use names that make the architecture, profile, filesystem, and format visible:

```text
disk_images/omega-x86_64-desktop-q35-ext4.raw
disk_images/omega-aarch64-virt-development-ext4.raw
disk_images/omega-riscv64-virt-development-ext4.qcow2
```

The runtime OVD instance may copy or provision a private image under its own
directory, but its manifest must retain the source profile ID and artifact
digests.

### 12B.5 Provisioning commands

Add catalog-aware commands such as:

```bash
emulator/profile_catalog.py artifacts \
    --profile aarch64-virt-development \
    --policy build-if-stale

emulator/ovd_manager.sh create-from-profile \
    --profile aarch64-virt-development \
    --name omega-aarch64-ext4

emulator/ovd_manager.sh validate-artifacts \
    --name omega-aarch64-ext4
```

The command output must identify whether each artifact was reused, rebuilt,
or rejected, and must print the exact manifest and digest paths.

## 11. CLI and GUI experience

Add catalog-aware commands without breaking current instance commands:

```bash
ovd_manager.sh profiles list
ovd_manager.sh profiles show --profile x86_64-desktop-q35
ovd_manager.sh profiles validate
ovd_manager.sh create-from-profile \
    --profile x86_64-desktop-q35 \
    --name omega-q35-smoke
ovd_manager.sh start --name omega-q35-smoke
```

The GUI should display:

- profile ID and human-readable name;
- form factor and architecture;
- backend and machine;
- supported/experimental/conditional status;
- required tools and missing dependencies;
- preferred display mode;
- storage/network/input capabilities;
- last verified QEMU version;
- profile documentation and source links.

Conditional external profiles should have a visible “external dependency”
badge and a disabled launch button when their adapter is unavailable.

## 12. Test matrix

### 12.1 Unit tests

- schema parsing and required-field validation;
- semantic-version comparison;
- architecture/backend compatibility;
- machine/device/property validation;
- path and command-argument safety;
- deterministic CSV/JSON/Markdown generation;
- content hash and lock-file generation;
- profile inheritance/overrides if inheritance is introduced;
- external dependency detection;
- status and deprecation transitions.

### 12.2 Native QEMU integration tests

| Profile family | Architectures | Minimum tests |
| --- | --- | --- |
| `q35` desktop | x86_64 | UEFI/serial/display/storage/network/QMP |
| `pc` legacy | x86_64 | legacy machine, VGA/text, storage, network |
| `virt` development | AArch64 | FDT, serial, SimpleFb/VirtIO-GPU, VirtIO storage/network |
| `virt` development | RISC-V 64 | OpenSBI, FDT, PLIC, serial, VirtIO storage/network |
| USB profiles | all applicable | xHCI, HID, storage, hotplug, cleanup |
| display profiles | all applicable | preferred modes, fallback, framebuffer metadata |

### 12.3 External adapter tests

Run only when dependencies are installed:

- Android AVD creation/listing/launch/boot/ADB/cleanup;
- Android phone/tablet orientation, touch, keyboard, network, and snapshot;
- VMApple prerequisite checks and dry-run rendering;
- VMApple launch smoke on a dedicated Apple-Silicon/macOS runner.

Missing external tools should produce `SKIP`, not a false `PASS` or a failure
of unrelated native-QEMU CI.

## 13. CI and release policy

### Required CI lane

Every pull request must run:

- schema validation;
- catalog generation and clean-diff check;
- profile rendering for every native profile;
- all-architecture dry-run checks;
- native QEMU smoke for x86_64, AArch64, and RISC-V;
- OVD unit and integration tests;
- documentation links and generated-artifact consistency.

### Optional CI lanes

- Android Emulator on a host with SDK/system images;
- Apple-Silicon VMApple runner;
- GPU/display-enabled host tests;
- KVM/HVF/WHPX acceleration;
- weekly full profile matrix;
- release-candidate compatibility scan against multiple QEMU versions.

The default PR lane must remain usable on the project’s current macOS
development environment and must not require Android SDKs, privileged network
interfaces, or proprietary VM assets.

## 14. Security and safety controls

- Never allow a catalog profile to execute arbitrary shell text by default.
- Validate every external asset path and keep it outside the repository unless
  explicitly approved.
- Use isolated OVD roots and Android AVD/data directories in CI.
- Default to user-mode QEMU networking; TAP/bridge/passthrough requires an
  explicit opt-in.
- Do not expose host disks, USB devices, cameras, or microphones implicitly.
- Mark host acceleration and passthrough as capability-sensitive options.
- Sanitize profile names, generated filenames, logs, and JSON output.
- Treat imported profile catalogs as untrusted input until schema-validated.
- Keep firmware, Android images, and macOS VM assets outside Git unless their
  licenses explicitly permit redistribution.

## 15. Implementation phases

| Phase | Scope | Exit criteria |
| --- | --- | --- |
| R0 Registry foundation | Schema, catalog directory, IDs, status, validator | Catalog parses and rejects invalid profiles |
| R1 Native QEMU profiles | `q35`, `pc`, AArch64 `virt`, RISC-V `virt` | Dry-run and smoke tests pass on all ISAs |
| R2 Generated reports | CSV, JSON, Markdown, lock file | Deterministic generation and CI consistency |
| R3 OVD integration | Profile listing, show, validate, create-from-profile, GUI | Users can launch profiles without editing config manually |
| R4 Android adapter | AVD discovery, launch, GPU, ADB, snapshots, cleanup | Missing SDK is a clean skip; installed AVDs pass smoke tests |
| R5 VMApple adapter | Prerequisites, assets, dry-run, conditional launch | Dedicated Apple-Silicon/macOS validation works |
| R6 Physical mapping | Hardware records linked to approximating virtual profiles | Differences and unsupported hardware are explicit |
| R7 Maintenance automation | QEMU compatibility scan, deprecation, ownership, release reports | Catalog remains trustworthy across QEMU updates |

## 16. Acceptance criteria

The profile system is ready for normal use when:

- every catalog entry has a stable ID, schema version, owner, status, and
  verification record;
- native QEMU profiles render deterministic command arrays;
- architecture, machine, device, and option mismatches are rejected before
  launch;
- CSV and Markdown are generated from the canonical registry;
- OVD can create and validate instances from a profile;
- external Android and VMApple adapters are clearly separated from native
  QEMU profiles;
- missing optional dependencies produce actionable skips;
- profile changes are covered by unit and integration tests;
- QEMU version compatibility is recorded rather than assumed; and
- no profile claims to emulate a commercial device beyond its documented
  virtual hardware contract.

## 17. Related documentation

- [`docs/REAL_HARDWARE_VALIDATION_MATRIX.md`](REAL_HARDWARE_VALIDATION_MATRIX.md)
- [`docs/COMMUNICATIONS_INTEGRATION_PLAN.md`](COMMUNICATIONS_INTEGRATION_PLAN.md)
- [`docs/POINTING_DEVICES_INTEGRATION_PLAN.md`](POINTING_DEVICES_INTEGRATION_PLAN.md)
- [`docs/ROADMAP.md`](ROADMAP.md)
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md)
- [`emulator/README.md`](../emulator/README.md)
- [`scripts/README.md`](../scripts/README.md)
