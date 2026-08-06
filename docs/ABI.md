# Omega Kernel: Application Binary Interface (ABI) Specification

## Executive Summary
This specification defines the formal Application Binary Interface (ABI) of the **Omega** kernel. It describes the memory layout, linkage conventions, system call calling conventions, privilege boundaries, and file descriptor semantics across all supported 64-bit architectures: **x86_64 (AMD64)**, **AArch64 (ARM64)**, and **RISC-V 64 (`rv64gc`)**.

---

## 1. System Call Architecture & Calling Conventions

Omega system calls utilize dedicated architectural trap instructions to transition from Userland (Ring 3 / EL0 / U-Mode) to Kernel Mode (Ring 0 / EL1 / S-Mode).

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

| Syscall Number | Symbolic Name | Prototype Definition | Description |
| :---: | :--- | :--- | :--- |
| `1` | `SYS_YIELD` | `int sys_yield(void)` | Yield CPU execution slice to next ready thread |
| `2` | `SYS_WRITE` | `long sys_write(int fd, const char* buf, size_t count)` | Write buffer bytes to specified file descriptor |
| `3` | `SYS_EXIT` | `void sys_exit(int status)` | Terminate current user thread with exit code |
| `4` | `SYS_OPEN` | `int sys_open(const char* path, int flags)` | Open file path and return process file descriptor |
| `5` | `SYS_READ` | `long sys_read(int fd, char* buf, size_t count)` | Read bytes from file descriptor into user buffer |
| `6` | `SYS_CLOSE` | `int sys_close(int fd)` | Close active process file descriptor |
| `7` | `SYS_FORK` | `int sys_fork(void)` | Duplicate executing process address space and thread |
| `8` | `SYS_EXECVE` | `int sys_execve(const char* path, char** argv, char** envp)` | Replace process image with new 64-bit ELF executable |

---

## 3. Standard File Descriptor Index Allocation

Per-process file descriptor tables (`fd_table[16]`) allocate standard I/O Streams upon process initialization:

- **`0`**: `STDIN_FILENO` (Standard Input stream)
- **`1`**: `STDOUT_FILENO` (Standard Output serial console)
- **`2`**: `STDERR_FILENO` (Standard Error serial console)

---

## 4. Virtual Address Space Layout & Privilege Boundaries

Omega enforces strict privilege separation between Kernel Space and Userland Address Space:

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
|  - ELF BSS / Data Segments                            |
|  - ELF Code Segment (.text - Base 0x400000)           |
|                                                       |
+-------------------------------------------------------+ 0x0000000000000000
```

---

## 5. Primitive Data Types & Alignment Specifications

| Type | Bit Width | Byte Alignment | Architectural Representation |
| :--- | :---: | :---: | :--- |
| `char` | 8-bit | 1-byte | Signed 8-bit Integer |
| `short` | 16-bit | 2-byte | Signed 16-bit Integer |
| `int` | 32-bit | 4-byte | Signed 32-bit Integer |
| `long` | 64-bit | 8-byte | Signed 64-bit Integer |
| `long long` | 64-bit | 8-byte | Signed 64-bit Integer |
| `pointer` (`void*`) | 64-bit | 8-byte | Unsigned 64-bit Virtual Address |
| `size_t` | 64-bit | 8-byte | Unsigned 64-bit Byte Count |
