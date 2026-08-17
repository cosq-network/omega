# Bash Porting Plan for Omega

## Document status

| Field | Value |
| :--- | :--- |
| Target shell | GNU Bash 5.3 or a later compatible patch release |
| Target architectures | x86_64, AArch64, RISC-V 64 (`rv64gc`) |
| Initial linkage model | Static ELF64, linked against the Omega musl SDK |
| Initial installation path | `/bin/bash` |
| Initial shell contract | Non-interactive `bash -c` execution |
| Final shell contract | Interactive Bash on an Omega TTY with job control |
| Status | B0 and B2 implemented; B3 runtime execution remains blocked by process/exec work |
| Primary dependencies | [`libc-integration.md`](libc-integration.md), [`ON_TARGET_COMPILER_PLAN.md`](ON_TARGET_COMPILER_PLAN.md), [`ABI.md`](ABI.md) |

This document is the execution plan for porting Bash to Omega. It is deliberately
more specific than the general SDK and libc plans: it defines the kernel work,
userspace libraries, build configuration, initrd packaging, terminal model,
cross-architecture testing, and completion gates required for a usable Bash.

The plan distinguishes three deliverables:

1. **Bash script runner:** `bash -c '...'` for boot scripts and automation.
2. **Bash process shell:** pipelines, redirections, command substitution,
   background processes, and shell scripts.
3. **Interactive Bash:** a prompt, line editing, history, signals, job control,
   terminal modes, and a usable serial or framebuffer terminal.

The first two deliverables can be completed without a full terminal subsystem.
The third cannot. Bash's interactive behavior depends on operating-system
support for terminals, signals, sessions, and process groups; it is not only a
userspace parser feature.

### Current implementation status

- `scripts/build_bash.sh` implements the first static, non-interactive profile.
- Bash 5.3 builds successfully for x86_64, AArch64, and RISC-V 64.
- `scripts/test_bash_build.sh` validates the three static ELF artifacts and
  their manifests.
- `CMakeLists.txt` exposes `OMEGA_BUILD_BASH=ON` under
  `OMEGA_BUILD_USERSPACE=ON`.
- The artifacts are not yet booted by Omega. B3 is blocked until general
  `execve`, process replacement, and the shell's runtime syscall set are
  complete.

## 1. Scope and non-goals

### 1.1 In scope

- Build Bash from a pinned upstream source release for all three Omega ISAs.
- Link Bash statically against `libc/omega-sdk/<isa>`.
- Execute Bash as a normal ELF userspace process loaded by Omega.
- Provide a POSIX-compatible `/bin/sh` entry point after the shell runtime is
  proven. The exact `/bin/sh` implementation may initially be a smaller shell.
- Support shell scripts, environment variables, filesystem redirection,
  pipelines, command substitution, and process waiting.
- Provide an interactive TTY-backed Bash as the final milestone.
- Add deterministic host and QEMU tests for x86_64, AArch64, and RISC-V.
- Document license and source-provenance requirements for Bash and Readline.

### 1.2 Out of scope for the first Bash release

- Dynamic linking, shared `libbash` components, or a dynamic loader.
- Full Linux binary compatibility.
- Bash networking features such as `/dev/tcp` and `/dev/udp`.
- Process substitution until `/dev/fd`, FIFOs, or an equivalent Omega facility
  exists.
- Complete locale, gettext, NLS, and internationalization support.
- Bash loadable builtins and arbitrary shared-object plugins.
- Bash as PID 1. A small init program should supervise the first shell.
- Full POSIX user/group databases unless required by the selected utilities.
- Multi-user terminal security beyond the kernel's initial credential model.

## 2. Current Omega baseline

Omega already provides the foundation for static C userspace:

- Host-built static musl SDKs for x86_64, AArch64, and RISC-V.
- Per-ISA CRT objects, syscall shims, linker scripts, and SDK manifests.
- Static TinyCC target binaries for all three ISAs.
- ELF64 PT_LOAD mapping and initial userspace stack construction.
- Per-process address spaces and COW lifecycle primitives.
- Basic process, descriptor, memory, VFS, initrd, and tmpfs infrastructure.

These are necessary but not sufficient for Bash. The current ABI documentation
still identifies general `execve`, complete signal semantics, and broader
userspace process behavior as incomplete. See [`ABI.md`](ABI.md) and the
userspace entries in [`ROADMAP.md`](ROADMAP.md).

### 2.1 Baseline capability matrix

| Capability | Current planning assessment | Bash consequence |
| :--- | :--- | :--- |
| Static musl | Implemented as an SDK build milestone | Suitable foundation for Bash |
| Static ELF loading | Implemented for boot-time userspace | Must be generalized for arbitrary Bash binaries |
| `fork`/COW/`wait4` | Foundation exists; lifecycle hardening remains | Required for subshells and pipelines |
| `execve` | Not yet a complete isolated replacement path | Blocks running external commands reliably |
| `open`/`read`/`write`/`close` | Basic paths exist | Must support regular files, pipes, and terminal FDs consistently |
| `dup2`/`pipe2` | Present or being integrated, but needs end-to-end validation | Required for redirection and pipelines |
| `fcntl` | Minimal | Must support descriptor flags and close-on-exec behavior |
| `brk`/`mmap` | Static musl path exists | Must survive Bash's larger and longer-lived allocation pattern |
| Initrd/tmpfs | Present | Needed for `/bin`, scripts, `/tmp`, and writable state |
| Directory APIs | Partial | Needed for `PATH` lookup, startup files, and utilities |
| Signals | Incomplete; signal action paths may return `ENOSYS` | Blocks Ctrl-C, termination, traps, and job control |
| TTY/PTY | Not complete | Blocks interactive Bash and Readline |
| Sessions/process groups | Not complete | Blocks foreground/background job control |
| `ioctl`/termios | Not complete | Blocks terminal mode changes and window-size handling |

The consequence is explicit: the first Bash milestone must be non-interactive,
and the final interactive milestone must be gated on kernel terminal work.

## 3. Upstream and licensing decisions

### 3.1 Source selection

Pin one Bash release in the repository's source manifest. The initial target is
Bash 5.3, with the exact tarball checksum recorded before vendoring or fetching
it in CI. Keep Omega patches as separate files rather than editing the source
tree without a record.

Required source metadata:

```text
source name: bash
version: 5.3.x
source URL: https://ftp.gnu.org/pub/gnu/bash/
checksum: SHA-256 recorded in the build manifest
license: GNU GPL version 3 or later
patch directory: tools/bash/patches/
port notes: tools/bash/OMEGA.md
```

Bash's official documentation describes its interactive features, including
line editing, history, aliases, signal handling, and job control:

- [GNU Bash project page](https://www.gnu.org/s/bash/)
- [Bash interactive shell behavior](https://www.gnu.org/software/bash/manual/html_node/Interactive-Shell-Behavior.html)
- [Bash job-control basics](https://www.gnu.org/software/bash/manual/html_node/Job-Control-Basics.html)

### 3.2 License obligations

- Preserve Bash's GPLv3-or-later notices and source offer requirements.
- Preserve the license and notices of every bundled component.
- Treat Readline as a separate licensing decision. Readline is GPL-licensed,
  not LGPL-licensed, so a Bash build with Readline has the corresponding
  distribution obligations.
- Do not link Bash or Readline into the Omega kernel.
- Add license files to the initrd source/package manifest.
- Record whether the distributed image includes complete corresponding source,
  patch files, and build instructions.

References:

- [Bash project and license information](https://www.gnu.org/s/bash/)
- [GNU GPL](https://www.gnu.org/licenses/gpl.en.html)
- [Readline licensing discussion](https://www.gnu.org/licenses/why-not-lgpl.en.html)

## 4. Target architecture and ABI policy

Bash is a userspace program. It must not contain architecture-specific kernel
logic. The only architecture-specific parts are the compiler target, startup
objects, syscall ABI already supplied by musl, ELF relocation output, and any
small architecture-specific runtime fixes discovered during testing.

| ISA | Compiler target | Kernel entry | Initial artifact |
| :--- | :--- | :--- | :--- |
| x86_64 | Omega x86_64 Clang/TCC target | `syscall`/`sysret` path | `bash-x86_64.elf` |
| AArch64 | Omega AArch64 Clang/TCC target | `svc`/`eret` path | `bash-aarch64.elf` |
| RISC-V 64 | Omega RISC-V 64 Clang/TCC target | `ecall`/`sret` path | `bash-riscv64.elf` |

All three builds must use the matching SDK only:

```text
libc/omega-sdk/x86_64/
libc/omega-sdk/aarch64/
libc/omega-sdk/riscv64/
```

An artifact is invalid if it has the wrong `e_machine`, contains a dynamic
interpreter, requires a shared object, or is linked against a host libc.

## 5. Dependency graph

The port must follow this order:

```text
musl SDK and ELF loader
        |
        +--> process replacement and argv/envp
        |          |
        |          +--> external commands and shell scripts
        |
        +--> file descriptors, VFS, tmpfs, directory APIs
        |          |
        |          +--> redirection and PATH lookup
        |
        +--> pipes, fork, wait, descriptor inheritance
        |          |
        |          +--> pipelines, subshells, command substitution
        |
        +--> signals
                   |
                   +--> traps, interrupts, termination
                              |
                              +--> TTY, sessions, process groups
                                         |
                                         +--> job control and interactive Bash
                                                    |
                                                    +--> Readline/history
```

No later phase should be marked complete because Bash merely compiles. Each
phase requires an Omega boot test that exercises the corresponding behavior.

## 6. Bash build profile

### 6.1 Initial profile

The first build should minimize dependencies and avoid features that require
kernel facilities not yet present:

| Feature | Initial setting | Reason |
| :--- | :--- | :--- |
| Static linking | Enabled | Matches the Omega SDK and current ELF loader |
| Readline | Disabled | No TTY/termios implementation yet |
| History | Disabled or non-persistent | Avoids startup and writable-home dependency |
| Job control | Disabled initially | Requires sessions, process groups, and terminal control |
| NLS/gettext | Disabled | Avoids libintl/locale dependency |
| Process substitution | Disabled initially | Requires `/dev/fd`, FIFOs, or equivalent |
| Network redirections | Disabled/not supported | Requires sockets and network device semantics |
| Bash malloc | Disabled | Use musl allocator and its `malloc`/`free` implementation |
| Loadable builtins | Disabled | No dynamic loader or shared-object ABI |

The exact configure switches must be taken from the pinned release's
`./configure --help`; do not assume that a switch from another Bash version is
available. Bash supports out-of-tree builds and `--host` cross configuration,
but configure tests that execute target binaries must be supplied with cache
answers or replaced with deterministic feature tests.

### 6.2 Cross-build model

The future build wrapper should be:

```sh
bash scripts/build_bash.sh x86_64
bash scripts/build_bash.sh aarch64
bash scripts/build_bash.sh riscv64
```

The wrapper must:

1. Validate the architecture name.
2. Ensure the matching musl SDK exists or build it.
3. Select the matching compiler, assembler, linker, `ar`, and `ranlib`.
4. Configure Bash in `build/bash-<isa>/` out of tree.
5. Supply the matching sysroot and Omega linker script.
6. Use a checked-in configure cache for target-runtime probes.
7. Build without executing target binaries on the host.
8. Run `readelf`/LLVM validation on the result.
9. Copy only the final ELF and license metadata into the staging tree.

The port must not rely on host `/usr/include`, host `libc.so`, host Readline,
or host `config.h`. Every target-specific generated file belongs under the
architecture-specific build directory.

### 6.3 Expected output layout

```text
build/bash-x86_64/
build/bash-aarch64/
build/bash-riscv64/

userland/bash/<isa>/bin/bash
userland/bash/<isa>/licenses/bash/
userland/bash/<isa>/manifest.json
```

The final package should place the runtime files in an initrd staging tree:

```text
/bin/bash
/bin/sh                  # only after the selected /bin/sh policy is approved
/etc/profile             # optional, after startup files are supported
/etc/bash.bashrc         # optional, later milestone
/tmp/
/dev/null
/dev/console
```

## 7. Kernel and libc work items

### 7.1 Process and execution layer

Implement and verify:

- `execve` replaces the current address space atomically from the caller's
  perspective, or terminates the process cleanly on a load failure.
- The new image receives correct `argc`, `argv`, `envp`, and `auxv`.
- `argv[0]` is the invoked path or shell command name, not always `/init`.
- File descriptors marked close-on-exec are closed; ordinary descriptors are
  inherited.
- The process retains the intended PID and credentials across `execve`.
- Failed `execve` preserves the old image and returns a useful errno where
  possible.
- `fork` creates a child that can safely call `execve` without corrupting the
  parent's mappings or descriptor table.
- `wait4` reports child exit status and reaps the child exactly once.

Required tests:

```text
execve /bin/hello
execve missing path -> ENOENT
fork -> child exec -> parent wait4
fork -> child failure -> parent observes non-zero exit
descriptor inheritance and close-on-exec
argv/envp propagation on all ISAs
```

### 7.2 Descriptor and VFS layer

Bash uses descriptors as its primary composition mechanism. Complete:

- Correct read/write access mode checks.
- Per-open file offset and `lseek` behavior.
- `dup`, `dup2`, and `fcntl` descriptor flags.
- `FD_CLOEXEC` handling.
- `pipe2` with blocking semantics, EOF after all writers close, and broken-pipe
  behavior.
- Directory descriptors and `getdents64`.
- `stat`/`fstat` fields needed by musl and utilities.
- `O_CREAT`, `O_TRUNC`, `O_APPEND`, and permission validation.
- `chdir`, `getcwd`, and path traversal.
- A stable `/dev/null` and `/dev/console` implementation.
- A writable `/tmp` backed by tmpfs for the first shell release.

The first writable shell image may keep the initrd read-only and use tmpfs for
temporary files. Persistent home directories and ext4 file creation are
separate storage milestones.

### 7.3 Signal layer

Before interactive Bash, implement the minimum Linux-style signal model used by
musl and Bash:

- `rt_sigaction`.
- `rt_sigprocmask`.
- `rt_sigreturn` or the architecture-equivalent signal-frame return path.
- `kill`/`tgkill` for process-directed delivery.
- Default actions for termination, stop, continue, and core/ignore cases as
  applicable to Omega.
- Safe signal-frame construction and restoration on x86_64, AArch64, and
  RISC-V.
- Signal delivery at syscall return and interrupt/preemption boundaries.
- Correct interrupted-syscall restart or `EINTR` behavior.

Start with single-threaded processes. Defer pthread-directed signal delivery
until Omega has a real thread-group model.

Bash depends on signals for Ctrl-C, traps, child status changes, termination,
and job-control notifications. It is acceptable for the first non-interactive
profile to have a narrow signal implementation, but it is not acceptable to
claim interactive Bash while signal delivery remains a stub.

### 7.4 Terminal, TTY, and PTY layer

The final interactive shell requires a terminal abstraction independent of the
physical console:

```text
UART / VGA / framebuffer input-output
                |
             TTY core
       (line discipline + termios)
                |
          terminal device FDs
                |
     session / process-group control
                |
              Bash
```

Implement in this order:

1. A kernel terminal object with input and output queues.
2. A serial TTY backend for the x86_64 reference machine.
3. Synthetic or UART-backed TTY adapters for AArch64 and RISC-V QEMU.
4. Canonical input mode, echo, erase, interrupt, and end-of-file handling.
5. Raw mode for Readline.
6. `ioctl` termios operations (`TCGETS`, `TCSETS` or the selected ABI subset).
7. `TIOCGPGRP`, `TIOCSPGRP`, and terminal size queries.
8. Controlling-terminal association with sessions.
9. PTY master/slave support for future terminal emulators.

The initial interactive acceptance target is a serial TTY. A graphical terminal
emulator and PTY-based desktop terminal are later consumers of the same TTY
contract.

### 7.5 Sessions, process groups, and job control

Implement:

- `setsid`.
- `setpgid`/`getpgid`.
- `getsid`.
- Foreground process-group tracking per terminal.
- Terminal-generated `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, and `SIGTTOU`
  behavior according to the supported termios subset.
- Child-stop and child-continue notifications via `wait4`/`waitid` policy.
- Orphaned process-group handling sufficient for shell cleanup.
- Session teardown when the controlling terminal closes.

Bash's job-control implementation expects the shell and terminal driver to
  maintain process-group ownership together. [Bash job control](https://www.gnu.org/software/bash/manual/html_node/Job-Control.html)

The first background-process milestone may support `cmd &` and `wait` without
interactive suspension. Foreground/background commands, `Ctrl-Z`, `bg`, and
`fg` require the complete process-group path.

## 8. Userspace support and utilities

Bash is not a useful system shell without a small command environment. Build
the following statically against the same musl SDK:

### Bootstrap utilities

```text
true false echo printf test [
pwd env export
cat head tail
ls mkdir rmdir rm cp mv
sleep kill
basename dirname
```

Each utility should have an explicit Omega source/build entry and an ELF
manifest. Avoid depending on Bash-specific behavior in utility test scripts.

The initial standalone implementation of `ls`, `dir`, `ln`, `pwd`, `cat`,
`mkdir`, `rm`, `rmdir`, `mv`, `echo`, `true`, `false`, `env`, and `test` is now
provided by `scripts/build_commands.sh` and validated for all three target
ISAs by `scripts/test_commands_build.sh`. The remaining bootstrap entries
(`printf`, `[`, `head`, `tail`, `cp`, `sleep`, `kill`, `basename`, and
`dirname`) remain follow-up work. This build milestone does not imply that
Bash can yet execute the full external-command set under QEMU; filesystem
mutation and complete process supervision remain hard gates. The
current-directory ABI and a simple `/bin/echo` `execve` replacement probe
have since been added and pass on all three ISAs. Filesystem mutation, signal
delivery, process supervision, broader command output/ABI conformance, and
shell-level external-command execution remain hard gates.

### Required shell-visible files

```text
/bin/bash
/bin/sh                  # policy decision: small POSIX shell or Bash link
/bin/<bootstrap utilities>
/etc/profile             # only when startup-file support is enabled
/tmp
/dev/null
/dev/console
```

The initial `/init` should select the shell through a documented boot argument
or configuration value, for example:

```text
omega.init=/bin/bash
omega.shell_mode=script|interactive
```

Do not hard-code Bash as PID 1 before process supervision and shell recovery
are available.

## 9. Phased execution plan

### B0 — Freeze the port contract

Deliverables:

- Pin Bash version and checksum.
- Record license files and source provenance.
- Create `tools/bash/OMEGA.md` and `tools/bash/patches/`.
- Decide whether `/bin/sh` initially points to Bash or a smaller POSIX shell.
- Define the initrd manifest and per-ISA artifact naming.

Exit criteria: a written manifest exists and no source is built from an
unrecorded host installation.

### B1 — Kernel prerequisite audit

Deliverables:

- A syscall inventory mapping Bash/musl requirements to Omega implementations.
- Unit tests for descriptor, VFS, memory, process, and ELF behavior.
- Explicit `ENOSYS`/`EINTR` policy for unsupported calls.
- Cross-ISA ABI tests for syscall return values and stack construction.

Exit criteria: the audit identifies no required B2 behavior that is silently
missing or accidentally provided by the host.

### B2 — Build Bash without executing it

Deliverables:

- `scripts/build_bash.sh <isa>`.
- Reproducible out-of-tree builds for all three ISAs.
- Static ELF validation.
- No dynamic interpreter, host libc, or host Readline dependency.

Exit criteria:

```text
build/bash-x86_64/bash      -> EM_X86_64
build/bash-aarch64/bash      -> EM_AARCH64
build/bash-riscv64/bash      -> EM_RISCV
```

The implementation must use the exact artifact names produced by the build
system; the examples above are illustrative and should be normalized before
the script is committed.

### B3 — Execute `bash -c`

Required behavior:

```sh
bash -c 'echo hello'
bash -c 'x=omega; test "$x" = omega'
bash -c 'for x in a b c; do echo "$x"; done'
bash -c 'if true; then echo yes; fi'
```

Required kernel work: stable `execve`, argv/envp, process exit, basic VFS,
stdio, memory growth, and enough signals for clean termination.

Exit criteria: the same test vector passes on x86_64, AArch64, and RISC-V
under their reference QEMU profiles, with no kernel fault or leaked child.

### B4 — Add scripts, redirection, and external commands

Required behavior:

```sh
bash -c 'echo hello > /tmp/out; cat /tmp/out'
bash -c 'cat < /tmp/out'
bash -c 'PATH=/bin; echo "$PATH"'
bash -c 'command -v cat'
bash -c 'env VAR=value /bin/echo "$VAR"'
```

Required kernel work: file creation/truncation, offsets, directory traversal,
PATH lookup, descriptor inheritance, and `FD_CLOEXEC`.

Exit criteria: shell scripts can invoke every bootstrap utility and clean up
their temporary files.

### B5 — Add pipelines, subshells, and background execution

Required behavior:

```sh
echo hello | cat
(echo one; echo two) | cat
echo background & wait
```

Required kernel work: working pipe EOF semantics, fork/exec descriptor setup,
child reaping, and process status propagation.

Exit criteria: pipeline tests run repeatedly without hangs, descriptor leaks,
or zombie accumulation on all three ISAs.

### B6 — Add signals and non-job-control interrupts

Required behavior:

```sh
bash -c 'trap "echo trapped" INT; ...'
bash -c 'trap "echo exited" EXIT'
```

Add a test-only mechanism to deliver a signal to the shell process. Validate
that interrupted syscalls, shell traps, and exit statuses are deterministic.

Exit criteria: signal tests pass independently of the terminal implementation.

### B7 — Implement serial TTY and interactive Bash

Required behavior:

```text
prompt appears
typed command executes
Backspace edits the line
Enter submits the line
Ctrl-C interrupts a foreground command
Ctrl-D exits at an empty prompt
```

Begin with Readline disabled and a minimal Omega line discipline if that makes
bring-up faster. The final Readline milestone comes after raw terminal mode
and termios are stable.

Exit criteria: an interactive Bash session works over the x86_64 serial console
and the equivalent synthetic/UART consoles for AArch64 and RISC-V.

### B8 — Enable job control

Required behavior:

```text
sleep 10
Ctrl-Z
jobs
bg
fg
Ctrl-C
```

Exit criteria: foreground ownership, stopped jobs, continued jobs, shell exit,
and terminal disconnect are all handled without kernel panic or unreaped
processes.

### B9 — Package and promote

Deliverables:

- Reproducible Bash artifacts and manifests for all ISAs.
- License/source bundle.
- Initrd packaging integration.
- `docs/RUNNING.md` commands for launching each shell profile.
- A stable shell compatibility statement.
- Roadmap status updates that distinguish script Bash from interactive Bash.

Exit criteria: a clean checkout can build the selected profile and boot it on
all three reference QEMU platforms.

## 10. Architecture-specific execution matrix

### x86_64

First implementation target because it has the most mature Omega boot, serial,
interrupt, and QEMU environment.

Required validation:

- `qemu-system-x86_64` serial boot with no graphical display.
- User stack and syscall return across `syscall`/`sysret`.
- Signal frame correctness through the x86_64 trap path.
- TTY input over the serial console.
- Optional VGA/framebuffer terminal later.

### AArch64

Required validation:

- `qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic`.
- `svc` syscall entry and `eret` return preserve all user registers required
  by Bash and musl.
- AArch64 signal frame and `rt_sigreturn` restore `TPIDR_EL0`/TLS correctly.
- PL011-backed or synthetic TTY input.
- Cache/MMU attributes remain correct for user stacks, pipes, and terminal
  buffers.

### RISC-V 64

Required validation:

- `qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic`.
- `ecall` syscall entry and `sret` return preserve the ABI state.
- RISC-V signal frame and `rt_sigreturn` restore `tp`/TLS correctly.
- SBI timer/trap interactions do not corrupt shell processes.
- UART or synthetic TTY input and process-group signal delivery.

No architecture may be marked complete from a host build alone. Each must
boot its own matching Bash ELF and execute the common test vectors.

## 11. Test plan

### 11.1 Host-only tests

- Source checksum and license manifest verification.
- Configure-cache reproducibility.
- Static ELF and architecture validation.
- No `PT_INTERP`, no unexpected `DT_NEEDED`, and no host library paths.
- Symbol and relocation checks.
- Bash parser and upstream test subset that does not require Linux-specific
  behavior. Bash's normal build workflow includes `configure`, `make`, and an
  optional test phase; adapt this to cross execution constraints.

### 11.2 Kernel/userspace tests

- `execve` success/failure and argv/envp propagation.
- Descriptor inheritance and close-on-exec.
- File redirection and offsets.
- Pipe blocking, EOF, and broken-writer behavior.
- Child exit, wait, signal, and trap behavior.
- TTY canonical/raw modes and ioctl results.
- Session/process-group transitions.

### 11.3 QEMU shell vectors

Every architecture uses the same vector set:

```text
S01: bash -c 'echo hello'
S02: variables, quoting, arithmetic, and command status
S03: if/for/while/case/function parsing
S04: /tmp redirection and append
S05: PATH lookup and external utility execution
S06: pipelines and subshells
S07: command substitution
S08: background command and wait
S09: traps and signal exit status
S10: interactive prompt and line editing
S11: Ctrl-C, Ctrl-D, Ctrl-Z, bg, fg, jobs
S12: clean shell exit and process-table cleanup
```

The harness should capture serial output, enforce a timeout, report the first
failed vector, and check for kernel panic, user fault, invalid ELF, leaked
processes, and descriptor exhaustion.

### 11.4 Completion matrix

| Milestone | x86_64 | AArch64 | RISC-V | Required for release |
| :--- | :---: | :---: | :---: | :---: |
| Bash builds statically | pass | pass | pass | yes |
| `bash -c` | pass | pass | pass | yes |
| External commands | pass | pass | pass | yes |
| Pipelines/redirection | pass | pass | pass | yes |
| Signals/traps | pass | pass | pass | yes |
| Interactive TTY | pass | pass | pass | yes for interactive profile |
| Readline/history | pass | pass | pass | recommended |
| Job control | pass | pass | pass | yes for interactive profile |

## 12. Risks and mitigations

| Risk | Effect | Mitigation |
| :--- | :--- | :--- |
| Bash configure executes target probes | Cross-build fails or uses host answers | Checked-in cache, explicit `ac_cv_*`, no target execution |
| Incomplete `execve` | Shell starts but external commands fail | Make B3 a hard gate before utility packaging |
| Pipe semantics are incomplete | Hangs and descriptor leaks | Kernel pipe tests with EOF/broken-writer cases |
| Signal-frame bug | Corruption appears architecture-specific | Independent signal-frame tests for each ISA |
| No TTY | Bash works only with `-c` | Separate script and interactive release profiles |
| Readline dependency growth | Build becomes difficult and license scope expands | Start with `--noediting`; add Readline later |
| Bash-specific utilities absent | Shell appears broken | Ship a minimal, tested utility set |
| RISC-V backend/runtime differences | Bash or utilities fail only on RISC-V | Common test vectors and per-ISA ELF/runtime checks |
| Initrd is read-only | Scripts cannot create temporary files | Mount tmpfs at `/tmp`; defer persistent storage |
| Bash as PID 1 | No supervision or recovery | Keep `/init` as supervisor until B9 |

## 13. Definition of done

### Script Bash release

- Bash builds reproducibly for all three ISAs.
- Bash is statically linked to the matching Omega musl SDK.
- `bash -c` and the common script vector pass on all three QEMU targets.
- External utilities, redirection, pipelines, and command substitution work.
- Signals and process cleanup are deterministic.
- Initrd packaging, licenses, and source manifests are complete.

### Interactive Bash release

Everything in the script release, plus:

- TTY input/output works on all three reference platforms.
- Canonical and raw terminal modes work.
- Ctrl-C, Ctrl-D, and Ctrl-Z behave correctly.
- Foreground/background process groups and `jobs`, `bg`, and `fg` work.
- Readline/history are either integrated with their license obligations or
  deliberately replaced by a documented Omega line editor.
- A terminal emulator can later consume the same TTY/PTY interface.

## 14. Relationship to existing documents

| Document | Required update/relationship |
| :--- | :--- |
| [`libc-integration.md`](libc-integration.md) | Bash consumes the static musl SDK; its remaining signal, directory, time, and POSIX gaps are Bash prerequisites. |
| [`ON_TARGET_COMPILER_PLAN.md`](ON_TARGET_COMPILER_PLAN.md) | TinyCC should compile and link Bash-compatible utilities after `execve` and file creation work; it is not required to bootstrap the first Bash binary. |
| [`OMEGA_SDK_PLAN.md`](OMEGA_SDK_PLAN.md) | Bash belongs to the `posix-static` profile, after the small-shell milestone. |
| [`POSIX_COMMANDS_PORTING_PLAN.md`](POSIX_COMMANDS_PORTING_PLAN.md) | Defines the standalone `/bin` command slice, its three-ISA build validation, and kernel runtime prerequisites. |
| [`ABI.md`](ABI.md) | Bash depends on stable process, descriptor, signal, ELF, and initial-stack ABI behavior. |
| [`ROADMAP.md`](ROADMAP.md) | Phase 7.D.5 should track script shell separately from interactive shell; Phase 10A.5 consumes the completed terminal/job-control work. |
| [`RUNNING.md`](RUNNING.md) | Add build and QEMU launch commands only after the scripts exist. |

This plan should be updated at every phase gate with the commit, test command,
architecture matrix, and remaining limitations. A build-only result must never
be recorded as a completed Bash port.
