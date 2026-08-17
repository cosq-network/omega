# Omega POSIX Command Porting Plan

Status (2026-08-17): initial standalone command slice implemented and
validated for x86_64, AArch64, and RISC-V 64; kernel filesystem ABI work
remains for on-target execution.

## Scope

Omega provides a separate statically linked executable for each command. The
first profile targets the Linux/coreutils-compatible behavior needed by Bash
and early system administration, while keeping the implementation small
enough to validate on all three supported ISAs:

`ls`, `dir`, `ln`, `pwd`, `cat`, `mkdir`, `rm`, `rmdir`, `mv`, `echo`, `true`,
`false`, `env`, and `test`.

`cd` is intentionally a Bash builtin: an external `/bin/cd` cannot change the
parent shell's working directory. The second implementation tranche is
planned for `cp`, `touch`, `head`, `tail`, `basename`, `dirname`, `sleep`, and
`kill`.

## Build and packaging

The commands use the per-ISA musl SDK produced by
`scripts/build_musl_sysroot.sh`. `scripts/build_commands.sh` compiles them
with the Omega linker script and packages them as individual ELF files under
`userland/commands/<arch>/bin`. `scripts/test_commands_build.sh` builds and
checks static ELF output for x86_64, AArch64, and RISC-V 64. CMake exposes the
same work through `OMEGA_BUILD_USERSPACE=ON` and
`OMEGA_BUILD_COMMANDS=ON`.

The validation command is:

```sh
bash scripts/test_commands_build.sh
```

It checks all 14 expected executables, static linkage, and the architecture
reported by `file` for each generated ELF.

The generated artifacts are intended for both the initrd profile and the
future writable-root profile. The initrd image should initially contain the
commands under `/bin`; later image assembly can install the same manifest into
the persistent root filesystem.

## Kernel prerequisites

The source port is complete only when the following kernel interfaces are
available to userland:

- `open`, `read`, `write`, `close`, `fstat`, `lseek`, `getdents64`, and
  `fcntl` for regular files and directories;
- `chdir` and `getcwd` for `pwd` and Bash directory changes;
- `mkdir`, `unlink`, `rmdir`, and `rename` for the mutation commands;
- `link`, `symlink`, `readlink`, and `lstat` for `ln`, `ls`, and safe removal;
- `execve`, `wait4`, `dup2`, `pipe2`, and environment handling for command
  execution from Bash;
- correct relative-path resolution using a per-process current directory;
- permission, ownership, directory-link-count, and timestamp updates;
- initrd read-only semantics plus writable `/tmp` tmpfs semantics;
- persistent-root backing once the ext4 integration is enabled.

The current kernel has a useful read/open/getdents foundation, but mutation,
link, current-directory, and complete process-exec semantics are still being
completed. The command binaries therefore validate the port and link ABI now;
runtime QEMU tests should be added as each syscall group lands.

## Execution milestones

1. Build and inspect all static command artifacts for every ISA.
2. Add a minimal `/bin` initrd manifest and run `true`, `false`, `echo`, `cat`,
   and `env` through `execve`.
3. Complete current-directory and directory mutation syscalls; exercise
   `pwd`, `mkdir`, `rmdir`, and Bash's `cd`.
4. Complete unlink/rename/link/symlink operations and exercise `rm`, `mv`,
   `ln`, and recursive `ls` on tmpfs.
5. Add persistent-root tests, permission/error-path tests, and command-level
   conformance cases.
6. Implement the second tranche and publish one combined Bash plus `/bin`
   initrd for x86_64, AArch64, and RISC-V 64.
