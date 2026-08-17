# Omega Installed Root Filesystem Layout Plan

Status: design baseline (2026-08-17)

This document defines how a bootable Omega distribution should be laid out on
a target machine. It covers the disk image, boot artifacts, immutable early
userspace, persistent root filesystem, runtime mounts, package ownership, and
the migration path from the current initrd-only bring-up.

The design targets x86_64, AArch64, and RISC-V 64. The filesystem namespace is
the same on every architecture; only the boot artifact and platform firmware
integration differ. A target installation contains one architecture's kernel,
initrd, and userspace binaries. Multi-ISA build outputs remain host-side
release artifacts and are not mixed into one installed root.

## 1. Design goals

The installed system must:

- boot without depending on the persistent root being writable;
- keep the kernel, initrd, and boot configuration recoverable;
- provide a conventional POSIX hierarchy for Bash and the command suite;
- support a read-only or immutable base system with writable state separated;
- allow `/home`, `/var`, and caches to be replaced or repaired independently;
- make architecture and release identity explicit in boot metadata;
- support both a small development image and a production disk layout;
- fail safely when optional storage, networking, or user configuration is
  unavailable.

The first production profile is static and initrd-friendly. Dynamic linking,
package transactions, encrypted volumes, and writable ext4 mutation are later
layers; the directory layout reserves their paths now without pretending that
those services already exist.

## 2. Storage profiles

### 2.1 Development and recovery image

Use this profile while storage mounting and filesystem mutation are still
being completed:

```text
raw disk image
└── optional ESP or firmware boot region
    └── kernel + initrd loaded by the firmware/QEMU launcher
```

The initrd is the authoritative root for early boot. It is read-only and
contains `/init`, the static command bootstrap, and optional Bash. `/tmp` is a
tmpfs when available. This profile is suitable for QEMU, recovery, installer
images, and hardware bring-up, but it is not a persistent workstation root.

### 2.2 Installed target disk

The initial installed layout should use GPT with these partitions:

| Partition | Suggested size | Filesystem | Mount point | Purpose |
|---|---:|---|---|---|
| ESP | 256 MiB–1 GiB | FAT32 | `/boot/efi` | UEFI loader, architecture-specific boot entries, recovery metadata |
| Omega boot | 64–256 MiB | FAT32 or ext4 | `/boot` | Kernel, initrd, boot configuration, rollback slots |
| Root A | 2–8 GiB minimum | ext4 initially | `/` | Immutable or mostly immutable operating-system release |
| State | 1–4 GiB minimum | ext4 | `/var` | Logs, package database, runtime state, caches |
| Home | Optional; remaining space | ext4 | `/home` | User data and user configuration |
| Recovery | 256 MiB–2 GiB | FAT32 or ext4 | not normally mounted | Known-good installer and rescue image |

For a small device, combine Root A and State into one ext4 partition while
retaining separate `/boot` and `/home` semantics. For an update-capable
device, reserve equal-sized Root A and Root B partitions and make one active
at a time. The inactive slot is never mounted read-write during normal boot.

Partition GUIDs, labels, and UUIDs must be recorded in the installer manifest;
device names such as `/dev/sda2` are not stable installation identifiers.

### 2.3 Future encrypted layout

Encryption should wrap State and Home first. The boot and recovery partitions
must remain available to firmware, while Root may later be a verified,
read-only image. The eventual topology is:

```text
ESP → bootloader → verified kernel/initrd
                    ├── Root A or Root B
                    └── encrypted data volume → /var and /home
```

Key handling, rollback protection, and hardware-backed secrets are separate
security projects and are not prerequisites for the initial filesystem tree.

## 3. Installed directory tree

The baseline target root is:

```text
/
├── bin/                 Essential user commands and shell entry points
├── boot/                Mounted boot partition, if not firmware-only
├── dev/                 Device nodes or the future device service mount
├── etc/                 Static system configuration
│   ├── omega/           Omega release, platform, and service configuration
│   ├── init.d/          Early service scripts, if script-based init is used
│   ├── profile          System-wide shell environment
│   ├── hostname         Machine name
│   ├── hosts            Local name mappings
│   ├── fstab            Filesystem and mount policy
│   └── passwd, group    Local identity databases when enabled
├── home/                User home directories; separate partition preferred
├── init                 Early userspace entry point or symlink to /sbin/init
├── lib/                 Compatibility path; static profile may be empty
├── media/               Removable-media mount points
├── mnt/                 Administrator and installer temporary mounts
├── opt/                 Optional vendor or development payloads
├── proc/                procfs mount point
├── root/                Root user's home directory
├── run/                 Volatile boot/runtime state; tmpfs
├── sbin/                Essential administrative commands and init
├── srv/                 Service data, initially unused
├── sys/                 sysfs mount point
├── tmp/                 Volatile temporary files; tmpfs
├── usr/
│   ├── bin/             Non-early user commands
│   ├── include/         Development headers, only in SDK/developer images
│   ├── lib/             Shared libraries and package libraries in future
│   ├── libexec/         Private service helpers
│   ├── local/           Administrator-installed local software
│   └── share/           Manifests, locale data, documentation, zone data
└── var/
    ├── cache/           Rebuildable package and command caches
    ├── lib/             Persistent service/package databases
    ├── log/             System and service logs
    ├── run/             Compatibility path to /run, if needed
    ├── spool/           Queues and deferred work
    └── tmp/              Persistent-service temporary data
```

### 3.1 Early command placement

The first static profile should install these files in `/bin`:

```text
/bin/sh       selected POSIX shell policy (later milestone)
/bin/bash     Bash, when the Bash profile is enabled
/bin/ls       /bin/dir
/bin/ln       /bin/pwd
/bin/cat      /bin/mkdir
/bin/rm       /bin/rmdir
/bin/mv       /bin/echo
/bin/true     /bin/false
/bin/env      /bin/test
```

`cd` remains a shell builtin because an external process cannot change its
parent's current directory. `printf`, `[`, `cp`, `head`, `tail`, `sleep`,
`kill`, `basename`, and `dirname` are reserved for the next command tranche.

During early boot, `/sbin/init` may be the same static image as `/init`; the
installed layout must nevertheless provide the conventional names so scripts
do not depend on the initrd-specific path.

### 3.2 Static versus dynamic runtime paths

The current Omega command and SDK profiles are statically linked. Therefore:

- static commands belong directly in `/bin` or `/usr/bin`;
- `/lib` and `/usr/lib` need only contain configuration, future libraries, or
  compatibility symlinks in the first release;
- no `/lib/ld-musl-*.so.1` is installed until the dynamic loader is supported;
- the musl archive, headers, CRT, and TinyCC belong in a developer SDK image,
  not in a minimal production root;
- a developer image may add `/opt/omega/sdk/<isa>/` or expose the SDK from the
  host, but production binaries must not rely on it at runtime.

## 4. Boot and mount sequence

The intended boot sequence is:

1. Firmware or the platform bootloader identifies the ESP and loads the
   architecture-matching Omega kernel and initrd.
2. The kernel initializes memory, interrupts, the console, and storage
   discovery without requiring `/`.
3. The initrd is mounted as the early root and starts `/init`.
4. `/dev`, `/proc`, `/sys`, `/run`, and `/tmp` are mounted or populated as
   their supporting kernel/service implementations become available.
5. The storage manager discovers the target disk by partition GUID/UUID.
6. The installer or init system mounts the selected Root slot read-only first,
   validates its release manifest, then mounts or activates State and Home.
7. Required directories are checked, compatibility symlinks are created, and
   `/etc/omega/boot-state` records the active slot and mount result.
8. The system switches its visible root using a controlled root handoff. The
   initrd remains available as `/run/omega/initrd` or an equivalent recovery
   reference until the handoff is complete.
9. A service manager starts console, storage, network, and login services.
10. The login shell receives `PATH=/bin:/usr/bin:/sbin:/usr/sbin` and a valid
    per-process current directory, normally the user's home.

The root handoff must have an explicit rescue path. If Root or State fails to
mount, keep the initrd root active, expose diagnostics, and start a recovery
shell rather than attempting writes to an unknown device.

## 5. Configuration and ownership policy

The base image should be reproducible. Files under `/bin`, `/sbin`, `/usr`,
and the immutable portion of `/etc` are owned by the Omega release manifest.
Local configuration belongs in clearly identified files under `/etc/omega` or
in `/var/lib`; package updates must not silently overwrite administrator data.

Initial ownership and modes:

| Path class | Owner | Mode | Policy |
|---|---|---:|---|
| Kernel/initrd/boot metadata | `root:root` | `0644` | EFI and bootloader-specific exceptions apply |
| Executables in `/bin`, `/sbin`, `/usr/bin` | `root:root` | `0755` | Must be matching-ISA static ELF unless explicitly marked otherwise |
| System configuration | `root:root` | `0644` | Secrets are narrower, normally `0600` |
| `/etc/passwd`, `/etc/group` | `root:root` | `0644` | Replace with a credential service when available |
| `/root` | `root:root` | `0700` | Never shared with regular users |
| `/home/<user>` | `<user>:<group>` | `0700` or `0750` | Created by installer/user-management service |
| `/tmp`, `/run` | `root:root` | `01777` / `0755` | Volatile mounts; apply sticky-bit policy to `/tmp` |
| `/var/log` | `root:root` | `0755` | Services write through controlled files or a log service |

The installer must reject symlinks or paths that escape the target root while
extracting package payloads. It must also reject wrong-architecture ELFs,
dynamic interpreters unsupported by the target loader, and setuid/setgid bits
until credential transitions are implemented and audited.

## 6. Image and package manifests

Every installed release should have a machine-readable manifest at:

```text
/usr/share/omega/release.json
/etc/omega/installation.json
/etc/omega/boot-state
```

The release manifest records the Omega version, target ISA, kernel hash,
initrd hash, root slot, SDK ABI version, enabled profiles, and file hashes.
The installation manifest records partition UUIDs, filesystem types, install
time, hostname, and the selected console/storage profiles.

An early package manifest format should include:

```text
path, type, mode, uid, gid, size, sha256, package, mutable
```

Packages are initially archive payloads unpacked by the host installer. A
future on-target package manager may use the same manifest and transaction
rules, but must stage changes under `/var/lib/omega/transactions/<id>` and
atomically commit them. Never modify the active root in place without a
recoverable transaction or a bootable fallback slot.

## 7. Installer workflow

The installer should have two modes:

### 7.1 Image builder

Used for QEMU and release images:

1. Select one target ISA and a release profile (`minimal`, `bash`, or
   `developer`).
2. Build the matching kernel, initrd, static command set, and optional Bash.
3. Create the GPT image and filesystems.
4. Populate Root from a deterministic staging tree.
5. Populate `/boot` and the ESP with matching artifacts and manifests.
6. Create State, Home, and recovery directories with correct modes.
7. Run host-side ELF, path, ownership, and manifest validation.
8. Boot the image in QEMU and verify mount order, root handoff, `/bin/echo`,
   `pwd`, `ls`, and shell startup where enabled.

### 7.2 Target installer

Used on a running recovery environment:

1. Boot the installer initrd and enumerate storage devices.
2. Require explicit confirmation of the selected disk before repartitioning.
3. Create or validate GPT partitions and record their UUIDs.
4. Format only the partitions selected by the user; preserve Home when
   requested.
5. Install Root, boot artifacts, State defaults, and the recovery payload.
6. Generate `fstab`, boot entries, release manifests, and an initial user.
7. Flush and verify hashes, unmount cleanly, and reboot into the new slot.
8. On first boot, run filesystem checks and finalize machine identity without
   making the system unusable if network setup fails.

Repartitioning, formatting, and root-slot replacement are destructive actions.
The installer must display exact device paths, sizes, partition GUIDs, and
preservation choices before execution.

## 8. Implementation phases

### R0 — Initrd namespace contract

- Define the canonical tree and `/init` handoff contract.
- Extend the initrd packer with a manifest and directory entries.
- Package the current static commands under `/bin`.
- Add a host test that rejects duplicate paths, traversal, wrong ISA, and
  missing `/init`.

### R1 — Read-only persistent root

- Mount a discovered ext4 root read-only after initrd startup.
- Validate the release manifest and expose root mount diagnostics.
- Implement `/dev`, `/proc`, `/sys`, `/run`, and `/tmp` mount points or stubs.
- Add a controlled initrd-to-root handoff with rescue fallback.

### R2 — Writable state and user homes

- Add ext4 writable mount support and VFS mutation operations.
- Mount State at `/var` and create `/var/lib/omega`, `/var/log`, and caches.
- Mount Home at `/home` and implement initial identity/configuration setup.
- Add `fstab` parsing, UUID-based mounting, and filesystem check policy.

### R3 — Installer and boot slots

- Implement GPT/ESP population and architecture-specific boot entries.
- Add Root A/Root B selection, generation manifests, and rollback state.
- Build deterministic `minimal`, `bash`, `developer`, and `recovery` images.
- Add power-loss and interrupted-install recovery tests.

### R4 — Production hardening

- Add verified boot, signed manifests, encrypted State/Home, and secure key
  provisioning.
- Add package transactions, quotas, log rotation, and service supervision.
- Validate on x86_64, AArch64, and RISC-V reference hardware profiles.

## 9. Verification matrix

| Test | Expected result |
|---|---|
| Empty/missing persistent disk | Initrd boots recovery shell; no kernel panic |
| Wrong-ISA `/init` or command | Installer/packer rejects it before boot |
| Read-only Root | System boots; writes are limited to `/run`, `/tmp`, and mounted State |
| Missing State | Boot reports degraded state and preserves recovery path |
| Separate Home preserved | Reinstall replaces Root without deleting Home |
| Interrupted Root A update | Bootloader selects last-known-good slot |
| Path traversal package entry | Extraction aborts without escaping target root |
| `/bin/echo` and `pwd` | Execute from installed root with correct cwd and exit status |
| Bash profile | Bash starts with the documented PATH and startup files |
| All three ISAs | Same namespace and manifest policy; matching target binaries |

## 10. Decisions still required

Before implementation begins, the project should choose:

1. ext4 as the first persistent root filesystem, or a simpler read-only image
   format for Root A;
2. UEFI-first boot on x86_64/AArch64/RISC-V versus a platform-specific boot
   adapter for each board class;
3. whether `/bin/sh` initially aliases Bash or is a smaller POSIX shell;
4. whether `/usr` is merged into `/` in the first release;
5. whether Root A/Root B is required for the first installable image or can be
   deferred until package updates exist;
6. the identity service and whether local `/etc/passwd` is only a bootstrap;
7. the exact init/service manager that performs the root handoff.

The recommended first implementation is: GPT, ESP + `/boot` + one ext4 Root
partition, initrd recovery fallback, tmpfs `/run` and `/tmp`, static commands
in `/bin`, no dynamic loader, and a separate Home partition only when the
storage target is large enough. Root A/Root B and encryption can then be added
without changing the userspace namespace.

`scripts/create_bootable_disk.sh` implements this first image profile when
the host provides `sgdisk`, `mkfs.ext4`, `debugfs`, FAT formatting tools, and
`mtools`. It places the EFI and `/boot` payload on the FAT32 ESP and the
installed root, musl SDK, Omega TinyCC, and ported POSIX commands on ext4.
`--legacy-fat` is retained only for older compatibility tests and does not
represent the persistent installation layout.
