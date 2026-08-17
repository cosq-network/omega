# On-Target C Compiler Integration Plan (TinyCC + musl)

## Implementation status (2026-08-17)

The host-side Omega target compiler port is implemented. TinyCC builds as a
static Omega ELF for x86_64, AArch64, and RISC-V using the matching
`libc/omega-sdk/<isa>` sysroot:

```sh
bash scripts/build_tcc.sh x86_64
bash scripts/build_tcc.sh aarch64
bash scripts/build_tcc.sh riscv64
bash scripts/test_libc_integration.sh
```

Compiler binaries are copied to `libc/omega-sdk/<isa>/bin/tcc`; intermediate
build products are under `build/tcc-<isa>/`. AArch64 and RISC-V also build a
target-ABI-matched `lib/libtcc1.a` for compiler-generated arithmetic and
cache-flush helpers.

This completes the build/port milestone. The on-target compile-and-run
milestone in Phase L5 is not yet claimed: it still requires a userspace
`execve` workflow and a QEMU harness that boots TCC inside Omega.

## 1. Purpose and Positioning

Omega currently builds as a **freestanding** kernel: the userspace runtime
(`omega_crt.c`, `user/`) is a minimal hand-rolled C library, and programs are
compiled **off-target** with Clang on the host (`-nostdlib -ffreestanding
-nostdinc`) and linked as static `ET_EXEC` ELF64 binaries.

This document defines the plan to **ship a C compiler that runs on Omega
itself** — the on-target self-hosting milestone — alongside the **musl libc**
that the compiler links programs against.

- **TinyCC (TCC)** is the chosen on-target compiler: compiler + assembler +
  linker in one small binary, emitting static `ET_EXEC` ELF64 with no
  PIC/dynamic dependencies, with backends for **x86_64, AArch64, and
  RISC-V 64**. TCC has **no libc of its own**.
- **musl** is the static `libc.a` (MIT) that TCC links user programs
  against. It is built once per ISA **on the host** (Clang) and shipped in
  the initrd. musl is never recompiled on-target (TCC lacks `_Complex`).

This plan builds on the implemented static musl port defined in
[`libc-integration.md`](libc-integration.md) (Phases L1–L3), and fulfills the
"small shell and file utility set" and self-host goals of
[`OMEGA_SDK_PLAN.md`](OMEGA_SDK_PLAN.md).

## 2. Design Decisions

1. **TCC, not chibicc/GCC/Clang-on-target.** TCC is the only realistic
   compiler with x86_64 + AArch64 + RISC-V 64 backends, is small and fast
   enough for a research OS, is self-contained (assembler + linker built
   in), and ships static `ET_EXEC` output that the Omega ELF loader accepts.
   chibicc lacks a mature AArch64 backend; GCC/Clang cannot self-host in the
   available footprint.
2. **musl is built once on the host** (Clang, per-ISA), shipped in the
   initrd, and linked *against* by TCC. Never recompiled on-target.
3. **License: LGPL 2.1 for TCC is acceptable** as a separate distro binary
   (like Alpine ships musl/busybox alongside Apache components). It is not
   linked into the kernel. The Apache-2.0 tree stays clean; TCC lives under
   `tools/` with its own license file.
4. **Phased, dependency-ordered.** Kernel prerequisites → musl → TCC
   build/port → on-target execution → distro shipping. Nothing about TCC
   begins until `execve`, file write, and `brk` work.
5. **Verification = QEMU serial-grep + host unit tests**, matching the
   existing test harness (`scripts/test.sh` pattern).

## 3. Current Kernel Baseline (remaining gaps)

The kernel supports the boot-time static musl smoke path and `/tmp`/initrd
infrastructure. It does not yet support the complete on-target TCC workflow.
The remaining gaps are:
(`kernel/sys/syscall.cpp`, `kernel/sys/process.cpp`,
`kernel/sys/elf_loader.cpp`):

| Gap | State today | Blocks |
| :--- | :--- | :--- |
| `execve` | General process replacement remains incomplete; boot-time `/init` works | Running a compiler-produced child program |
| `write(2)` | Only fds 1/2 → console; no VFS write path | TCC writing output files |
| File creation | No `O_CREAT`, no VFS create, no ext4 inode allocation | TCC `-o` output |
| `brk` | Page-backed growth is implemented for the static musl path | Full compiler workload stress testing |
| Missing syscalls | `fstat`, `lseek`, `dup2`, `pipe2`, `getdents64`, `fcntl`, `uname`, `getpid`, `rt_sigaction`, `clock_gettime` absent → `-1` | musl startup, stdio, TCC file I/O |
| `fd_table` | Expanded to 256 entries | Further fork/stdio validation |
| argv/env | `argv[0]` hardcoded `"/init"`; no argv/env passing | General program execution |
| `mmap` | Anonymous only; `PROT_EXEC` honored | TCC `-run` needs EXEC pages |

All of these must land (as Phase L1 of the libc plan, plus the file/exec
layer below) before TCC can run.

## 4. Phase L1: Kernel Prerequisites

Reused from [`libc-integration.md`](libc-integration.md) §4 — required by
**both** musl and TCC. Implemented kernel-only, tested on all three ISAs.

| # | Item | File | Detail |
| :--- | :--- | :--- | :--- |
| L1.1 | `fd_table` → 256 entries | `kernel/include/kernel/process.hpp` | Replace `fd_table[16]`; enforce limit via constant |
| L1.2 | Pre-open stdin/stdout/stderr | `kernel/sys/process.cpp` | Install console node at fds 0/1/2 on create |
| L1.3 | Fix `brk` to map/unmap pages | `kernel/sys/process.cpp` | Track heap pages in `mappings[]`; page-fault on touch must not panic |
| L1.4 | Add `fstat`, `lseek`, `dup2`, `pipe2`, `getdents64`, `fcntl`, `uname`, `getpid` | `kernel/sys/syscall.cpp` | Real implementations; `rt_sigaction`/`clock_gettime` may return `-ENOSYS` initially |
| L1.5 | TLS page + register setup | `kernel/arch/<isa>/boot.s`, `process.hpp` | Single TLS page per process (`%fs`/TPIDR/tp) |
| L1.6 | Parse `PT_TLS` in ELF loader | `kernel/sys/elf_loader.cpp` | Set TLS base before userspace entry |
| L1.7 | Fix `SYS_OPEN` flag handling | `kernel/sys/syscall.cpp` | Honor `O_RDONLY`/`O_WRONLY`/`O_RDWR`; add `O_CREAT` constant |
| L1.8 | Add `SYS_IOCTL` stub | `kernel/sys/syscall.cpp` | Return `-ENOSYS` (musl startup tolerates) |

**Exit criteria:** all L1 items pass on x86_64, AArch64, RISC-V in QEMU.

## 5. Phase L2: File I/O and execve

TCC is a **file-based** compiler (reads `.c`, writes `.o`/executables), so
before it can run, the kernel needs a real file + process layer.

### 5.1 Generalize `execve`

`ElfLoader::load_into(process, data, size, entry, stack)` already maps PT_LOAD
segments and builds a stack; it is hardcoded to `/init`. Changes:

- **Generalize `load_into`** (`kernel/sys/elf_loader.cpp`): accept
  `argc/argv/envp` and a program-name string instead of hardcoding
  `"/init"`; build the stack ABI from them. Keep the fixed per-ISA stack
  layout.
- **Implement `sys_execve`** (`kernel/sys/syscall.cpp`): resolve the path via
  `vfs::VirtualFilesystem::open` (or `Initrd::find` fallback), read the file
  bytes, validate ELF, tear down the current process's mappings, then
  `load_into` the current process with the new image and new argv.
- Keep boot-time `/init` loading working (pass `/init` as argv[0]).

### 5.2 VFS file write + creation

- **Wire `write(2)` through the VFS** (`kernel/sys/syscall.cpp`): if fd is a
  real file fd, call `VirtualFilesystem::write` (already exists at
  `vfs.cpp:44` but has no syscall caller); keep console writes for fds 1/2.
- **Add `O_CREAT`/`O_TRUNC` handling** to `open`/`openat`; add a `create`
  function pointer to `VfsNode` (`kernel/include/kernel/vfs.hpp`).
- **tmpfs at `/tmp`** for the compiler milestone: the initrd is read-only
  and ext4 cannot yet create/extend files (`ext4.cpp` `file_write` is
  overwrite-in-place only). A small in-memory tmpfs mounted at `/tmp` gives
  TCC a writable output directory without waiting for full ext4 write
  support. **Note:** true ext4 file creation requires inode/block allocation
  — a large, separate milestone
  ([`STORAGE_ARCHITECTURE_PLAN.md`](STORAGE_ARCHITECTURE_PLAN.md)).
- **Directory listing**: add `getdents64` over a `readdir` VFS hook
  (initrd list + tmpfs) so `ls`-style tools work.

**Exit criteria:** a userspace program can `open("/tmp/x","w")`, `write`,
`close`, `execve` another program with argv, and read back what it wrote —
on all three ISAs in QEMU.

## 6. Phase L3: musl Build — implemented

Follow [`libc-integration.md`](libc-integration.md) Phases L2–L3 exactly:

- Vendor musl under `libc/musl/`; build static `libc.a` for all three ISAs
  with Clang, in a `--config-musl`-compatible layout.
- Build the current Omega shim (`libc/omega-shim/`): per-arch syscall
  assembly, startup glue, and `brk`/TLS support used by the static path.
- Produce a sysroot per ISA: `libc/omega-sdk/<isa>/include`, `lib/libc.a`,
  `lib/crt1.o`, linker script.
- **TCC config**: build TCC with `--config-musl` so its default sysroot
  paths point at the Omega musl sysroot (no glibc assumptions).

**Exit criteria:** all three static sysroots build; x86_64 musl malloc smoke
testing passes under QEMU. Cross-ISA QEMU libc coverage remains follow-up work.

## 7. Phase L4: TCC Port

### 7.1 Source and layout — implemented

- Vendor TCC under `tools/tcc/` with its
  LGPL-2.1 license file preserved. Add `tools/tcc/OMEGA.md` documenting the
  port.
- Configure/build through the repository wrapper:
  ```bash
  bash scripts/build_tcc.sh x86_64
  bash scripts/build_tcc.sh aarch64
  bash scripts/build_tcc.sh riscv64
  ```
  Produces a static `tcc` binary per ISA, linked against the Omega musl
  sysroot.
- **Patches** (track in `tools/tcc/patches/`):
  - Verify the musl sysroot covers everything TCC's generated code and its
    own runtime need (most already in the L1 syscall set).
  - `-run` mode needs executable memory mapping: TCC mmaps code pages with
    `PROT_EXEC` — the kernel's `mmap` already honors `page_flags(prot)` with
    `PAGE_EXEC` (`process.cpp:118`); verify on QEMU.
  - TCC emits standard ELF — `ElfLoader::validate` must accept TCC output
    (static ET_EXEC, PT_LOAD, no PT_INTERP). Add a unit test compiling a
    hello world with the TCC binary and loading it.
- TCC's own test suite (`tests/tests2/`) is the compiler gate.

### 7.2 Architecture status

| ISA | TCC backend | Maturity | Risk |
| :--- | :--- | :--- | :--- |
| x86_64 | `x86_64-gen.c` | Most mature | Low |
| AArch64 | `arm64-gen.c` | Functional; occasional struct-passing/float ABI edges | Medium |
| RISC-V 64 | `riscv64-gen.c` | Real but least mature (relocations/asm still landing) | **Highest** |

The three target compiler ELFs currently build and pass the host integration
check. RISC-V remains the highest-risk backend for future compiler conformance
testing, but is no longer an unbuilt/unsupported target.

### 7.3 Integration with musl

- TCC links user programs against the **same** `libc.a` + `crt1.o` +
  linker script that host Clang builds use — one sysroot, two frontends.
- Verify TCC can compile a hello-world against the musl sysroot **on the
  host** first (fast loop), then on-target.

**Build exit criteria:** a static `tcc` ELF is produced for all three ISAs and
consumes the same Omega musl SDK as the host toolchain. The on-target
`tcc hello.c -o hello` execution criterion remains Phase L5.

## 8. Phase L5: On-Target Execution

### 8.1 Initrd content

The initrd packer now accepts multiple named files, which is the packaging
primitive required by this phase:

```sh
python3 scripts/create_initrd.py build/initrd.img build/init.elf \
  --file bin/tcc=libc/omega-sdk/x86_64/bin/tcc \
  --file lib/libc.a=libc/omega-sdk/x86_64/lib/libc.a \
  --file lib/crt1.o=libc/omega-sdk/x86_64/lib/crt1.o \
  --file lib/omega.ld=libc/omega-sdk/x86_64/lib/omega.ld
```

Directory hierarchy entries are supported by the kernel initrd loader. The
actual `/bin/tcc` boot-and-exec test remains pending until the execve path is
complete.

Extend `scripts/create_initrd.py` to pack multiple files (the initrd format
already supports `nfiles > 1`):

```text
init          # existing freestanding /init (or musl-linked /init)
bin/tcc       # TCC static binary for the target ISA
bin/cc        # tiny wrapper (script or TCC exec) so users write `cc`
lib/libc.a    # musl static archive
lib/crt1.o    # startup object
lib/omega.ld  # linker script
include/      # musl + omega headers (stdint.h, stdio.h, ...)
tmp/          # empty dir node for scratch (tmpfs)
```

### 8.2 TCC command on-target

`tcc` on Omega must find headers/libs. Options (pick one, default first):

- **`-B` + sysroot env**: invoke as
  `tcc -B/lib/tcc -I/include -L/lib hello.c -o /tmp/hello`
  (paths baked per-ISA into `bin/cc`), or
- **`--config-musl` baked sysroot** if the on-target tree matches TCC's
  configured prefix (requires `/lib`, `/include` layout in initrd).

### 8.3 QEMU integration test (the milestone gate)

New `scripts/test_on_target_compiler.sh` (per ISA, mirrors
`test_c_sdk.sh` pattern: build, pack multi-file initrd, QEMU, grep serial):

1. boot Omega with initrd containing `/init`, `/bin/tcc`, libc, headers;
2. `/init` writes `hello.c` to `/tmp`, runs `tcc hello.c -o /tmp/hello`
   (via execve), execs `/tmp/hello`;
3. serial log shows `[ON-TARGET][PASS] hello from tcc` + exit 0;
4. later: compile+run a program using musl `printf`/`malloc`/`free`.

**Exit criteria:** the QEMU on-target compile-and-run test passes on all
three ISAs.

## 9. Phase L6: Distro Shipping

### 9.1 Artifacts

- Add TCC + musl to the release: extend `scripts/create_bootable_disk.sh`
  (or the future ext4 image builder) to place `bin/tcc`, `lib/`, `include/`
  onto the shipped filesystem image.
- Document the LGPL notice for TCC in the release assets (LICENSE-compliant
  attribution, separate binary).

### 9.2 SDK integration

- Add a `posix-static` profile note: TCC is the on-target compiler; Clang is
  the host cross-compiler; both consume the same Omega musl sysroot.
- Add an `omega-check-elf`-style validation for TCC-produced binaries
  (already covered by `ElfLoader::validate`; keep in CI).

### 9.3 CI

- New lane: build TCC for all three ISAs + run the on-target compiler test
  on all three ISAs in the Alpine CI container (needs nothing new: clang,
  lld, qemu already installed).
- Keep the existing freestanding `/init` passing (backward compat).

## 10. Verification Summary

| Layer | Test | Where |
| :--- | :--- | :--- |
| Kernel L1 | L1.1–L1.8 QEMU boot + syscall tests | `scripts/test*.sh` |
| File/exec | write-file, create, execve argv round-trip | QEMU on 3 ISAs |
| musl | LIBC-1..10 from libc-integration.md | QEMU on 3 ISAs |
| TCC host | `tcc` compiles hello against sysroot | host unit |
| TCC on-target | compile+run hello via serial grep | `scripts/test_on_target_compiler.sh` |
| Backward compat | existing `/init` still boots | `test_userland.sh` |

## 11. Key Risks

| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| `execve` + file I/O are large kernel changes | Blocks all compiler work | Land L1 + file/exec first, test per-ISA, before any TCC work |
| ext4 file creation is a big separate milestone | TCC cannot write to a real FS | Use tmpfs at `/tmp` for the compiler milestone; defer ext4 grow |
| TCC `-run`/mmap EXEC edge cases | Compile-time crashes | Verify `mmap` `PROT_EXEC` early; host-test before on-target |
| RISC-V backend immaturity | Risky on-target gate | Validate `tests/tests2/` on riscv64 QEMU earliest; worst case ship experimental |
| musl startup syscall gaps | musl/TCC won't start | L1.4 covers the required set; `ENOSYS` tolerated for signals |
| LGPL vs Apache tree | License mismatch | TCC is a separate shipped binary with its own license; not linked into kernel |
| TCC output rejected by loader | Compile succeeds, run fails | Add unit test: TCC-compiled hello loads via `ElfLoader` |

## 12. Relationship to Existing Plans

| Document | Relationship |
| :--- | :--- |
| `docs/libc-integration.md` | Defines the musl port (L1–L3) this plan builds on; phases L1–L3 are shared prerequisites |
| `docs/OMEGA_SDK_PLAN.md` | Defines SDK profiles; TCC is the on-target compiler for the `posix-static` profile |
| `docs/ROADMAP.md` | Phase 10.1.1 (C Library), Phase 7.D.5 (Userspace bootstrap) |
| `docs/ABI.md` | Syscall numbers and calling conventions unchanged; this plan depends on the ABI being frozen |
| `docs/ARCHITECTURE.md` | HAL interfaces unchanged; TCC + musl sit entirely in userspace |
| `docs/STORAGE_ARCHITECTURE_PLAN.md` | ext4 write/create is a prerequisite for persistent on-target output |

## 13. Recommended Sequence

1. **L1 kernel prerequisites** (brk, fds, missing syscalls, TLS, open flags)
2. **execve generalization + VFS write/create + tmpfs** (file/exec layer)
3. **musl build + shim (L2)** → musl hello runs on 3 ISAs
4. **TCC build/port (L4)** → host-verified against Omega sysroot
5. **Multi-file initrd + on-target test (L5)** → gate passes on 3 ISAs
6. **Distro packaging + CI lane + docs (L6)**

Each step is independently verifiable and all three ISAs are exercised at
every gate.
