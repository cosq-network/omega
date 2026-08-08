# Omega Kernel Implementation Roadmap & Milestone Matrix

## Overview
This document outlines the multi-phase implementation roadmap for **Omega**—a freestanding, cross-platform microkernel core written in C++20. Omega cross-compiles natively on macOS (Apple Silicon M1/M2/M3) using Clang and LLVM (`ld.lld`) for **x86_64**, **AArch64**, and **RISC-V 64 (`rv64gc`)** architectures.

Phases **1–6** cover the research-kernel foundation (completed). Phases **7–12** describe the realistic path from QEMU-proven subsystems to a **production-grade operating system** suitable for laptops, desktops, tablets, and phones.

### Architecture maturity baseline

Omega currently has a functional bring-up baseline on all three target ISAs,
but the architecture implementations are not yet equivalent to Linux-class
platform support. The roadmap therefore treats each ISA as an explicit
platform track rather than assuming that a driver or subsystem is portable
after it works on one architecture.

| Architecture | Current Omega baseline | Main gap to close | Near-term priority |
| :--- | :--- | :--- | :---: |
| **x86_64** | Long-mode boot, PML4 paging, GDT/IDT, PCI, VGA/Bochs VBE, serial, early VirtIO, QEMU `q35`/`pc` | ACPI, APIC/x2APIC, timers, SMP, UEFI/GOP, MSI/MSI-X, IOMMU, mature PCIe/NVMe/USB/network drivers | **P0** |
| **AArch64** | EL transition, vectors, UART, GIC foundation, FDT, SimpleFb, QEMU `virt`, experimental VirtIO-MMIO | PSCI, GICv3, generic timer, SMP, cache/MMU attributes, ACPI/UEFI, SMMU, board/SoC drivers | **P0** |
| **RISC-V 64** | OpenSBI S-mode entry, Sv39, `stvec`, PLIC foundation, UART, FDT, SimpleFb, QEMU `virt` | SBI HSM/CPU management, timer/IPI, APLIC/IMSIC evolution, SMP, Sv48, PMP, vector support, board drivers | **P1** |

The target is not to reproduce Linux’s full hardware matrix immediately. The
first production-quality milestone is a documented reference platform for each
ISA with reliable boot, memory isolation, interrupts, timers, SMP, storage,
networking, display, input, recovery, and a stable userspace ABI.

---

## 🚦 Phase Completion Matrix (Phases 1–6)

| Phase / Milestone | Status | Description | Key Subsystems | Verification |
| :--- | :---: | :--- | :--- | :---: |
| **Phase 1: Foundation & Toolchains** | `COMPLETED` | Cross-compilation & early UART serial logging | Clang, CMake toolchains, COM1 & PL011 UART | QEMU Serial Console |
| **Phase 2A: x86_64 Long Mode & Boot** | `COMPLETED` | 64-bit Long Mode identity paging & bootloader note | PML4, PAE, Xen PVH ELF note (`.xen_note`) | QEMU `-kernel` |
| **Phase 2B: AArch64 Exception Levels** | `COMPLETED` | EL2 to EL1 drop & vector base register configuration | `CurrentEL`, `VBAR_EL1`, `SP_EL1` stack | QEMU `-M virt` |
| **Phase 2C: RISC-V 64 S-Mode & Traps** | `COMPLETED` | Supervisor Mode (S-mode) boot & trap vector setup | `satp` (Sv39), `stvec`, OpenSBI console `ecall` | QEMU `-M virt -bios default` |
| **Phase 3A: Physical Memory (PMM)** | `COMPLETED` | 4KiB Bitmap frame allocator | `alloc_frame` / `free_frame` | Physical Bitmap Matrix |
| **Phase 3B: Virtual Memory (VMM)** | `COMPLETED` | Architectural page table mapping engine | `CR3` (x86), `TTBR0_EL1` (ARM), `satp` (RISC-V) | `vmm_map_page` |
| **Phase 3C: Dynamic Kernel Heap** | `COMPLETED` | Free-list block header allocator | `kmalloc` / `kfree` with block coalescing | Static Heap Buffer |
| **Phase 3D: Interrupt Drivers** | `COMPLETED` | Hardware interrupt gate setup | 256-entry IDT (x86), VBAR (ARM), STVEC/PLIC (RISC-V) | Hardware Traps |
| **Phase 4A: Preemptive Multi-threading**| `COMPLETED` | Round-robin thread scheduler | Thread Control Blocks (TCB), stack allocation | Cooperative Yields |
| **Phase 4B: System Call ABI Dispatcher**| `COMPLETED` | Formal System Call ABI dispatcher (`docs/ABI.md`) | `SYS_YIELD`, `SYS_WRITE`, `SYS_EXIT` | Syscall Dispatcher |
| **Phase 5A: Virtual Filesystem (VFS)** | `COMPLETED` | VFS node tree & root directory mount | POSIX node operations, `/` mount | `vfs::open("/")` |
| **Phase 5B: RAM Disk Initrd** | `COMPLETED` | Initial RAM disk memory file driver | Memory file header reader | `initrd::init()` |
| **Phase 5.1: Userland Privilege Mode**| `COMPLETED` | Ring 3 / EL0 / U-Mode privilege manager | Userland stack setup & mode jump | `enter_userland()` |
| **Phase 5.2: ELF 64-bit Binary Parser**| `COMPLETED` | Executable binary header & segment parser | `Elf64Header` & `PT_LOAD` loader | `ElfLoader::load()` |
| **Phase 5.3: POSIX Syscall Expansion** | `COMPLETED` | Process file descriptor table & core syscalls | `sys_open`, `sys_read`, `sys_close`, `sys_fork`, `sys_execve` | `fd_table[16]` |
| **Phase 5.4: PCI Bus Scanner** | `COMPLETED` | PCI bus configuration space reader | I/O Ports `0xCF8`/`0xCFC` scanning | Device Vendor Scan |
| **Phase 5.5: VirtIO Network Stack** | `COMPLETED` | VirtIO-Net packet driver & TCP/IP stack | L2 Ethernet, L3 IPv4, L4 UDP/TCP headers | Frame RX Reader |
| **Phase 6A: Bootable Disk Generator** | `COMPLETED` | UEFI/GPT multi-format disk generator | RAW, QCOW2, VMDK, VDI with FAT32 payloads | `create_bootable_disk.sh` |
| **Phase 6B: Firmware Compatibility** | `COMPLETED` | U-Boot (`bootefi`/`booti`) & Coreboot (EDK2/GRUB) | Embedded `/EFI/BOOT/` & `/boot/omega.elf` | `docs/FIRMWARE_BOOT.md` |
| **Phase 6C: OVD Emulator & GUI** | `HARDENING` | Python-only Omega Virtual Device Manager with a built-in Tkinter GUI; multi-architecture display, storage, networking, artifact, and lifecycle tooling | Schema validation, predefined profiles, ext4 artifact/digest checks, process identity and process-group lifecycle safety, transactional archive import, safe paths, storage/network profiles, snapshots, import/export, dry-run, asynchronous machine discovery, integrated VNC, and GUI diagnostics | `emulator/test_ovd_unit.py`, `emulator/test_profile_catalog.py`, `emulator/test_profile_ext4_integration.py`, `emulator/test_vnc.py`, `emulator/test_gui_module.py`, `scripts/test_scripts_unit.py` |
| **Phase 6D: Containerization & CI/CD** | `COMPLETED` | Alpine Dockerfile, DevContainers, GitHub Actions | DevContainers, Codespaces, GitHub Actions CI/CD incl. `test_display.sh` | `.github/workflows/ci.yml` |

---

## 🎯 Production Vision

Omega's long-term goal is to evolve from a research microkernel into a **production operating system** deployable on:

| Form Factor | Primary ISA | Target Use |
| :--- | :--- | :--- |
| **Laptops & Desktops** | x86_64, AArch64 | Developer and general-purpose daily-driver computing |
| **Tablets** | AArch64 | Touch-first productivity, media, and sideloaded apps |
| **Phones** | AArch64 | Mobile telephony, connectivity, and consumer app ecosystem |

**Recommended rollout order:** Laptops/Desktops → Tablets → Phones. Phones require the most driver, regulatory, and ecosystem work; laptops share much of the same AArch64/x86_64 hardware stack as tablets with fewer carrier and certification hurdles.

---

## 📋 Phase 0: Product & Architecture Decisions (Prerequisite)

These decisions must be locked before large-scale production investment. They are tracked here as **Phase 0** because they gate every subsequent phase.

| Decision | Options | Recommendation | Impact |
| :--- | :--- | :--- | :--- |
| **Kernel model** | Pure microkernel vs hybrid | Hybrid microkernel (drivers in userspace, performance-critical paths optimized) | Driver IPC overhead, security boundary, development velocity |
| **Primary ISA for v1** | x86_64, AArch64, RISC-V | **AArch64** (mobile/tablet) + **x86_64** (desktop/laptop); defer RISC-V until hardware partners exist | Toolchain focus, driver porting effort |
| **App compatibility model** | Custom SDK, Linux ABI, Android (ART) | Custom SDK v1 with optional Linux ABI subset for developer tooling | Defines 50%+ of userland and ecosystem work |
| **Distribution model** | Open community vs OEM-only | Open reference images + OEM partnership track for phones | Legal, signing keys, update infrastructure |
| **Security baseline** | Capabilities, SELinux-style MAC, sandbox-only | Capability-based IPC + per-app sandbox with explicit permission prompts | Mobile readiness, app store trust model |

**Exit criteria:** Written architecture decision records (ADRs) for each row; frozen v1 target platforms (reference hardware list).

---

## 🗺️ Phase 7: QEMU Parity & Kernel Patterns (Near-Term)

Phase 7 completes the patterns started in Phases 1–6 inside QEMU. These subsystems are **reference implementations**, not production drivers—they establish APIs and behavior that real hardware drivers will implement later.

| Milestone | Status | Description | Key Deliverables | Verification |
| :--- | :---: | :--- | :--- | :---: |
| **Phase 7.1: Storage Core & VirtIO-Block** | `IN PROGRESS` | Common storage API, device graph, DMA foundation, and guarded VirtIO-Block reference driver | Request validation, lifecycle binding, MMIO/legacy PCI VirtIO, partition metadata, controlled writes, protocol-driver expansion | Host unit tests, x86_64 QEMU runtime completion, profile-backed ext4 tests, and cross-architecture builds |
| **Phase 7.2: System Display Module (Standard VGA)** | `COMPLETED` | x86_64 VGA text mode, Bochs VBE linear FB, dual serial+display console | `hal::Display`, Bochs DISPI 1024×768×32, 8×16 font, Multiboot2 FB tag, `kprintf` mirroring | `scripts/test_display.sh`, CI, OVD `--gpu` |
| **Phase 7.2b: AArch64 & RISC-V Display** | `IN PROGRESS` | SimpleFb (DT), shared FDT parser, portable framebuffer console, and guarded VirtIO-GPU MMIO foundation | FDT walker, DT pointer handoff, `SimpleFb` HALs, identity-map VMM bring-up, VirtIO-GPU protocol/queue scaffold, serial fallback | `scripts/test_display_aarch64.sh`, QEMU AArch64/RISC-V smoke tests |
| **Phase 7.3: SMP Multi-Core** | `PLANNED` | Symmetric multiprocessing across all cores | APIC ICR (x86), PSCI (AArch64), OpenSBI IPI (RISC-V); per-CPU run queues | Multi-core boot log, parallel thread execution |
| **Phase 7.4: Timer-Driven Preemption** | `PLANNED` | Replace cooperative yield with hardware timer preemption | Periodic tick interrupt, preemptive context switch | Latency benchmark under load |
| **Phase 7.5: Per-Process Address Spaces** | `PLANNED` | Isolated virtual address space per process | Separate page tables per process, kernel/user boundary enforcement | Two processes with distinct mappings |
| **Phase 7.6: IPC Foundation** | `PLANNED` | Inter-process communication for microkernel services | Message passing, capability tokens, shared-memory grants | Driver server ↔ client round-trip |

**Exit criteria:** Omega boots in QEMU with block storage, **graphical console on x86_64 (done)**, SMP, preemptive scheduling, process isolation, and a userspace driver server communicating over IPC.

### Phase 7 Architecture Tracks

These tracks make the shared kernel services usable on real multicore
platforms. Each item requires a host unit test where possible, a QEMU
integration test, and a documented failure/recovery path before being marked
complete.

#### 7.A x86_64 platform maturity — P0

| Milestone | Status | Deliverable | Verification |
| :--- | :---: | :--- | :--- |
| **7.A.1 ACPI and firmware handoff** | `PLANNED` | Parse RSDP/RSDT/XSDT, MADT, FADT, HPET, MCFG, and memory maps from UEFI/BIOS handoff | QEMU `q35` ACPI table dump and malformed-table tests |
| **7.A.2 APIC and MSI interrupt routing** | `PLANNED` | Local APIC, I/O APIC, x2APIC capability detection, MSI/MSI-X routing, interrupt affinity | Timer, PCI, and cross-core interrupt tests |
| **7.A.3 Timers and timekeeping** | `PLANNED` | TSC calibration, HPET/PIT fallback, monotonic clock, timer queues, clocksource selection | Drift, timeout, and preemption tests |
| **7.A.4 SMP and topology** | `PLANNED` | AP startup, per-CPU data, CPU topology, TLB shootdown, reschedule IPIs | QEMU 2/4/8-vCPU boot and parallel scheduler tests |
| **7.A.5 UEFI GOP and PCIe** | `PLANNED` | GOP framebuffer handoff, ECAM/MCFG, BAR allocation, bus mastering, hotplug-safe enumeration | OVMF/Q35 framebuffer and PCIe enumeration tests |
| **7.A.6 x86_64 memory hardening** | `PLANNED` | NX/W^X, PAT/cache attributes, optional five-level paging, guard pages, KASLR prerequisites | Page-permission and fault-injection tests |

#### 7.B AArch64 platform maturity — P0

| Milestone | Status | Deliverable | Verification |
| :--- | :---: | :--- | :--- |
| **7.B.1 PSCI and boot protocol** | `PLANNED` | PSCI CPU_ON/OFF, reset, system suspend hooks, UEFI/Device Tree handoff normalization | QEMU `virt` PSCI and reboot tests |
| **7.B.2 GICv2/GICv3 interrupt framework** | `PARTIAL` | Complete distributor/redistributor setup, priority/routing, SGI/PPI/SPI handling, MSI integration | IRQ storm, timer, and multi-vCPU tests |
| **7.B.3 ARM generic timer** | `PLANNED` | CNTFRQ/CNTPCT calibration, virtual/physical timer selection, tickless deadline timers | Clock drift and preemption tests |
| **7.B.4 AArch64 SMP and cache discipline** | `PLANNED` | Per-CPU data, barriers, cache maintenance, TLB invalidation, coherency-safe DMA | QEMU multi-vCPU and DMA consistency tests |
| **7.B.5 MMU and memory attributes** | `PLANNED` | Permissioned page tables, device-vs-normal memory, shareability, execute-never, 4K/16K page policy | Fault, mapping, and framebuffer attribute tests |
| **7.B.6 UEFI, ACPI, and board abstraction** | `PLANNED` | ACPI/DT selection, generic board layer, PSCI/GIC/timer resource discovery, SMMU boundary | QEMU `virt`, Raspberry Pi, and first reference-board boot tests |

#### 7.C RISC-V 64 platform maturity — P1

| Milestone | Status | Deliverable | Verification |
| :--- | :---: | :--- | :--- |
| **7.C.1 SBI platform services** | `PARTIAL` | SBI version/features, HSM CPU control, timer, IPI, reset, and vendor-extension policy | OpenSBI version matrix and service-failure tests |
| **7.C.2 Timer and IPI abstraction** | `PLANNED` | ACLINT/CLINT discovery, SBI timer fallback, software interrupts, per-CPU deadlines | Timer/preemption and cross-core IPI tests |
| **7.C.3 PLIC to AIA evolution** | `PLANNED` | PLIC baseline hardening, APLIC/IMSIC capability model, interrupt domains, MSI delivery | QEMU interrupt-controller matrix |
| **7.C.4 RISC-V SMP and memory ordering** | `PLANNED` | Hart discovery/startup, per-hart state, fences, TLB shootdown, scheduler IPIs | QEMU multi-hart and concurrency tests |
| **7.C.5 Virtual-memory and protection expansion** | `PLANNED` | Sv39 hardening, optional Sv48, PMP policy, execute permissions, page-fault path | Mapping, isolation, and PMP fault tests |
| **7.C.6 ISA feature and board capability model** | `PLANNED` | DT/ACPI probing, ISA extension discovery, vector-state policy, board resource drivers | RV64GC/vector capability tests and reference-board boot |

#### 7.D Shared architecture-neutral kernel services — P0

| Milestone | Status | Deliverable | Verification |
| :--- | :---: | :--- | :--- |
| **7.D.1 Timer-independent scheduler core** | `PLANNED` | Architecture-neutral scheduler clock, priorities, accounting, and preemption hooks | Identical scheduler tests on all ISAs |
| **7.D.2 Per-process address spaces** | `PLANNED` | Isolated page tables, syscall validation, copy-on-write prerequisites, guard regions | Two-process isolation and fault tests |
| **7.D.3 Capability-based IPC** | `PLANNED` | Typed messages, endpoint ownership, capability transfer, shared-memory grants, timeout/cancellation | Driver-server round-trip and abuse tests |
| **7.D.4 Portable driver contracts** | `PLANNED` | Common interrupt, DMA, MMIO, block, net, display, and input interfaces | Synthetic drivers plus all-ISA contract tests |
| **7.D.5 Userspace bootstrap** | `PLANNED` | Init process, process lifecycle, shell over serial, libc bootstrap, stable ABI versioning | End-to-end ELF/init/syscall test |

**Architecture-track exit criteria:** each target boots on its reference QEMU
machine with validated interrupt delivery, timer-driven preemption, SMP,
isolated processes, a working storage path, network path, display or serial
console, and a documented userspace-driver boundary. No architecture is
considered production-ready solely because it compiles or reaches
`kernel_main()`.

### Storage Architecture

The storage roadmap is defined in [`docs/STORAGE_ARCHITECTURE_PLAN.md`](STORAGE_ARCHITECTURE_PLAN.md). Storage is organized by transport and protocol rather than by physical media label: NVMe covers PCIe SSDs, AHCI/ATA covers SATA SSDs and HDDs, SDHCI covers both SD and microSD, and USB Mass Storage covers USB flash and USB optical devices. The current implementation is a kernel-resident foundation with a writable synthetic backend and guarded VirtIO-Block path; future hardware drivers and the userspace `storaged` migration boundary remain planned.

| Storage milestone | Status | Scope |
| :--- | :---: | :--- |
| **7.1a Storage core and DMA** | `PARTIAL` | Common request/status API, device registry and lifecycle states, validation, DMA mapping foundation, and synthetic backend validated on all ISAs; asynchronous completion, cancellation, and production bounce-buffer paths remain |
| **7.1b Partitions and read-only filesystems** | `PARTIAL` | GPT/MBR metadata parser added; FAT32, ext4, ISO9660, and UDF mounting remain |
| **7.1c VirtIO-Block** | `PARTIAL` | x86_64 transitional VirtIO-PCI discovery, legacy queue geometry, feature negotiation, read/write/flush completion, and QEMU runtime verification; VirtIO-MMIO AArch64/RISC-V completion and modern PCI transport remain experimental |
| **7.1d NVMe** | `PLANNED` | NVMe controller, namespaces, PRP reads, reset recovery |
| **7.1e AHCI/SATA/ATAPI** | `PLANNED` | SATA SSD/HDD reads and CD/DVD packet reads |
| **7.1f SDHCI and USB Mass Storage** | `PLANNED` | SD/microSD, xHCI, BOT/UAS, USB flash and optical media |
| **7.1g Controlled writes** | `PARTIAL` | Common writable/read-only policy, FUA/barrier flags, flush dispatch, and synthetic write tests; filesystem/hardware writes remain |
| **7.1h Storage emulator and test harness** | `COMPLETED` | QEMU storage profiles in OVD and x86 launcher; script/OVD unit tests, dry-run command inspection, and all-ISA storage integration tests |
| **7.1i Emulator manageability and UX** | `COMPLETED` | OVD schema validation, configurable roots, lifecycle state, daemon logs, QMP monitor support, networking, initrd, ephemeral mode, snapshots, clone/import/export, predefined profile catalog, ext4 artifact refresh, and profile-aware GUI controls |

### Current Phase 7.1 implementation boundary

Implemented and verified:

- Shared storage types, device registration, lifecycle state transitions,
  request validation, read/write/flush dispatch, and read-only protection.
- DMA allocation/mapping foundation and a synthetic writable memory block used
  for deterministic tests.
- GPT/MBR metadata parsing with host-side unit coverage.
- Guarded VirtIO-Block discovery and request encoding, including x86_64
  transitional PCI read/write/flush completion under QEMU; AArch64/RISC-V
  VirtIO-MMIO runtime completion remains opt-in pending device-specific tests.
- Profile-backed native OVD artifacts resolve the current kernel and generate
  a matching ext4 raw image; dry-run and real-image integration tests validate
  manifest, copy, and launcher wiring when `mke2fs`/`mkfs.ext4` is installed.
- QEMU/OVD storage transport wiring and dry-run inspection for the supported
  emulator profiles.
- Unit and integration coverage across x86_64, AArch64, and RISC-V.
- Emulator unit, GUI contract, and three-architecture lifecycle coverage,
  including daemon state, fake-tool dispatch, and cleanup behavior.

Not yet implemented:

- NVMe, AHCI/SATA, ATAPI, SDHCI, xHCI/USB MSC, and physical-device hotplug
  drivers.
- FAT32, ext4, ISO9660, and UDF mounting or filesystem writeback.
- Production hardware recovery, power-loss testing, multiqueue performance,
  and userspace `storaged` migration over IPC.

### Phase 7.2 Delivered (x86_64 Standard VGA)

The **System Display Module (SDM)** is implemented for PC-class VGA hardware in QEMU and firmware handoff paths. See `docs/VGA_DISPLAY_PLAN.md` for the full specification.

| Sub-milestone | Status | Deliverable |
| :--- | :---: | :--- |
| **7.2a VGA Text Mode** | `COMPLETED` | 80×25 text buffer at `0xB8000`, CRTC cursor, vsync scroll |
| **7.2b Bochs VBE Linear FB** | `COMPLETED` | PCI probe (`1234:1111` / `1AF4:1111`), DISPI mode set, 8×16 font, graphical console grid |
| **7.2c Bootloader FB Handoff** | `COMPLETED` | Multiboot2 framebuffer request tag in `boot.s`, tag type 8 parser |
| **7.2d Hardening & CI** | `COMPLETED` | In-kernel self-tests, `scripts/test_display.sh`, OVD `-vga std` integration |

**Deferred from Phase 7.2:** VirtIO-GPU (→ 7.2b), VMware SVGA, UEFI GOP EFI stub, full ANSI escape subset, SMP display spinlock.

### Phase 7.2b In Progress (AArch64 & RISC-V 64 Display)

Extends the SDM to non-x86 targets using **framebuffer-first** backends (no VGA text mode). The portable console, font, and `kprintf` mirroring layers from Phase 7.2 are reused unchanged. See `docs/DISPLAY_AARCH64_RISCV_PLAN.md` for the full specification.

| Sub-milestone | Status | Deliverable |
| :--- | :---: | :--- |
| **7.2b.1 FDT Infrastructure** | `COMPLETED` | Shared `kernel/sys/fdt.cpp` walker; DT pointer capture through AArch64 `x0` and RISC-V OpenSBI `a1` |
| **7.2b.2 SimpleFb (AArch64)** | `COMPLETED*` | SimpleFb backend, format metadata, shared console routing, safe serial fallback, and pixel self-test path |
| **7.2b.3 SimpleFb (RISC-V)** | `COMPLETED*` | Correct OpenSBI entry placement, PMM bootstrap storage, display HAL, and portable console integration |
| **7.2b.4 VirtIO-GPU** | `IN PROGRESS` | Shared GPU protocol and MMIO discovery scaffold; activation remains opt-in pending verified queue completion |
| **7.2b.5 CI & Hardening** | `IN PROGRESS` | `scripts/test_display_aarch64.sh`, integration assertions, pixel-format dispatch; real framebuffer CI pending |

\* QEMU's default `virt` configuration in the current environment does not expose a DT `simple-framebuffer` node, so the validated path uses serial fallback; a real DT framebuffer activates the same SimpleFb backend.

**Current state:** AArch64 and RISC-V use real `display.cpp` HALs rather than stubs. Shared display initialization runs on all architectures after PMM/VMM, and RISC-V reaches `kernel_main()` under OpenSBI. The current framebuffer mapping is an identity-map QEMU bring-up path; a full architecture-specific VMM is still required for arbitrary physical framebuffer addresses. VirtIO-MMIO discovery, feature negotiation, queue layout, and the 2D command sequence now exist in `kernel/sys/virtio_gpu.cpp`, but activation is guarded by `ENABLE_EXPERIMENTAL_VIRTIO_GPU` until queue completion is validated without risking early boot. PCI ECAM transport and userspace display capabilities remain next milestones.

---

## 🔒 Phase 8: Kernel Hardening (Production Safety)

Required before running on untrusted workloads or real user hardware.

### 8.1 Memory & Process Model

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **8.1.1 Copy-on-Write** | COW for `fork`, shared libraries, and `mmap` | Fork + write isolation test |
| **8.1.2 Demand Paging** | Page faults load data on first access | Fault-driven page-in test |
| **8.1.3 `mmap` / `munmap` / `mprotect`** | Full virtual memory API for userland | libc mmap test suite |
| **8.1.4 W^X Enforcement** | Writable and executable pages are mutually exclusive | Attempt execute-from-writable page → fault |
| **8.1.5 ASLR** | Randomized base addresses for executables, heap, stack | Address layout entropy check |
| **8.1.6 OOM Handling** | Memory pressure callbacks and process termination policy | Allocation failure under pressure |

### 8.2 Concurrency & Scheduling

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **8.2.1 Per-CPU Run Queues** | Scheduler scales linearly with core count | Benchmark on 4+ cores |
| **8.2.2 IPI Infrastructure** | Inter-processor interrupts for TLB shootdown, reschedule | Cross-core TLB invalidation test |
| **8.2.3 Kernel Synchronization** | Spinlocks, mutexes, read-copy-update paths | Stress test under contention |
| **8.2.4 Priority Scheduling** | Real-time and normal priority classes | RT thread latency measurement |

### 8.3 Security Model

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **8.3.1 Capability System** | Fine-grained rights for IPC endpoints and device access | Unauthorized access rejected |
| **8.3.2 Secure Boot Chain** | Signed kernel, verified init, measured boot | Boot with tampered image → refuse |
| **8.3.3 Key Storage (TEE)** | Integration with ARM TrustZone / platform secure enclave | Seal/unseal device key test |
| **8.3.4 Syscall Filtering** | Per-process syscall allowlists for sandboxed apps | Blocked syscall returns `EPERM` |
| **8.3.5 Stack Canaries & Guard Pages** | Stack overflow detection | Deliberate overflow → fault |

### 8.4 Reliability & Observability

| Milestone | Description | Verification |
| :--- | :--- | :--- |
| **8.4.1 Structured Logging** | Ring buffer, log levels, subsystem tags | Log capture under boot |
| **8.4.2 Panic Recovery & Watchdog** | Watchdog timer, graceful degradation on fatal error | Watchdog reset test |
| **8.4.3 Crash Dumps** | Kernel and userspace minidump to storage | Post-crash dump readable |
| **8.4.4 Tracepoints & Profiling** | Static trace events, sampling profiler hooks | Profile hot path in scheduler |
| **8.4.5 Fuzzing Infrastructure** | Continuous syscall, VFS, and network fuzzing in CI | Fuzzer finds zero crashes in 24h run |

**Exit criteria:** Kernel passes external security review checklist; fuzzing CI green; two isolated processes cannot access each other's memory or capabilities.

---

### Communications Architecture Plan

The communications integration strategy is documented in
[`docs/COMMUNICATIONS_INTEGRATION_PLAN.md`](COMMUNICATIONS_INTEGRATION_PLAN.md).
It defines the generic device graph, event/completion and DMA contracts, and
the phased path for native serial, VirtIO/e1000 Ethernet, xHCI USB 2/3,
USB Type-C/PD, and synthetic then hardware-backed 2.4/5 GHz Wi-Fi. The plan
also aligns USB serial/network/storage classes with the existing abstractions
and reserves userspace `seriald`, `usbhostd`, `netd`, `wifid`, and `typed`
services for the IPC migration milestone.

| Communications milestone | Status | Scope |
| --- | --- | --- |
| **C0 Foundation** | `PLANNED` | Device graph, capabilities, events, completion, and DMA contract |
| **C1 Serial** | `PLANNED` | Native UART lifecycle and USB CDC ACM convergence |
| **C2 Ethernet** | `PLANNED` | Generic `netdev`, VirtIO-net, e1000, PHY/link management |
| **C3 USB core** | `PLANNED` | xHCI USB 2/3, enumeration, URBs, hubs, hotplug |
| **C4 Type-C and USB classes** | `PLANNED` | CDC, HID, mass storage, Type-C roles, TCPC/PD foundation |
| **C5 Wi-Fi** | `PLANNED` | Synthetic radio, 2.4/5 GHz capabilities, regulatory/security boundary |
| **C6 IPC migration** | `PLANNED` | `seriald`, `usbhostd`, `netd`, `wifid`, `typed`, and `devd` |

### Pointing Devices Architecture Plan

The pointing-device strategy is documented in
[`docs/POINTING_DEVICES_INTEGRATION_PLAN.md`](POINTING_DEVICES_INTEGRATION_PLAN.md).
It defines the common event/device model and staged support for USB/PS2
mice, USB and I²C-HID touchpads, embedded I²C/SPI touchscreens, multi-touch,
calibration, gestures, OVD input profiles, and the future userspace `inputd`
service across x86_64, AArch64, and RISC-V.

| Pointing-device milestone | Status | Scope |
| --- | --- | --- |
| **P0 Input foundation** | `PLANNED` | Device graph, event envelope, queues, lifecycle, and capabilities |
| **P1 HID core** | `PLANNED` | Descriptor parser, report decoder, and USB HID binding |
| **P2 Mouse** | `PLANNED` | USB/PS2 mouse, buttons, wheel, recovery, and hotplug |
| **P3 Touch core** | `PLANNED` | Contact frames, slots, normalization, and calibration |
| **P4 Touchpad** | `PLANNED` | USB HID/I²C-HID touchpads, gestures, and palm policy |
| **P5 Touchscreen** | `PLANNED` | I²C/SPI controllers, GPIO IRQ, reset, and panel transforms |
| **P6 IPC/inputd** | `PLANNED` | Userspace event service, permissions, profiles, and consumers |

### Real Hardware Validation Matrix

The hardware-validation strategy is documented in
[`docs/REAL_HARDWARE_VALIDATION_MATRIX.md`](REAL_HARDWARE_VALIDATION_MATRIX.md).
It prioritizes ODROID-H4/Intel NUC8 for x86_64, Raspberry Pi 5/Orange Pi 5
for AArch64, and VisionFive 2/BPI-F3 for RISC-V, then expands to PinePhone Pro,
PineTab2, unlockable Fairphone devices, Framework laptops, and refurbished
business desktops. The matrix defines bring-up, recovery, display, storage,
communications, pointing-device, power, and support-classification gates.

| Hardware milestone | Status | Scope |
| --- | --- | --- |
| **H0 QEMU** | `COMPLETED` | Cross-architecture deterministic regression baseline |
| **H1 x86_64** | `PLANNED` | UEFI/GOP, serial, PCI, NVMe, USB, Ethernet, and display |
| **H2 AArch64** | `PLANNED` | FDT, UART, SD, USB, Ethernet, and framebuffer |
| **H3 RISC-V** | `PLANNED` | OpenSBI, FDT, PLIC, UART, storage, Ethernet, and HDMI |
| **H4 High-speed boards** | `PLANNED` | Orange Pi 5/BPI-F3 PCIe, Wi-Fi, USB, Type-C, and 1080p display |
| **H5 Mobile/tablet** | `PLANNED` | PineTab2/PinePhone Pro touch, battery, suspend, and mobile display |
| **H6 Laptop/desktop** | `PLANNED` | Framework/EliteDesk ACPI, touchpad, power, hotplug, and multi-display |

### OVD Real-Device Profile Registry

The profile-catalog strategy is documented in
[`docs/OVD_REAL_DEVICE_PROFILE_PLAN.md`](OVD_REAL_DEVICE_PROFILE_PLAN.md).
It establishes a versioned declarative registry for direct QEMU `q35`, `pc`,
AArch64 `virt`, and RISC-V `virt` profiles, plus explicitly conditional
VMApple and Android Emulator/AVD adapters. It also defines generated CSV/JSON/
Markdown reports, profile-to-hardware mappings, schema validation, deterministic
command rendering, external dependency checks, CI lanes, ownership, and
deprecation policy. Native profiles must resolve the latest compatible
`omega.elf` and matching disk-image manifest; missing or stale artifacts are
built before launch. The default Omega system filesystem is ext4,
with FAT32 retained only for an explicit boot/ESP compatibility partition or
legacy boot-image mode.

| OVD profile milestone | Status | Scope |
| --- | --- | --- |
| **R0 Registry foundation** | `PARTIAL` | Versioned JSON schema/catalog, IDs, status, deterministic validation, listing, and rendering are implemented |
| **R1 Native QEMU profiles** | `PARTIAL` | x86_64 `q35`/`pc`, AArch64 `virt`, and RISC-V `virt` definitions plus kernel freshness, staged ext4 image resolution, and verified OVD refresh |
| **R2 Generated reports** | `PLANNED` | Deterministic CSV, JSON, Markdown, and lock artifacts |
| **R3 OVD integration** | `PARTIAL` | Profile list/show/validate/render/artifacts commands, native create-from-profile path, profile-aware Python/Tkinter management, and verified image checks are wired; external adapters remain |
| **R4 Android adapter** | `PLANNED` | AVD discovery, launch, GPU, ADB, snapshots, and cleanup |
| **R5 VMApple adapter** | `PLANNED` | Apple-Silicon/macOS prerequisite and conditional launch workflow |
| **R6 Physical mappings** | `PLANNED` | Hardware records linked to virtual approximations |
| **R7 Maintenance automation** | `PLANNED` | QEMU compatibility scans, ownership, deprecation, and release reports |

## 🖥️ Phase 9: Real Hardware Support

QEMU VirtIO drivers do not transfer to production devices. Phase 9 introduces platform abstraction and real hardware drivers, **one reference board at a time**.

### 9.0 Reference Hardware Targets

| Priority | Platform | Form Factor | Boot Interface |
| :---: | :--- | :--- | :--- |
| 1 | QEMU `virt` (current) | Emulator | DT / UEFI |
| 2 | Raspberry Pi 4/5 | Tablet-class SBC | Device Tree |
| 3 | Intel NUC / generic x86_64 laptop | Laptop/Desktop | UEFI + ACPI |
| 4 | ARM laptop (e.g. Qualcomm dev kit) | Laptop | ACPI / DT |
| 5 | Pixel-class reference phone | Phone | SoC bootloader + DT |

**Strategy:** Fully support one board before expanding. Each new board adds a `kernel/arch/<board>/` platform layer without rewriting portable subsystems.

### 9.1 Platform & Firmware

| Milestone | x86_64 / Laptop | AArch64 / Tablet-Phone |
| :--- | :--- | :--- |
| **9.1.1 Boot handoff** | UEFI boot services, ACPI tables | Device Tree or ACPI, PSCI CPU operations |
| **9.1.2 Interrupt controller** | APIC / x2APIC | GICv3 |
| **9.1.3 Timers** | HPET, TSC | ARM Generic Timer |
| **9.1.4 Clock & reset** | — | SoC clock tree, reset controllers |
| **9.1.5 PMIC / power rails** | — | Regulator framework, PMIC driver |

### 9.2 Essential Driver Stack (Priority Order)

| Priority | Driver | Laptop/Desktop | Tablet | Phone |
| :---: | :--- | :---: | :---: | :---: |
| 1 | **Storage** (NVMe, eMMC, UFS) | ✓ | ✓ | ✓ |
| 2 | **Framebuffer / GPU** (linear FB → full GPU) | ✓ | ✓ | ✓ |
| 3 | **Input** (keyboard, touchpad, touchscreen) | ✓ | ✓ | ✓ |
| 4 | **Network** (Ethernet, WiFi) | ✓ | ✓ | ✓ |
| 5 | **USB / PCIe** | ✓ | ✓ | ○ |
| 6 | **Audio** (I2S, HDMI/DP) | ✓ | ✓ | ✓ |
| 7 | **Power** (cpufreq, cpuidle, suspend/resume) | ✓ | ✓ | ✓ |
| 8 | **Battery / fuel gauge** | ✓ | ✓ | ✓ |
| 9 | **Cellular modem (RIL)** | — | ○ | ✓ |
| 10 | **Camera (ISP), GPS, sensors, NFC, Bluetooth** | ○ | ✓ | ✓ |

✓ = required for MVP · ○ = post-MVP enhancement

### 9.3 Driver Architecture

All drivers run as **userspace servers** communicating over the Phase 7.6 IPC layer, holding capabilities granted by the device manager:

```text
Kernel (minimal)
  ├── IPC / capabilities / scheduling / MMU
  └── Platform HAL (interrupt routing, IOMMU)

Userspace driver servers
  ├── storaged    (NVMe / eMMC / UFS)
  ├── displayd    (framebuffer / GPU)
  ├── netd        (Ethernet / WiFi / cellular)
  ├── inputd      (HID / touch / keyboard)
  ├── audiod      (I2S / HDMI audio)
  └── powerd      (cpufreq / suspend / battery)
```

**Exit criteria:** Omega boots from internal storage on two reference platforms (one x86_64, one AArch64) with working storage, display, input, and network.

---

## 🏗️ Phase 10: Userland & Platform Services

The kernel is approximately 20% of a production OS. Phase 10 builds the software stack users and developers interact with.

### 10.1 Core Userspace

| Milestone | Description | Key Components |
| :--- | :--- | :--- |
| **10.1.1 C Library** | POSIX-compatible libc (musl port or custom) | `pthread`, `malloc`, syscalls, errno, stdio |
| **10.1.2 Init System** | Service supervision and dependency ordering | Process 1, service units, restart policy |
| **10.1.3 Device Manager** | Hotplug enumeration and driver binding | udev-style event dispatch |
| **10.1.4 Core Daemons** | System services | `netd`, `powerd`, `logd`, `sessiond` |

### 10.2 Filesystems

| Milestone | Description | Target Platform |
| :--- | :--- | :--- |
| **10.2.1 ext4 Support** | Full read/write ext4 on block devices | Desktop / laptop |
| **10.2.2 f2fs Support** | Flash-optimized filesystem | Tablet / phone |
| **10.2.3 VFS Maturity** | Inodes, dentry cache, file locking, mmap backing | All |
| **10.2.4 Overlay / Union FS** | Layered filesystem for OTA updates | Tablet / phone |
| **10.2.5 A/B Partition Updates** | Seamless OS updates with rollback | Tablet / phone |

### 10.3 Network Stack (Production)

| Milestone | Description |
| :--- | :--- |
| **10.3.1 Complete TCP/IP** | Full socket API, TCP state machine, UDP, ICMP |
| **10.3.2 DHCP / DNS / NTP** | Automatic network configuration |
| **10.3.3 TLS** | Encrypted transport (mbedTLS or similar) |
| **10.3.4 WiFi Supplicant** | WPA2/WPA3 association and roaming |
| **10.3.5 Firewall** | Per-app and system-wide packet filtering |

### 10.4 Desktop & Laptop UX (Phase 10A)

| Milestone | Description |
| :--- | :--- |
| **10A.1 Display Server** | Wayland-style compositor with GPU acceleration |
| **10A.2 Window Manager** | Decorations, tiling/floating, multi-monitor |
| **10A.3 Input Method & Accessibility** | Keyboard layouts, screen reader hooks |
| **10A.4 System Settings** | Network, display, power, user accounts |
| **10A.5 Terminal & Shell** | Interactive shell, terminal emulator |
| **10A.6 Package Manager** | Signed packages, dependency resolution, repositories |
| **10A.7 Installer** | Disk partitioning, UEFI boot entry, first-boot wizard |

**Success metric (10A):** Daily-driver usable by developers on one laptop model for terminal, browser, and editor workflows.

### 10.5 Mobile & Tablet UX (Phase 10B)

| Milestone | Description |
| :--- | :--- |
| **10B.1 Touch Shell** | Gesture navigation, app launcher, status bar |
| **10B.2 On-Screen Keyboard** | Predictive input, layouts, emoji |
| **10B.3 App Lifecycle** | Background suspend, push notification framework |
| **10B.4 Permissions UI** | Per-app prompts for camera, location, microphone, etc. |
| **10B.5 OTA Update UX** | Silent background updates, rollback, recovery mode |
| **10B.6 App SDK v1** | UI toolkit, sandbox API, build toolchain, emulator integration (extend OVD) |
| **10B.7 Media Framework** | Audio/video playback, camera capture pipeline |

**Success metric (10B):** Usable tablet for browsing, notes, and media on one reference device with sideloaded apps.

### 10.6 Phone UX (Phase 10C)

| Milestone | Description |
| :--- | :--- |
| **10C.1 Telephony (RIL)** | Voice calls, SMS, SIM management, carrier profiles |
| **10C.2 Cellular Data** | Mobile data, APN configuration, tethering |
| **10C.3 Deep Power Management** | Aggressive sleep, wake locks, app standby |
| **10C.4 Location Services** | GPS, fused location, geofencing |
| **10C.5 Sensor Framework** | Accelerometer, gyro, proximity, ambient light |
| **10C.6 App Store / Signing** | Signed app distribution, review pipeline |
| **10C.7 Regulatory Certification** | FCC, CE, GCF carrier certification (if shipping hardware) |

**Success metric (10C):** Voice + data + core apps on one reference phone (Pixel-class dev device).

**Exit criteria (Phase 10):** End-to-end boot to interactive UI on target hardware; third-party developer can build, sign, and run an app using the SDK and OVD emulator.

---

## 🚀 Phase 11: Production Engineering & Operations

Production OS quality depends as much on engineering process as on code.

| Area | Milestone | Description |
| :--- | :--- | :--- |
| **Build** | **11.1 Reproducible Builds** | Deterministic artifacts, hashed outputs, supply-chain audit |
| **Build** | **11.2 Signed Artifacts** | Code signing for kernel, drivers, packages, and OTA payloads |
| **Updates** | **11.3 OTA Infrastructure** | Delta updates, staged rollout, automatic rollback |
| **Updates** | **11.4 Offline Recovery** | Recovery partition, USB sideload, factory reset |
| **Compatibility** | **11.5 Stable ABI** | Frozen syscall ABI, driver IPC versioning, deprecation policy |
| **Testing** | **11.6 Hardware CI Farm** | Automated boot/regression on reference devices 24/7 |
| **Testing** | **11.7 Soak & Stress Testing** | Multi-day stability runs, memory leak detection |
| **Security** | **11.8 External Audit** | Third-party kernel and crypto audit before public beta |
| **Security** | **11.9 Bug Bounty & CVE Process** | Coordinated disclosure, security advisory pipeline |
| **Legal** | **11.10 License Compliance** | GPL/LGPL component tracking, codec patent review, export controls |
| **Support** | **11.11 Developer Portal** | SDK docs, API reference, sample apps, emulator downloads |
| **Support** | **11.12 Crash Analytics** | Privacy-respecting opt-in crash reporting and symbolication |
| **Ecosystem** | **11.13 OEM Onboarding** | BSP packages, signing key ceremony, compliance checklist |

**Exit criteria:** Public beta release with OTA updates, signed packages, documented SDK, and 99.5% crash-free sessions over a 30-day soak test.

---

## 📅 Phase 12: Platform Rollout Timeline

Estimated timelines assume a focused engineering team. Adjust proportionally for team size.

| Phase | Scope | Estimated Duration | Team Size (approx.) |
| :--- | :--- | :--- | :--- |
| **Phase 7** | QEMU parity (block, FB, SMP, IPC) | 6–12 months | 2–5 engineers |
| **Phase 8** | Kernel hardening | 6–12 months | 3–8 engineers |
| **Phase 9** | Real hardware (2 reference platforms) | 12–18 months | 5–15 engineers |
| **Phase 10A** | Desktop/laptop MVP | 12–18 months | 5–15 engineers |
| **Phase 10B** | Tablet MVP | 12–18 months | 10–25 engineers |
| **Phase 10C** | Phone MVP | 24–36 months | 20–50 engineers |
| **Phase 11** | Production ops (parallel with 9–10) | Ongoing from Phase 9 | 3–10 engineers |

### Consolidated Milestones

```text
Year 1        Phase 7 + 8          QEMU-complete, hardened kernel
Year 2        Phase 9 + 10A        Real hardware, desktop daily-driver
Year 3        Phase 10B            Tablet product beta
Year 4–5      Phase 10C + 11       Phone product, public release, OEM partners
```

---

## ✅ Immediate Next Steps for Omega (Action Items)

Concrete sequence mapped to the existing codebase:

1. **Complete shared Phase 7 services** — timer-driven preemption, per-process address spaces, capability IPC, portable DMA/MMIO contracts, and the userspace init path.
2. **Finish x86_64 P0 parity** — ACPI, APIC/x2APIC, HPET/TSC, SMP, MSI/MSI-X, UEFI GOP, PCIe ECAM, and hardened memory permissions on QEMU `q35`.
3. **Finish AArch64 P0 parity** — PSCI, complete GICv3 support, generic timers, SMP, cache/MMU attributes, UEFI/DT normalization, and VirtIO-MMIO completion on QEMU `virt`.
4. **Advance RISC-V P1 parity** — SBI HSM/timer/IPI services, SMP, PLIC hardening, AIA capability planning, Sv48/PMP preparation, and VirtIO-MMIO completion on QEMU `virt`.
5. **Complete storage and display reference paths** — NVMe/AHCI/SDHCI/USB layers, ext4 writeback, AArch64/RISC-V VirtIO-GPU, UEFI GOP, and real framebuffer integration. *(Standard VGA and x86_64 transitional VirtIO-Block completion are validated.)*
6. **Validate one reference board per ISA** — QEMU `q35`/OVMF, QEMU AArch64 `virt`, QEMU RISC-V `virt`, then Raspberry Pi 4/5 and one x86_64 laptop.
7. **Build the userspace base** — Minimal init, serial shell, libc subset, process tools, filesystem utilities, and a versioned syscall/IPC ABI in `docs/ABI.md`.
8. **Move drivers behind IPC** — Storage, network, display, input, and USB as capability-scoped userspace servers with measured IPC overhead.
9. **Keep OVD aligned with kernel maturity** — Add architecture capability gates, live boot matrices, device hotplug scenarios, artifact manifests, and profile readiness checks for each QEMU reference target.
10. **Defer phone work** — Until x86_64 and AArch64 laptop/tablet reference systems demonstrate reliable boot, storage, networking, display, input, power, recovery, and userspace quality.

---

## 📊 Scope & Risk Summary

| Goal | Estimated Effort |
| :--- | :--- |
| Daily-driver desktop on 1–2 machines | 5–15 engineers · 2–3 years |
| Consumer tablet product | 20–50 engineers · 3–5 years |
| Consumer smartphone | 50–200+ engineers · 5–10 years + OEM/carrier partners |

**Key risks:**

- **Driver long pole:** GPU, WiFi, and cellular modem drivers dominate calendar time.
- **Ecosystem chicken-and-egg:** Apps require developers; developers require devices and SDK maturity.
- **Microkernel IPC overhead:** Must be measured and optimized early to avoid performance regressions vs monolithic kernels.
- **Phone certification:** Regulatory and carrier approval adds 6–12 months beyond software readiness.

Omega's multi-arch HAL, freestanding C++20 runtime, formal syscall ABI, and OVD tooling provide a strong kernel-side foundation. The critical path to production runs through **real hardware drivers, userland maturity, and ecosystem tooling**—not additional QEMU subsystems alone.

---

## 📚 Related Documentation

| Document | Scope |
| :--- | :--- |
| `docs/ARCHITECTURE.md` | Kernel architecture and HAL design |
| `docs/ABI.md` | System call ABI specification |
| `docs/FIRMWARE_BOOT.md` | UEFI, U-Boot, and Coreboot compatibility |
| `docs/RUNNING.md` | Build and QEMU execution guide |
| `docs/VGA_DISPLAY_PLAN.md` | System Display Module — x86_64 Standard VGA (Phase 7.2) |
| `docs/DISPLAY_AARCH64_RISCV_PLAN.md` | System Display Module — AArch64 & RISC-V extension (Phase 7.2b) |
| `docs/STORAGE_ARCHITECTURE_PLAN.md` | Cross-architecture storage architecture and implementation plan |
| `scripts/README.md` | Script catalog, launcher options, image generation, and test guide |
| `emulator/README.md` | OVD configuration, storage profiles, launch modes, and test guide |
| `docs/RISCV64_PLAN.md` | RISC-V 64 architectural plan |
| `docs/COMPLETION_REPORT.md` | Phase 1–6 verification report |
