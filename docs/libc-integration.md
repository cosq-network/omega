# Standard C Library Integration Plan

## Implementation status (2026-08-17)

The host-built static musl port is implemented for x86_64, AArch64, and
RISC-V. Build the SDKs with:

```sh
bash scripts/build_musl_sysroot.sh x86_64
bash scripts/build_musl_sysroot.sh aarch64
bash scripts/build_musl_sysroot.sh riscv64
```

Each command produces `libc/omega-sdk/<isa>/` containing musl headers,
`lib/libc.a`, `lib/crt1.o`, `lib/libomega-shim.a`, `lib/omega.ld`, and an
`omega-sdk.json` manifest. The SDK is static and non-PIE; it does not provide
`libc.so` or a dynamic loader.

The kernel prerequisites needed for the static musl path are implemented,
including 256 file descriptors, page-backed `brk`, anonymous `mmap`, PT_TLS
loading/register setup, ELF image-aware mmap placement, multi-file initrds,
and `/tmp` tmpfs. An x86_64 static musl malloc smoke test runs under QEMU.

Dynamic linking, complete `execve`-driven process replacement, persistent
ext4 creation, and the complete LIBC-1..10 QEMU matrix remain follow-up work.
The phase tables below are retained as the roadmap for those features.

The first standalone musl-linked POSIX command slice is now available through
`scripts/build_commands.sh` and `scripts/test_commands_build.sh`. It produces
`ls`, `dir`, `ln`, `pwd`, `cat`, `mkdir`, `rm`, `rmdir`, `mv`, `echo`, `true`,
`false`, `env`, and `test` for all three ISAs. This is a build/link milestone,
not a claim of complete command runtime support; see
[`POSIX_COMMANDS_PORTING_PLAN.md`](POSIX_COMMANDS_PORTING_PLAN.md).

## 1. Purpose and Positioning

Omega currently builds as a **freestanding** kernel: the userspace runtime
(`omega_crt.c`, `user/`) is a minimal hand-rolled C library providing
`strlen`, `memcpy`, `memset`, a bump-allocator `malloc` (4 KiB, no `free`),
and a handful of direct syscall wrappers. Programs are compiled with
`-nostdlib -ffreestanding -nostdinc` and linked as static `ET_EXEC` ELF64
binaries with no dynamic linker support.

This document defines the plan to transition Omega from a **freestanding
research kernel** to a **hosted operating system** by integrating a standard
C library. The target is **musl** (MIT-licensed, static-first, POSIX-compliant,
actively maintained), which aligns with Omega's license, multi-architecture
scope, and long-term production goals.

The integration does **not** change Omega's Linux-compatible syscall ABI.
Standard libc functions (`printf`, `malloc`, `open`, `read`, etc.) will be
implemented as userspace wrappers over the existing Linux-numbered syscall
dispatch. What changes is the **runtime environment**: programs will link
against a real `libc.a`, use `__libc_start_main`, get proper `errno`,
`stdio` buffering, `malloc`/`free`, and eventually `pthread` and dynamic
loading.

## 2. Current Freestanding Baseline

### 2.1 Existing userspace runtime

| Component | Location | Status |
| :--- | :--- | :--- |
| CRT0 assembly | `user/omega_crt0_*.s` | Per-arch, minimal stack setup |
| C runtime | `user/omega_crt.c` | `strlen`, `memcpy`, `memset`, bump `malloc`, `write`, `exit` |
| Syscall stubs | `user/omega_syscall_*.s` | Per-arch `syscall`/`svc`/`ecall` wrappers |
| Startup | `omega_start()` | Unpacks `argc`/`argv`/`envp`, calls `main()` |
| Headers | `user/include/omega/` | `stdint.h`, `unistd.h`, `stdio.h`, `stdlib.h`, `string.h`, `errno.h`, `syscall.h` |
| Build | `scripts/build_user_*.sh` | Shell scripts, outside CMake |

### 2.2 Current limitations

| Limitation | Root cause | Impact |
| :--- | :--- | :--- |
| `malloc` has no `free` | Bump allocator over 4 KiB static buffer | Memory leaks; no long-running services |
| No `stdio` buffering | No `FILE*` streams, `setvbuf` | Every `write` is a syscall; no `fprintf` |
| `fd_table` = 16 entries | Fixed-size array in `Process` | No `opendir`, limited stdio, no `select`/`poll` |
| `SYS_BRK` is a no-op | Records break but does not map pages | Heap cannot grow; `sbrk()`/`brk()` broken |
| No `PT_DYNAMIC`/`PT_INTERP` | `ElfLoader::validate()` rejects them | Only static `ET_EXEC`; no shared libs, no dynamic linker |
| No TLS | No `%fs`/`%gs` or `TPIDR` setup | No thread-local `errno`, no `pthread` |
| No `__libc_start_main` | Custom `omega_start()` | Standard libc startup incompatible |
| No `cxa_atexit` | `-fno-use-cxa-atexit` | C++ static destructors broken |
| Build outside CMake | Shell scripts only | No dependency tracking, no multi-target integration |
| No `environ` global | `envp` passed via stack only | `getenv`/`setenv` impossible |

### 2.3 Kernel ABI compatibility

Omega's syscall ABI is **already Linux-compatible**:

| Layer | Current state |
| :--- | :--- |
| Syscall numbers | Linux per-ISA numbers (`syscall.hpp`) |
| Calling convention | Linux register ordering (`syscall`, `svc #0`, `ecall`) |
| ELF format | Standard ELF64, `PT_LOAD` segments only |
| Stack layout | `argc`, `argv`, `envp`, `auxv` with `AT_NULL` |
| Error convention | Negative kernel returns → `-errno` in userspace |

The libc integration preserves all of the above. No kernel syscall numbers,
register conventions, or stack layouts change.

## 3. Libc Selection: musl

### 3.1 Why musl

| Criterion | musl | Alternative (newlib, uClibc, custom) |
| :--- | :--- | :--- |
| License | MIT (matches Omega) | newlib: BSD-like; uClibc: LGPL; custom: N/A |
| Static linking | First-class, no GOT/PLT overhead | newlib: OK; uClibc: partial; custom: varies |
| POSIX compliance | Excellent, complete | newlib: incomplete; uClibc: partial |
| Code size | ~30K LOC core | newlib: larger; uClibc: smaller but abandoned |
| Multi-arch | x86_64, aarch64, riscv64 | All have coverage, but musl is most tested |
| Maintenance | Active (2024–2026) | uClibc: unmaintained; newlib: slow |
| Dynamic linking | Supported (future milestone) | Varies |
| Alpine Linux provenance | Same codebase as Omega's Docker base | Proven in containerized/embedded use |

### 3.2 musl integration model

musl will be built as a **static archive** (`libc.a`) for each target ISA.
No shared libraries (`libc.so`) are planned for the initial integration.
musl's internal `__syscall()` interface will be provided by Omega-specific
assembly stubs that translate musl's syscall convention to Omega's Linux-numbered
dispatcher.

```text
libc API (printf, malloc, open, ...)
    ↓
musl internal __syscall() / __syscall_cp()
    ↓
Omega syscall shim (per-arch assembly)
    ↓
Omega SyscallDispatcher::dispatch6()
    ↓
kernel/sys/ implementation
```

musl's source will live under `libc/musl/` in the repository (git submodule
or vendored tree). The Omega-specific shim layer (`libc/omega-shim/`) will
contain:

- Per-arch `__syscall` assembly (6-argument, Linux-numbered)
- TLS setup stubs (`__init_tls`, `__set_thread_area`)
- `__libc_start_main` entry glue
- `cxa_atexit` / `cxa_finalize` implementations backed by Omega process exit
- `sbrk`/`brk` implementation backed by `SYS_BRK` (after kernel fix)
- `_dl_relocate_object` stub (no-op for static builds)

## 4. Kernel Prerequisites

The following kernel changes must land **before** musl can be integrated.
They are scoped as kernel-only changes with no libc dependency.

### 4.1 File descriptor table expansion

| Item | Current | Required | File |
| :--- | :--- | :--- | :--- |
| `fd_table` size | 16 entries | 256 entries | `kernel/include/kernel/process.hpp` |
| FD limit enforcement | Hard-coded `16` | Configurable constant | `kernel/sys/syscall.cpp` |
| Pre-opened stdin/stdout/stderr | Not opened | FDs 0, 1, 2 at process creation | `kernel/sys/process.cpp` |

### 4.2 `SYS_BRK` implementation

| Item | Current | Required | File |
| :--- | :--- | :--- | :--- |
| `program_break` | Fixed `0x60000000` | Dynamic, page-aligned expansion | `kernel/sys/process.cpp` |
| Page mapping | None | Map/unmap pages on `brk` request | `kernel/sys/vmm.cpp` |
| `SYS_BRK` syscall | Returns new break, no mapping | Maps pages, returns new break or current on NULL | `kernel/sys/syscall.cpp` |

### 4.3 Missing POSIX syscalls

These syscalls are required by musl's startup and stdio layer:

| Syscall | Linux number (x86_64) | Omega status | Kernel file |
| :--- | :--- | :--- | :--- |
| `fstat` | 5 | Not implemented | `syscall.cpp` |
| `lseek` | 8 | Not implemented | `syscall.cpp` |
| `dup2` | 33 | Not implemented | `syscall.cpp` |
| `pipe2` | 293 | Not implemented | `syscall.cpp` |
| `getdents64` | 217 | Not implemented | `syscall.cpp` |
| `fcntl` | 72 | Not implemented | `syscall.cpp` |
| `uname` | 63 | Not implemented | `syscall.cpp` |
| `getpid` | 39 | Not implemented | `syscall.cpp` |
| `rt_sigaction` | 13 | Not implemented | `syscall.cpp` (stub returning `-ENOSYS`) |
| `clock_gettime` | 228 | Not implemented | `syscall.cpp` (stub) |

musl's startup (`__libc_start_main`) requires at minimum `fstat`, `lseek`,
`getdents64`, and `uname`. Signal-related syscalls can return `ENOSYS`
initially; musl handles unavailable signals gracefully.

### 4.4 TLS (Thread-Local Storage)

| Item | Current | Required | File |
| :--- | :--- | :--- | :--- |
| x86_64 TLS base | None | `%fs`-based, `0x600000000000` area | `kernel/arch/x86_64/boot.s`, `vmm.cpp` |
| AArch64 TLS base | None | `TPIDR_EL0` setup | `kernel/arch/aarch64/boot.s`, `vmm.cpp` |
| RISC-V TLS base | None | `TP` register setup | `kernel/arch/riscv64/boot.s`, `vmm.cpp` |
| TLS page in `Process` | None | Allocate and map TLS page per process | `kernel/include/kernel/process.hpp` |
| ELF TLS segment parsing | None | Parse `PT_TLS` in `ElfLoader` | `kernel/sys/elf_loader.cpp` |

TLS is required for musl's `errno` (which musl makes thread-local) and for
future `pthread` support. The initial integration needs only a single TLS
page per process; thread-count scaling comes later with `pthread`.

### 4.5 VFS and process creation hardening

| Item | Current | Required | File |
| :--- | :--- | :--- | :--- |
| `SYS_OPEN` flags | Ignores `O_RDONLY`/`O_RDWR` distinction | Proper `O_RDONLY=0`, `O_WRONLY=1`, `O_RDWR=2` | `syscall.cpp` |
| `VfsNode::flags` | Ignored in read/write | Enforce read-only vs writable | `vfs.cpp`, `ext4.cpp` |
| `SYS_IOCTL` | Not present | Stub returning `-ENOSYS` | `syscall.cpp` |

## 5. musl Port Details

### 5.1 musl version

Target **musl 1.2.5+** (latest stable as of 2025). This version supports
all three target ISAs and has the most complete POSIX coverage.

### 5.2 musl configuration

The supported invocation is the repository wrapper, which supplies the
out-of-tree build directory, Clang target, archive tools, generated headers,
Omega shim, and per-ISA linker script:

```sh
bash scripts/build_musl_sysroot.sh <x86_64|aarch64|riscv64> [--rebuild]
```

The generated SDK is the authoritative `--config-musl`-compatible sysroot
for TinyCC and for host cross-compiled Omega applications.

musl will be configured with:

```bash
# x86_64
./configure --target=x86_64-unknown-none-elf \
    --disable-shared --enable-static \
    --prefix=<omega-root>/libc/omega-sdk/x86_64

# aarch64
./configure --target=aarch64-unknown-none-elf \
    --disable-shared --enable-static \
    --prefix=<omega-root>/libc/omega-sdk/aarch64

# riscv64
./configure --target=riscv64-unknown-none-elf \
    --disable-shared --enable-static \
    --prefix=<omega-root>/libc/omega-sdk/riscv64
```

Additional musl config overrides (via `config.mak` or `make config`):

```makefile
# Omega-specific overrides
CFLAGS += -ffreestanding -fno-exceptions -fno-rtti
LDFLAGS += -nostdlib -nostdinc
ARCH = x86_64  # or aarch64, riscv64
```

### 5.3 Syscall shim layer

The Omega shim provides musl's internal syscall entry points and forwards the
Linux-numbered per-ISA syscall ABI to Omega's assembly syscall entry points.
The current shim does not maintain a separate translation table because the
musl and Omega numbers are aligned for the implemented subset.

```c
// libc/omega-shim/src/syscall.c (simplified)
long __syscall(long number, long a1, long a2, long a3, long a4, long a5, long a6) {
    // Translate musl number → Omega number via generated table
    uint64_t omega_nr = translate_syscall_number(number);
    return omega_syscall6(omega_nr, a1, a2, a3, a4, a5, a6);
}
```

### 5.4 TLS and startup

musl expects:

1. A valid TLS base in `%fs` (x86_64) / `TPIDR_EL0` (AArch64) / `tp` (RISC-V)
2. `__libc_start_main` to receive `argc`, `argv`, `envp`, and `auxv`
3. `cxa_atexit` for C++ destructor registration (can be a no-op initially)

Omega's ELF loader will be extended to:
- Parse `PT_TLS` segments and allocate TLS pages
- Set the TLS base register before entering userspace
- Preserve the existing `argc`/`argv`/`envp`/`auxv` stack layout so
  `__libc_start_main` can unpack it identically to `omega_start`

### 5.5 Header and sysroot layout

```
libc/
├── musl/                        # Upstream musl source (submodule)
│   ├── src/
│   ├── include/
│   └── ...
├── omega-shim/                  # Omega-specific syscall/startup shims
│   ├── src/
│   │   ├── syscall.S            # Per-arch __syscall assembly
│   │   ├── __libc_start_main.c  # Entry glue
│   │   ├── cxa_atexit.c         # Static destructor registry
│   │   └── brk.c                # sbrk/brk over SYS_BRK
│   ├── include/
│   │   └── omega/               # Omega-specific headers
│   └── CMakeLists.txt
├── omega-sdk/                   # Generated sysroot
│   ├── x86_64/
│   │   ├── include/
│   │   ├── lib/libc.a
│   │   └── lib/ldscripts/       # musl linker scripts
│   ├── aarch64/
│   └── riscv64/
└── CMakeLists.txt               # Top-level libc build
```

## 6. Build System Integration

### 6.1 Current state

- Kernel built via `CMakeLists.txt` + `cmake/<arch>-toolchain.cmake`
- Userspace programs built via `scripts/build_user_init.sh`,
  `scripts/build_user_c.sh` (direct `clang` + `ld.lld` invocations)
- No dependency tracking for userspace
- No cross-compilation sysroot

### 6.2 Target state

Add a `user/CMakeLists.txt` that builds both the Omega SDK and userspace
programs:

```cmake
# user/CMakeLists.txt (conceptual)
cmake_minimum_required(VERSION 3.20)
project(omega-userspace C CXX ASM)

# Find the generated sysroot
set(OMEGA_SDK_ROOT ${CMAKE_SOURCE_DIR}/libc/omega-sdk/${OMEGA_ARCH})

# Build the musl sysroot through the repository wrapper.
add_custom_target(omega-libc
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build_musl_sysroot.sh ${ARCH})
```

The top-level `CMakeLists.txt` will include userspace targets conditionally:

```cmake
option(OMEGA_BUILD_USERSPACE "Build the Omega musl sysroot and userspace tools" OFF)
option(OMEGA_BUILD_TCC "Build the target TinyCC binary when userspace is enabled" OFF)
option(OMEGA_BUILD_BASH "Build the static non-interactive Bash profile when userspace is enabled" OFF)
option(OMEGA_BUILD_COMMANDS "Build the static POSIX command suite when userspace is enabled" OFF)
if(OMEGA_BUILD_USERSPACE)
    # The top-level build creates omega-libc-${ARCH}; TCC, Bash, and commands
    # can be enabled independently and are collected by omega-userspace.
endif()
```

### 6.3 Compiler flags

Userspace programs will be compiled with:

```text
-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
-fno-use-cxa-atexit -nostdlib -nostdinc
-I<omega-sdk>/include
```

Note: `-fno-use-cxa-atexit` will be removed once musl's `cxa_atexit` is
fully functional. It is retained during the transition period.

### 6.4 Script migration

Shell scripts (`build_user_init.sh`, `build_user_c.sh`) will be replaced
by CMake targets. The scripts will remain as thin wrappers during the
transition:

```bash
# scripts/build_user_init.sh → calls CMake
cmake -B build/user-${ARCH} -DOMEGA_ARCH=${ARCH} -DOMEGA_BUILD_USERSPACE=ON
cmake --build build/user-${ARCH} --target init
```

## 7. ABI Stability and Compatibility

### 7.1 What does NOT change

| Item | Status |
| :--- | :--- |
| Syscall numbers | Unchanged (Linux per-ISA) |
| Syscall calling convention | Unchanged (`syscall`, `svc`, `ecall`) |
| ELF64 format | Unchanged (`PT_LOAD` only, static) |
| Stack layout (`argc`/`argv`/`envp`/`auxv`) | Unchanged |
| Error convention | Unchanged (negative → `errno`, return `-1`) |
| Kernel ELF entry point | Unchanged (`kernel/init/main.cpp`) |
| Process creation path | Unchanged (`fork` → `execve`) |

### 7.2 What DOES change for userspace

| Item | Before | After |
| :--- | :--- | :--- |
| C runtime | `omega_crt.c` (hand-rolled) | `musl/libc.a` |
| Startup | `omega_start()` → `main()` | `__libc_start_main()` → `main()` |
| `errno` | Global `int` | Thread-local `int` |
| `malloc` | Bump allocator, no `free` | musl allocator with `free`/`realloc` |
| `stdio` | `puts`/`fputs` only | Full `FILE*` buffered stdio |
| `environ` | Not available | Global `char**` with `getenv`/`setenv` |
| `fd_table` | 16 entries | 256 entries |
| stdin/stdout/stderr | Implicit via `SYS_WRITE` fd check | Pre-opened FDs 0, 1, 2 |

### 7.3 Backward compatibility

The existing freestanding programs (assembly `/init`, C SDK `/init`) will
continue to work because:

1. They do not depend on `libc.a` — they link directly against `omega_shim`
2. `omega_start()` is preserved as an alternative entry point
3. The syscall interface is unchanged
4. The stack layout is unchanged

New programs using musl will link against `libc.a` and start via
`__libc_start_main`. Both entry paths are supported simultaneously.

## 8. Testing Strategy

### 8.1 Host-side unit tests

| Test | Scope | Location |
| :--- | :--- | :--- |
| Syscall number translation | Verify musl → Omega number mapping | `libc/omega-shim/test/` |
| ELF validation | Reject dynamic/unsupported binaries | `tests/elf_loader_unit.cpp` (extended) |
| TLS layout | Verify `PT_TLS` parsing and register setup | `tests/tls_unit.cpp` |
| `brk` expansion | Verify page mapping on heap growth | `tests/brk_unit.cpp` |
| musl build | Build `libc.a` for all three ISAs | `libc/CMakeLists.txt` CI target |

### 8.2 QEMU integration tests

For each ISA (x86_64, AArch64, RISC-V):

| Test | Program | Verification |
| :--- | :--- | :--- |
| **LIBC-1** | musl `hello` (printf + exit) | Serial output, exit status 0 |
| **LIBC-2** | musl `malloc_test` (malloc/free/strdup) | No crash, correct output |
| **LIBC-3** | musl `stdio_test` (fopen/fread/fprintf) | File I/O via initrd |
| **LIBC-4** | musl `dir_test` (opendir/readdir/closedir) | Directory iteration on initrd |
| **LIBC-5** | musl `environ_test` (getenv/setenv) | Environment variable round-trip |
| **LIBC-6** | musl `errno_test` (thread-local errno) | Correct errno after failed syscall |
| **LIBC-7** | Legacy freestanding `/init` | Unchanged behavior (backward compat) |
| **LIBC-8** | Dynamic PT_INTERP rejection | Kernel rejects, test expects failure |
| **LIBC-9** | Wrong-ISA ELF rejection | Kernel rejects, test expects failure |
| **LIBC-10** | `brk` expansion test | Heap grows past initial break |

### 8.3 CI matrix

```text
CI lanes:
  - x86_64:  kernel build + musl build + LIBC-1..10
  - aarch64: kernel build + musl build + LIBC-1..10
  - riscv64: kernel build + musl build + LIBC-1..10
  - host:    musl build for all ISAs + unit tests + ELF validation
```

## 9. Implementation Phases

### Phase L1: Kernel Prerequisites (kernel-only, no libc)

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **L1.1** | Expand `fd_table` to 256 entries | Process creation, FD allocation test |
| **L1.2** | Pre-open stdin/stdout/stderr | FDs 0, 1, 2 available in userspace |
| **L1.3** | Fix `SYS_BRK` — map/unmap pages | `brk` test expands heap |
| **L1.4** | Add `fstat`, `lseek`, `dup2`, `pipe2`, `getdents64`, `fcntl`, `uname`, `getpid` | Each syscall returns correct values |
| **L1.5** | Add TLS page allocation and register setup | `%fs`/`TPIDR` points to valid TLS |
| **L1.6** | Parse `PT_TLS` in ELF loader | TLS segment mapped before entry |
| **L1.7** | Fix `SYS_OPEN` flag handling | `O_RDONLY`/`O_WRONLY`/`O_RDWR` enforced |

**Exit criteria:** All L1 milestones pass on all three ISAs in QEMU.

### Phase L2: musl Build and Shim Layer — implemented

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **L2.1** | Vendor musl and create `libc/` tree | Complete: `libc/musl/` and `libc/omega-shim/` |
| **L2.2** | Configure musl for all three ISAs | Complete: all three `libc.a` archives build |
| **L2.3** | Implement `__syscall` shim (per-arch) | Complete: `libomega-shim.a` is generated per ISA |
| **L2.4** | Implement `__libc_start_main` glue | `main(argc, argv, envp)` called correctly |
| **L2.5** | Implement `cxa_atexit`/`cxa_finalize` | C++ destructor registry works |
| **L2.6** | Implement `sbrk`/`brk` over `SYS_BRK` | Heap grows/shrinks |
| **L2.7** | Integrate musl build into CMake | Complete: `OMEGA_BUILD_USERSPACE=ON` exposes the target |

**Exit criteria:** met for the static SDK build and the x86_64 musl malloc
smoke test. Full cross-ISA QEMU libc coverage remains a later test milestone.

### Phase L3: Full musl Integration

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **L3.1** | Replace `omega_crt.c` with musl in `/init` | `/init` uses musl `printf`, `malloc`, `free` |
| **L3.2** | Enable stdio buffering (`FILE*`) | `fprintf` to stdout, `fopen` on initrd files |
| **L3.3** | Enable `getenv`/`setenv` via `environ` | Environment variable round-trip |
| **L3.4** | Enable thread-local `errno` | Concurrent error handling works |
| **L3.5** | Add `opendir`/`readdir`/`closedir` | Directory iteration via VFS |
| **L3.6** | Run full LIBC-1..10 test suite on all ISAs | All tests pass on x86_64, aarch64, riscv64 |
| **L3.7** | Remove `-fno-use-cxa-atexit` | C++ static destructors work |
| **L3.8** | Migrate shell scripts to CMake | `build_user_init.sh` calls CMake |

**Exit criteria:** musl-linked `/init` runs on all three ISAs; LIBC-1..10
pass; existing freestanding `/init` still works.

### Phase L4: POSIX Profile and SDK Hardening

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **L4.1** | Add `pwd.h`/`grp.h` after UID/GID database exists | `getpwnam` returns correct entries |
| **L4.2** | Add `time.h` after `clock_gettime` | `time()`, `gettimeofday()` work |
| **L4.3** | Add `signal.h` stubs (`ENOSYS`) | musl handles gracefully |
| **L4.4** | Build a small shell (`ash`-sized) | Interactive shell on serial console |
| **L4.5** | Add `omega-check-elf` validation tool | Wrong-ISA/dynamic binaries rejected |
| **L4.6** | Add reproducible SDK packaging | `omega-sdk` tarball with sysroot + tools |
| **L4.7** | Update `OMEGA_SDK_PLAN.md` with musl decision | Documented, consistent with SDK plan |

**Exit criteria:** Small POSIX utility set builds and runs; SDK v1 `posix-static`
profile documented and tested.

## 10. Relationship to Existing Plans

This plan is a concrete instantiation of the following existing Omega
documentation:

| Document | Relationship |
| :--- | :--- |
| `docs/OMEGA_SDK_PLAN.md` | Defines SDK profiles (`omega-c`, `posix-static`), sysroot layout, and verification requirements. This plan specifies musl as the `posix-static` implementation. |
| `docs/ROADMAP.md` Phase 10.1.1 | Static musl SDK is implemented; this plan defines the remaining hosted-POSIX sequence. |
| `docs/ROADMAP.md` Phase 7.D.5 | "Userspace bootstrap" — lists full libc, shell, signals, and execve as remaining work. This plan covers the libc portion. |
| `docs/ABI.md` | Syscall numbers and calling conventions are unchanged; this plan depends on the ABI being frozen. |
| `docs/ARCHITECTURE.md` | HAL interfaces are unchanged; musl sits entirely in userspace. |

## 11. Risks and Mitigations

| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| musl assumptions about kernel behavior | Build succeeds, runtime fails | Test musl on QEMU early; patch musl if needed |
| TLS complexity on all three ISAs | Silent memory corruption | Implement and test TLS before musl TLS segment |
| `SYS_BRK` page-mapping bugs | Heap corruption, crashes | Host unit tests + QEMU integration tests |
| Build system complexity | CMake becomes unwieldy | Keep kernel and userspace builds separate; thin script wrappers |
| musl upstream divergence | Omega patches drift from upstream | Minimize patches; contribute fixes upstream where possible |
| Static binary size | musl `libc.a` is ~400 KiB | Acceptable for static-first; dynamic linking follows later |

## 12. Exit Criteria and current boundary

The static musl port is complete when:

1. musl builds as a static `libc.a` for x86_64, aarch64, and riscv64
2. The existing freestanding `/init` continues to work unchanged
3. Build is driven by the scripts and CMake (`OMEGA_BUILD_USERSPACE=ON`)
4. The SDK manifest and static linker script are generated per ISA
5. `OMEGA_SDK_PLAN.md` reflects musl as the `posix-static`
   implementation
6. The remaining runtime APIs are covered by the follow-up LIBC-1..10 plan

Dynamic linking and the complete hosted-POSIX checklist are intentionally not
claimed by the current port.

## 13. Related Documentation

| Document | Scope |
| :--- | :--- |
| `docs/OMEGA_SDK_PLAN.md` | SDK profiles, sysroot layout, C/C++ runtime strategy |
| `docs/ON_TARGET_COMPILER_PLAN.md` | TinyCC on-target compiler + distro shipping, built on this plan's L1–L3 |
| `docs/ROADMAP.md` | Phase 10.1.1 (C Library), Phase 7.D.5 (Userspace bootstrap) |
| `docs/ABI.md` | Syscall ABI specification (unchanged by this plan) |
| `docs/ARCHITECTURE.md` | Kernel architecture and HAL design |
| `docs/RUNNING.md` | Build and QEMU execution guide |
