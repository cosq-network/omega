# Omega System Display Module: AArch64 & RISC-V 64 Extension Plan

## Document Status

| Field | Value |
| :--- | :--- |
| **Target Phase** | Phase 7.2b (QEMU Parity) → Phase 9.2 (Real Hardware) |
| **Primary Architectures** | AArch64, RISC-V 64 (`rv64gc`) |
| **Prerequisite** | Phase 7.2 completed on x86_64 (`docs/VGA_DISPLAY_PLAN.md`) |
| **Related Roadmap Items** | Phase 7.2b, Phase 7.6 (IPC), Phase 9.2 (displayd), Phase 10A.1 (Compositor) |

---

## 1. Executive Summary

Phase 7.2 delivered the **System Display Module (SDM)** on **x86_64** using Standard VGA (text mode + Bochs VBE). The AArch64 and RISC-V 64 SimpleFb integration slice is now implemented: both architectures have real display HALs, shared FDT parsing, boot-time DT pointer handoff, portable console initialization, and safe serial fallback. VirtIO-GPU remains the next display milestone.

This plan specifies how to extend the SDM to **AArch64** and **RISC-V 64** without rewriting the portable console, font, or `kprintf` routing layers. The same `hal::Display` → `display::Console` → `framebuffer.cpp` stack used on x86_64 applies; only arch-specific **backend probe**, **framebuffer mapping**, and **mode setup** differ.

The SDM on ARM/RISC-V is deliberately **framebuffer-first** (no VGA text mode):

1. **Device Tree simple-framebuffer** — fast QEMU bring-up; firmware describes a linear FB in the DT.
2. **VirtIO-GPU** — production-oriented path for QEMU `virt` and future paravirtualized guests.
3. **UEFI GOP / DT ramfb handoff** — same `BootFramebuffer` backend as x86 when firmware provides metadata.

**Success criteria for Phase 7.2b v1:**

- Omega initializes the shared display console on **QEMU AArch64 `-M virt`** with serial mirroring and a safe serial-only fallback when no framebuffer is handed off.
- SimpleFb pixel read/write self-tests are implemented and run when a valid framebuffer is present.
- `scripts/test_display_aarch64.sh` validates AArch64 boot, the display HAL marker, console routing, and idle-loop completion.
- RISC-V 64 now reaches `kernel_main()` after the OpenSBI handoff, boot-section, and PMM bootstrap fixes.
- OVD `ovd_run.sh --gpu` launches AArch64 with `-device virtio-gpu-pci`.

---

## 2. Relationship to x86_64 SDM

### 2.1 What Is Already Portable (Do Not Fork)

| Component | Location | Role |
| :--- | :--- | :--- |
| HAL interface | `kernel/include/arch/display.hpp` | Backend enum, `FramebufferInfo`, pixel-format metadata, `Display::init()` API |
| Graphical console | `kernel/sys/display_console.cpp` | Scroll, cursor grid, dual serial+FB output |
| Framebuffer primitives | `kernel/sys/framebuffer.cpp` | `put_pixel`, `fill_rect`, bpp dispatch |
| Font renderer | `kernel/sys/font.cpp` | 8×16 bitmap glyphs |
| `kprintf` mirroring | `kernel/sys/kprint.cpp` | Routes to `display::Console` when enabled |

### 2.2 What Is x86-Specific (Do Not Port)

| Component | Location | Reason |
| :--- | :--- | :--- |
| VGA text mode | `kernel/arch/x86_64/vga_text.cpp` | PC legacy; no ARM/RISC-V equivalent |
| VGA I/O ports | `kernel/arch/x86_64/vga_regs.cpp` | Port I/O at `0x3C0`–`0x3DF` |
| Bochs DISPI | `kernel/arch/x86_64/bochs_vbe.cpp` | Bochs/QEMU x86 VGA extension |
| Multiboot2 tags | `kernel/arch/x86_64/boot_fb.cpp` | x86 bootloader protocol (GRUB); ARM uses DT/UEFI |

### 2.3 HAL Backend Extensions Required

Extend `DisplayBackend` in `kernel/include/arch/display.hpp`:

```cpp
enum class DisplayBackend : uint8_t {
    None = 0,
    VgaText,           // x86_64 only
    BochsVbe,          // x86_64 only
    BootFramebuffer,   // Multiboot2 / UEFI GOP / DT ramfb (any arch)
    SimpleFb,          // Device Tree "simple-framebuffer" node
    VirtioGpu,         // VirtIO-GPU paravirtual device
};
```

---

## 3. Platform & Hardware Matrix

### 3.1 QEMU `virt` (Reference Target)

| Architecture | Display Hardware | Recommended v1 Backend | QEMU Flags |
| :--- | :--- | :--- | :--- |
| **AArch64** | `simple-framebuffer` in DT | **SimpleFb** | Firmware/DT-dependent; current QEMU `virt` smoke path uses serial fallback |
| **AArch64** | VirtIO-GPU PCI | **VirtioGpu** | `-device virtio-gpu-pci -display sdl` |
| **RISC-V 64** | Same as AArch64 | **SimpleFb** → serial fallback → **VirtioGpu** | `-M virt -cpu rv64 -bios default` |
| **Either** | UEFI GOP (future) | **BootFramebuffer** | UEFI boot via OVMF / EDK2 |

### 3.2 Real Hardware (Phase 9 Preview)

| Platform | Display Path | Omega Action |
| :--- | :--- | :--- |
| **Raspberry Pi 4/5** | Mailbox → framebuffer property tags | Platform driver in `kernel/arch/bcm2711/` |
| **ARM laptop (UEFI)** | GOP active at ExitBootServices | Parse GOP info → `BootFramebuffer` |
| **QEMU `-M raspi3`** | `simple-framebuffer` or vendor mailbox | DT simplefb + optional mailbox |
| **RISC-V SBC** | DT simplefb or LCD controller | DT probe + panel driver (Phase 9+) |

### 3.3 Explicit Non-Goals (Phase 7.2b)

- 3D acceleration, GL/Vulkan, hardware cursor sprites.
- Full DRM/KMS-style mode enumeration in kernel.
- Native Adreno / Mali / Intel GPU drivers (Phase 9+ / userspace `displayd`).
- PL111 bare-metal driver (defer unless simplefb unavailable on target board).
- SMP-safe display spinlock (defer to Phase 7.3 alongside general SMP work).

---

## 4. Backend Specifications

### 4.1 Device Tree Simple Framebuffer (`SimpleFb`)

QEMU `-M virt` (AArch64 and RISC-V) can expose a **`simple-framebuffer`** compatible node in the Flattened Device Tree (FDT).

**Typical DT properties:**

| Property | Type | Meaning |
| :--- | :--- | :--- |
| `compatible` | string | `"simple-framebuffer"` |
| `reg` | u64 × 2 | Physical base and size of framebuffer memory |
| `width` | u32 | Horizontal resolution in pixels |
| `height` | u32 | Vertical resolution in pixels |
| `stride` | u32 | Bytes per scanline (≥ `width × bytes_per_pixel`) |
| `format` | string | Pixel format (see table below) |

**Common `format` values:**

| Format string | BPP | Notes |
| :--- | :---: | :--- |
| `x8r8g8b8` | 32 | BGRA in little-endian memory (matches current `put_pixel` layout) |
| `a8r8g8b8` | 32 | Same channel order with alpha |
| `r5g6b5` | 16 | RGB565 — existing `framebuffer.cpp` path |
| `x1r5g5b5` | 16 | 15-bit RGB |

**Probe algorithm:**

```text
1. Locate FDT pointer from boot (see §5.1).
2. Walk nodes; match compatible = "simple-framebuffer".
3. Parse reg, width, height, stride, format → FramebufferInfo.
4. map_framebuffer_pages() via VMM.
5. active = SimpleFb; caps.linear_framebuffer = true; caps.text_mode = false.
```

### 4.2 VirtIO-GPU (`VirtioGpu`)

VirtIO-GPU is the long-term paravirtual display for QEMU `virt` and cloud/VM deployments. Device ID: PCI `0x1AF4:0x1050` (virtio GPU) or MMIO virtio on some platforms.

**Minimum command sequence (2D path only):**

```text
1. Discover device (PCI config scan or DT `virtio,mmio` node).
2. Reset device; negotiate features (ignore 3D features for v1).
3. Setup virtqueue for controlq.
4. VIRTIO_GPU_CMD_GET_DISPLAY_INFO   → width, height
5. VIRTIO_GPU_CMD_RESOURCE_CREATE_2D  → guest buffer description
6. VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING → guest physical pages
7. VIRTIO_GPU_CMD_SET_SCANOUT       → bind resource to display 0
8. Write pixels to backing memory (reuse put_pixel / font renderer)
9. VIRTIO_GPU_CMD_TRANSFER_TO_HOST + RESOURCE_FLUSH (when partial updates needed)
```

**Shared implementation:** Place protocol logic in `kernel/sys/virtio_gpu.cpp`; arch files provide PCI/MMIO transport only (mirror future VirtIO block/net layering).

### 4.3 Boot Framebuffer Handoff (`BootFramebuffer`)

Reuse the existing `BootFramebuffer` backend concept from x86:

| Source | Metadata | Parser location |
| :--- | :--- | :--- |
| UEFI GOP | `fb_base`, width, height, pitch, pixel format | Future EFI stub → boot params |
| DT `/chosen` ramfb | Pre-configured FB from firmware | Extend `boot_fb` or DT walker |
| Linux-style `screen-info` | Legacy — not targeted | — |

On AArch64 UEFI boot, GOP info should populate `FramebufferInfo` without reprogramming the hardware.

---

## 5. Boot & Initialization

### 5.1 Locating the Device Tree

| Architecture | Boot protocol | DT pointer |
| :--- | :--- | :--- |
| **AArch64 QEMU `-kernel`** | Direct kernel load | DTB appended by QEMU after kernel, or register `x0` = DT pointer (verify against QEMU version) |
| **AArch64 UEFI** | EFI stub (future) | From EFI configuration table |
| **RISC-V OpenSBI** | `a0` = hartid, `a1` = DT pointer | Pass from `boot.s` to C++ global |
| **RISC-V QEMU `-kernel`** | Firmware loads kernel | DT from OpenSBI or QEMU append |

**Implemented:** `kernel/sys/fdt.cpp` provides the shared walker. AArch64 passes the boot `x0` value and RISC-V passes OpenSBI `a1` into `kernel_main()`, where the pointer is registered before display probing.

### 5.2 Initialization Sequence (All Architectures)

```text
kernel_main()
  │
  ├─ hal::uart_init()
  ├─ memory::PhysicalMemoryManager::init()
  ├─ memory::VirtualMemoryManager::init()     ← MUST be real before FB map
  │
  ├─ hal::Display::init()                     ← shared initialization on all architectures
  │     ├─ BootFramebuffer (UEFI / DT ramfb) if valid
  │     ├─ Else VirtioGpu if PCI/MMIO device found
  │     ├─ Else SimpleFb if DT node found
  │     └─ Else None (serial only, validated on current QEMU virt)
  │
  ├─ display::Console::init()
  ├─ kernel::kprint_enable_console_routing()
  │
  └─ ... remaining subsystems ...
```

### 5.3 Framebuffer Memory Mapping

x86_64 currently identity-maps the low 4 GiB and probes FB with a write/read test. **AArch64 and RISC-V require real `vmm_map_page()`**:

```text
Physical:  From DT reg or VirtIO backing pages
Virtual:   Fixed kernel window — e.g. 0xFFFF8000_00000000 + offset (see docs/ABI.md)
Flags:     PAGE_PRESENT | PAGE_WRITABLE (kernel-only in v1)
Cache:     Device-nGnRnE (ARM) / appropriate PTE attributes for uncached FB
Pages:     ceil(stride × height / 4096)
```

**Blocker:** `kernel/sys/vmm.cpp` is currently a stub on all architectures. Phase 7.2b should either:

- **Option A (preferred):** Implement real page table walks for AArch64/RISC-V before display work, or
- **Option B (QEMU stopgap):** Extend early boot page tables in `boot.s` to cover known FB physical regions.

---

## 6. Proposed Source Tree

```text
kernel/
├── include/
│   ├── arch/display.hpp              # + SimpleFb, VirtioGpu backends
│   └── kernel/
│       ├── fdt.hpp                   # FDT node/property API
│       └── virtio_gpu.hpp            # VirtIO-GPU protocol
├── sys/
│   ├── fdt.cpp                       # Shared flattened DT walker
│   ├── virtio_gpu.cpp                # Shared VirtIO-GPU 2D commands
│   ├── display_console.cpp           # (unchanged)
│   ├── framebuffer.cpp               # + format string dispatch for simplefb
│   └── font.cpp                      # (unchanged)
├── arch/aarch64/
│   ├── display.cpp                   # SimpleFb HAL and serial fallback
│   └── virtio_transport.cpp          # PCI/MMIO virtqueue access
└── arch/riscv64/
    ├── display.cpp                   # SimpleFb HAL and serial fallback
    └── virtio_transport.cpp
```

**CMakeLists.txt:** The architecture branches now build `display.cpp`; the former `display_stub.cpp` files are no longer used.

---

## 7. Implementation Phases

### Phase 7.2b.1 — FDT Infrastructure

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2b.1a** | FDT header validation & node iterator | `kernel/sys/fdt.cpp` | Implemented shared DT structure/property walker |
| **7.2b.1b** | Capture DT pointer in AArch64 boot | `kernel/init/main.cpp`, `boot.s` | Implemented from boot `x0` |
| **7.2b.1c** | Capture DT pointer in RISC-V boot | `kernel/init/main.cpp`, `boot.s` | Implemented from OpenSBI `a1` |

### Phase 7.2b.2 — SimpleFb on AArch64

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2b.2a** | Parse `simple-framebuffer` node | `kernel/sys/fdt.cpp`, `display.cpp` | Implemented with format metadata |
| **7.2b.2b** | Map FB via VMM | `display.cpp`, `vmm.cpp` | Identity-map QEMU stopgap implemented; real VMM remains pending |
| **7.2b.2c** | Replace stub; enable display init in `main.cpp` | `display.cpp`, `main.cpp` | Implemented; banner/console route over FB when handed off |
| **7.2b.2d** | In-kernel self-tests | `display.cpp` | Implemented SimpleFb pixel read/write test |
| **7.2b.2e** | CI test script | `scripts/test_display_aarch64.sh` | Implemented HAL/fallback smoke test |

### Phase 7.2b.3 — RISC-V 64 Parity

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2b.3a** | Fix kernel entry after OpenSBI | `boot.s`, linker.ld | Completed; `_start` is image entry and QEMU reaches C++ |
| **7.2b.3b** | Port SimpleFb probe | `kernel/arch/riscv64/display.cpp`, `kernel/sys/fdt.cpp` | Shared HAL path completed; serial fallback validated |
| **7.2b.3c** | CI RISC-V display smoke test | `scripts/test.sh` | Integration assertions added |

### Phase 7.2b.4 — VirtIO-GPU

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2b.4a** | Shared VirtIO transport layer | `virtio_transport.cpp`, `virtio_gpu.cpp` | Detect `1AF4:1050` |
| **7.2b.4b** | GET_DISPLAY_INFO + RESOURCE_CREATE_2D | `virtio_gpu.cpp` | Log mode from device |
| **7.2b.4c** | SET_SCANOUT + pixel write | `virtio_gpu.cpp` | Crosshair or banner visible |
| **7.2b.4d** | Backend priority over SimpleFb when device present | `display.cpp` init | `[+] Display: VirtioGpu` |
| **7.2b.4e** | OVD `--gpu` for AArch64 | `ovd_run.sh` | SDL window with banner |

### Phase 7.2b.5 — Hardening

| Task ID | Task | Verification |
| :---: | :--- | :--- |
| **7.2b.5a** | Pixel format dispatch (`r5g6b5`, `x8r8g8b8`) | Correct colors in self-test |
| **7.2b.5b** | Bounds checking on all draw calls | No fault on edge coordinates |
| **7.2b.5c** | Document QEMU commands | `docs/RUNNING.md` updated |

---

## 8. Testing Plan

### 8.1 QEMU Launch Matrix

| Config | Command | Expected Backend |
| :--- | :--- | :--- |
| AArch64 serial + DT FB | `qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel omega.elf` | SimpleFb when firmware supplies DT FB; otherwise serial fallback |
| AArch64 VirtIO-GPU GUI | `... -device virtio-gpu-pci -display sdl` | VirtioGpu |
| RISC-V serial + DT FB | `qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic -kernel omega.elf` | SimpleFb when firmware supplies DT FB; otherwise serial fallback |
| OVD AArch64 headless | `ovd_run.sh run --name tablet --no-gpu` | SimpleFb or serial |
| OVD AArch64 GUI | `ovd_run.sh run --name tablet --gpu` | VirtioGpu |

### 8.2 Automated Tests

**Script:** `scripts/test_display_aarch64.sh`

```bash
# 1. Build aarch64 kernel
# 2. Boot QEMU -M virt, capture serial
# 3. Assert: "Display: SimpleFb" or the documented serial fallback marker
# 4. Assert: "[TEST][PASS] Display console write path"
# 5. Assert: "Welcome to Omega Kernel"
```

**Extend `scripts/test.sh`:** Add optional display assertions to existing AArch64 `run_test` once backend is live.

**Extend `.github/workflows/ci.yml`:** Add step after VGA tests:

```yaml
- name: Run AArch64 Display Tests
  run: ./scripts/test_display_aarch64.sh
```

### 8.3 In-Kernel Self-Test Markers

| Marker | Meaning |
| :--- | :--- |
| `[+] Display: SimpleFb WxHxBPP` | DT backend active |
| `[!] Display: No framebuffer backend found (serial only)` | Safe fallback when no DT/GOP/virtio backend is available |
| `[+] Display: VirtioGpu WxHxBPP` | VirtIO-GPU scanout active |
| `[TEST][PASS] SimpleFb pixel read/write` | FB mapping verified |
| `[TEST][PASS] Display console write path` | Portable console verified (reuse x86 test) |

---

## 9. OVD Integration

Extend `emulator/ovd_manager.sh` config:

```ini
ovd.vga=std          # x86_64 — existing
ovd.vga=simplefb     # AArch64/RISC-V default headless
ovd.vga=virtio-gpu   # AArch64/RISC-V GUI
```

Extend `emulator/ovd_run.sh`:

| Arch | `--no-gpu` | `--gpu` |
| :--- | :--- | :--- |
| x86_64 | `-vga std -display none` | `-vga std -display sdl/cocoa` |
| aarch64 | `-nographic` (+ implicit DT FB) | `-device virtio-gpu-pci -display sdl` |
| riscv64 | `-nographic` | `-device virtio-gpu-pci -display sdl` |

---

## 10. Dependencies & Blockers

| Dependency | Status | Impact |
| :--- | :--- | :--- |
| **Real VMM** (`map_page`) | Still a stub for non-x86 bring-up | Current SimpleFb path uses identity-map QEMU stopgap; arbitrary physical FB mappings need real page tables |
| **FDT pointer in boot** | Implemented | AArch64 captures `x0`; RISC-V receives OpenSBI `a1` and passes it to C++ |
| **RISC-V kernel boot** | Fixed for QEMU OpenSBI | `_start` is first in the image, PMM metadata is kernel-owned, and `kernel_main()` reaches idle |
| **PCI on AArch64** | Stub scanner | VirtIO-GPU needs ECAM or MMIO transport |
| **VirtIO core** | Net is placeholder | VirtIO-GPU shares queue/ descriptor logic |

**Recommended sequencing:** real VMM → VirtIO transport/discovery → VirtIO-GPU 2D path → OVD/CI framebuffer assertions → UEFI GOP/real hardware handoff.

---

## 11. Security & Safety

| Concern | Mitigation |
| :--- | :--- |
| **FB mapped to userland** | Kernel-only mapping in v1; explicit syscall for compositor (Phase 8) |
| **DT spoofing** | Trust firmware DT in v1; verify reserved-memory on real hardware |
| **VirtIO queue overflow** | Validate descriptor chains; bound command sizes |
| **Draw bounds** | Reuse existing `put_pixel` / `fill_rect` clamping |

---

## 12. Future Evolution

```text
Phase 7.2b (this plan)
  ├─ SimpleFb + serial fallback on QEMU virt (AArch64, RISC-V)
  └─ VirtIO-GPU on QEMU virt (AArch64, RISC-V) — pending transport layer

Phase 9.2
  └─ Raspberry Pi mailbox FB, UEFI GOP, userspace displayd

Phase 10A.1
  └─ Wayland-style compositor on shared FB mapping
```

---

## 13. Milestone Summary

| Milestone | Deliverable | Target |
| :--- | :--- | :--- |
| **M1** | FDT parser + DT pointer in boot | Phase 7.2b.1 |
| **M2** | SimpleFb-capable AArch64 HAL plus validated serial fallback | Phase 7.2b.2 |
| **M3** | SimpleFb-capable RISC-V HAL plus validated OpenSBI boot/fallback | Phase 7.2b.3 |
| **M4** | VirtIO-GPU + OVD `--gpu` on ARM | Phase 7.2b.4 |
| **M5** | CI display tests all three ISAs | Phase 7.2b.5 |

---

## 14. References

| Resource | URL / Location |
| :--- | :--- |
| Device Tree simple-framebuffer binding | https://www.kernel.org/doc/Documentation/devicetree/bindings/display/simple-framebuffer.txt |
| VirtIO GPU specification | https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html |
| QEMU virt machine | https://www.qemu.org/docs/master/system/arm/virt.html |
| Omega x86 SDM plan | `docs/VGA_DISPLAY_PLAN.md` |
| Omega HAL display API | `kernel/include/arch/display.hpp` |
| Omega roadmap Phase 7.2b | `docs/ROADMAP.md` |

---

## Appendix A: Backend Selection Pseudocode

```cpp
void Display::init() {
#if defined(__x86_64__)
    // Existing: BootFramebuffer → BochsVbe → VgaText
#elif defined(__aarch64__) || defined(__riscv)
    if (boot_fb_probe().valid)       { use BootFramebuffer; return; }
    if (virtio_gpu_init(&fb_info))   { use VirtioGpu;       return; }
    if (simplefb_probe(&fb_info))    { use SimpleFb;        return; }
#endif
    // None — serial only
}
```

## Appendix B: FDT Property Read Example

```text
Node: /simple-framebuffer@80000000
  compatible = "simple-framebuffer"
  reg = <0x0 0x80000000 0x0 0x00800000>
  width = <1024>
  height = <768>
  stride = <4096>
  format = "x8r8g8b8"
```

→ `FramebufferInfo { phys=0x80000000, width=1024, height=768, pitch=4096, bpp=32 }`
