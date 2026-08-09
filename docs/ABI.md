# Omega Kernel: Application Binary Interface (ABI) Specification

## Executive Summary
This specification defines the formal Application Binary Interface (ABI) of the **Omega** kernel and the Omega SDK boundary. It describes memory layout, linkage conventions, system call calling conventions, privilege boundaries, file descriptors, and Linux artifact requirements across **x86_64 (AMD64)**, **AArch64 (ARM64)**, and **RISC-V 64 (`rv64gc`)**.

---

## 1. System Call Architecture & Calling Conventions

The register and instruction table below is the planned userspace syscall
entry contract. The current kernel exposes a dispatcher for tests and bring-up;
native Ring 3/EL0/U-Mode trap entry is not enabled yet and userland entry fails
closed until architecture-specific trap frames and selectors are installed.

### Register Passing Conventions Across Architectures

| Register Role | x86_64 (AMD64) Convention | AArch64 (ARM64) Convention | RISC-V 64 (`rv64gc`) Convention |
| :--- | :--- | :--- | :--- |
| **Trap Instruction** | `syscall` | `svc #0` | `ecall` |
| **Syscall Number** | `RAX` | `X8` | `A7` |
| **Argument 1** | `RDI` | `X0` | `A0` |
| **Argument 2** | `RSI` | `X1` | `A1` |
| **Argument 3** | `RDX` | `X2` | `A2` |
| **Argument 4** | `RCX` | `X3` | `A3` |
| **Argument 5** | `R8` | `X4` | `A4` |
| **Argument 6** | `R9` | `X5` | `A5` |
| **Return Value** | `RAX` | `X0` | `A0` |
| **Error Code** | `RAX` (Negative errno) | `X0` (Negative errno) | `A0` (Negative errno) |

---

## 2. System Call Registry & Numerical Identifiers

Omega uses Linux-native syscall numbers per target ABI. The x86_64 table is
the primary v1 ABI; AArch64 and RV64 use their corresponding Linux numbers.
System calls return non-negative results on success and `-errno` on failure.

| Symbolic Name | x86_64 | AArch64/RV64 | Prototype |
| :--- | :---: | :---: | :--- |
| `SYS_READ` | `0` | `63` | `ssize_t read(int fd, void *buf, size_t count)` |
| `SYS_WRITE` | `1` | `64` | `ssize_t write(int fd, const void *buf, size_t count)` |
| `SYS_CLOSE` | `3` | `57` | `int close(int fd)` |
| `SYS_MMAP` | `9` | `222` | `void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)` |
| `SYS_MUNMAP` | `11` | `215` | `int munmap(void *addr, size_t len)` |
| `SYS_BRK` | `12` | `214` | `int brk(void *addr)` |
| `SYS_SCHED_YIELD` | `24` | `124` | `int sched_yield(void)` |
| `SYS_FORK` | `57` | clone-based | `pid_t fork(void)` |
| `SYS_EXECVE` | `59` | `221` | `int execve(const char *path, char *const argv[], char *const envp[])` |
| `SYS_EXIT` | `60` | `93` | `void exit(int status)` |
| `SYS_WAIT4` | `61` | `260` | `pid_t wait4(pid_t, int *, int, void *)` |
| `SYS_OPENAT` | `257` | `56` | `int openat(int dirfd, const char *path, int flags, mode_t mode)` |

Omega input extensions use the reserved, architecture-independent range
`0x4000`-`0x40ff` so the event ABI has the same numbers on x86_64, AArch64,
and RISC-V 64:

| Symbolic Name | Number | Prototype |
| :--- | :---: | :--- |
| `SYS_INPUT_READ` | `0x4000` | `ssize_t input_read(struct omega_input_event *events, size_t capacity)` |
| `SYS_INPUT_POLL` | `0x4001` | `ssize_t input_poll(void)` |
| `SYS_INPUT_SUBSCRIBE` | `0x4002` | `int input_subscribe(uint64_t type_mask)` |

`omega_input_event` is a packed, little-endian, 64-byte record. Version 1
contains version and size fields, device and sequence identifiers, a
timestamp, an event type/code pair, flags, three signed 32-bit values, and
reserved space. Event types are `KEY`, `REL`, `BUTTON`, `DEVICE`, and `SYN`.
Keyboard events expose raw transitions and modifier state; layout and Unicode
translation belong in userspace. Reads are bounded to the kernel queue
capacity and return the number of records consumed.

Credential calls use the native Linux per-ISA numbers for `getuid`, `geteuid`,
`getgid`, `getegid`, `setuid`, `setgid`, `setresuid`, `setresgid`,
`getgroups`, `setgroups`, and `umask`. File ownership uses `chmod`,
`chown`/`fchownat`, and `fchmodat` are represented in the dispatcher. Mature
Linux-style negative-errno behavior is the contract; unsupported or
architecture-dependent paths still fail explicitly until their full
implementation milestones are complete.

`fork`, `execve`, and `wait4` currently return `-ENOSYS` until copy-on-write,
isolated ELF replacement, and process reaping are implemented. The former
Omega numbers remain source-level compatibility aliases only.

## 3. Linux Users, Groups, Roles, and File Permissions

All architectures use the Linux credential model:

- `uid`, `euid`, `suid`, and `fsuid` are 32-bit user IDs.
- `gid`, `egid`, `sgid`, and `fsgid` are 32-bit group IDs.
- Supplementary groups are represented as a bounded group list.
- UID 0 has root DAC override semantics; execute permission still requires an
  execute bit, matching Linux's practical root behavior.
- File modes use the standard owner/group/other bits (`0400`, `0200`, `0100`,
  through `0001`) and are evaluated using the filesystem UID/GID.
- New-file umask state is stored per credential set and defaults to `0022`.

VFS traversal requires execute permission on each directory. File reads and
writes require the corresponding mode bit; `chmod` is restricted to the file
owner or root, while `chown` and supplementary-group changes require root.
The credential and mode-checking implementation is shared across x86_64,
AArch64, and RV64 builds. Process-bound credential activation is currently
available when the x86_64 process manager is active; early boot and standalone
tests use a controlled fallback credential set. A full capability model and
architecture-native userspace fault handling remain hardening work.


---

## 4. Standard File Descriptor Index Allocation

The x86_64 process foundation owns a 16-entry descriptor table per process.
Descriptors 0–2 are reserved for future standard streams, but are not opened
automatically yet. `open`/`openat` currently allocate descriptors from 3
through 15:

- **`0`**: `STDIN_FILENO` (Standard Input stream)
- **`1`**: `STDOUT_FILENO` (Standard Output serial console)
- **`2`**: `STDERR_FILENO` (Standard Error serial console)

---

## 5. Virtual Address Space Layout & Privilege Boundaries

Omega reserves separate kernel and user virtual-address regions. The x86_64
process foundation currently creates isolated roots and dedicated user PML4
slots; native Ring 3/EL0/U-Mode execution and a complete hardware-enforced
userspace boundary remain future work:

```text
+-------------------------------------------------------+ 0xFFFFFFFFFFFFFFFF
|                                                       |
|             Kernel Virtual Address Space              |
|        (Ring 0 / EL1 / S-Mode - Read/Write)           |
|                                                       |
+-------------------------------------------------------+ 0xFFFF800000000000
|                     Canonical Hole                    |
+-------------------------------------------------------+ 0x00007FFFFFFFFFFF
|                                                       |
|            Userland Virtual Address Space             |
|         (Ring 3 / EL0 / U-Mode - User Access)         |
|                                                       |
|  - User Stack (Top of Userland Space)                 |
|  - Dynamic Heap / Mmap Region                         |
|    Anonymous mmap bring-up base: 0x400000000000       |
|  - ELF BSS / Data Segments                            |
|  - ELF Code Segment (.text - Base 0x400000)           |
|                                                       |
+-------------------------------------------------------+ 0x0000000000000000
```

---

## 6. Primitive Data Types & Alignment Specifications

| Type | Bit Width | Byte Alignment | Architectural Representation |
| :--- | :---: | :---: | :--- |
| `char` | 8-bit | 1-byte | Signed 8-bit Integer |
| `short` | 16-bit | 2-byte | Signed 16-bit Integer |
| `int` | 32-bit | 4-byte | Signed 32-bit Integer |
| `long` | 64-bit | 8-byte | Signed 64-bit Integer |
| `long long` | 64-bit | 8-byte | Signed 64-bit Integer |
| `pointer` (`void*`) | 64-bit | 8-byte | Unsigned 64-bit Virtual Address |
| `size_t` | 64-bit | 8-byte | Unsigned 64-bit Byte Count |

## 7. Omega SDK Linux Artifact ABI

Omega consumes standard little-endian ELF64 artifacts for the matching
architecture. Compatibility is defined at three different layers:

| Artifact | Compatibility contract | Current status |
| :--- | :--- | :--- |
| Static library (`.a`) | Archive members must be ELF relocatable objects for the target ISA, use the target Linux psABI, and expose C/C++ symbols that the Omega SDK linker can resolve. A host Linux `.a` is not executable by itself. | **Link-time compatible when rebuilt/linked with the Omega SDK** |
| Executable (`ET_EXEC`/static `ET_DYN`) | ELF64, little-endian, matching `e_machine`, valid `PT_LOAD` ranges, no `PT_INTERP`, no unresolved dynamic metadata, and the Linux syscall/psABI contract above. | **Bounded format validation implemented; segment mapping and full userspace execution pending** |
| Shared object (`ET_DYN`/`.so`) | Position-independent code, ELF relocations, symbol/version resolution, `DT_NEEDED` dependency loading, and an Omega dynamic linker/loader are required. | **Not runnable yet; ABI reserved and explicitly rejected by the current loader** |

### Static libraries

A Linux-built static archive can be used as an Omega link input only when its
objects match the selected Omega target (`x86_64`, `AArch64`, or `RV64`), use
the corresponding System V AMD64, AAPCS64, or RISC-V psABI, and do not depend
on unavailable host services. The archive must be linked against an Omega
CRT/libc and Omega syscall stubs; glibc startup objects and glibc symbol
versions are not ABI-compatible with Omega.

### Executables

The first supported binary interchange target is a statically linked Linux
ELF64 executable rebuilt for the Omega SDK syscall ABI. The current loader validates
headers and program-header safety only when an image size is supplied. It does not yet map segments into an
active userspace process, apply relocations, establish a Linux-compatible
auxiliary vector, or enter a real userspace trap frame. Therefore a validated
executable is not yet automatically runnable.

### Shared objects and dynamic linking

`.so` compatibility requires a dynamic linker and a stable userspace runtime.
Omega currently rejects `PT_INTERP` and unresolved `PT_DYNAMIC` images rather
than silently loading them with incorrect semantics. The planned dynamic ABI
includes ELF symbol lookup, REL/RELA relocation processing, TLS, `DT_NEEDED`,
symbol versioning, `ld.so`-style initialization/finalization, and `dlopen`/
`dlsym` behavior.

### Cross-architecture build contract

Linux-produced artifacts must be built separately per target ISA; a Linux
x86_64 object cannot be reused as an AArch64 or RV64 object. The compatibility
matrix is:

| Omega target | ELF `e_machine` | psABI | Syscall register |
| :--- | :---: | :--- | :--- |
| x86_64 | `EM_X86_64` (`62`) | System V AMD64 | `RAX`, args `RDI`–`R9` |
| AArch64 | `EM_AARCH64` (`183`) | AAPCS64 | `X8`, args `X0`–`X5` |
| RISC-V 64 | `EM_RISCV` (`243`) | RISC-V ELF psABI | `A7`, args `A0`–`A5` |

The compatibility boundary is intentionally narrower than “any Linux
binary”: glibc/musl versioned symbols, Linux-specific `ioctl` values, vDSO,
procfs/sysfs, namespaces, cgroups, signals, and kernel modules require
dedicated Omega implementations.
