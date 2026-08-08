# Omega System Display Module: Standard VGA Implementation Plan

## Document Status

| Field | Value |
| :--- | :--- |
| **Target Phase** | Phase 7.2 (QEMU Parity) → Phase 9 (Real Hardware) |
| **Primary Architecture** | x86_64 (Standard VGA is a PC/AT legacy subsystem) |
| **Secondary Path** | Multiboot2 / UEFI GOP framebuffer handoff (bootloader-provided) |
| **Related Roadmap Items** | Phase 7.2, Phase 7.2b (`docs/DISPLAY_AARCH64_RISCV_PLAN.md`), Phase 9.2 (Framebuffer/GPU), Phase 10A.1 (Display Server) |

---

## 1. Executive Summary

This plan specifies how **Omega** will implement a **System Display Module (SDM)** built around **Standard VGA** hardware and its modern extensions. The module provides the first graphical output path for laptops and desktops before a full GPU driver stack (Adreno, Intel, AMD) exists.

The SDM is deliberately layered:

1. **VGA Text Mode** — immediate, zero-setup boot messages (80×25 character console).
2. **Linear Framebuffer (VBE / Bochs VBE / Bootloader GOP)** — primary graphical surface for fonts, pixels, and a future compositor.
3. **Portable Display HAL** — architecture-neutral API so AArch64 (PL111, simplefb) and RISC-V (virtio-gpu) can plug in later without rewriting the console or compositor.

**Success criteria for v1:**

- Omega prints a boot banner to VGA text mode on QEMU `-vga std` within 100 ms of `kernel_main()`.
- Omega switches to a ≥640×480×32 bpp linear framebuffer and renders a graphical console with a built-in 8×16 font.
- Serial (`COM1`) and display consoles operate concurrently; `kprintf` output is mirrored to both.
- Framebuffer physical memory is mapped through the existing VMM (`vmm_map_page`) with correct cache attributes.
- Automated CI test verifies pixel output via QEMU screenshot or Bochs VBE register state.

---

## 2. Terminology & Scope

### 2.1 What “Standard VGA” Means in This Plan

| Term | Description | Omega Support Tier |
| :--- | :--- | :--- |
| **VGA Text Mode** | 80×25 character cell matrix at physical `0xB8000` (color) | **Tier 1 — Required** |
| **VGA Register I/O** | Motorola 6845 CRTC-based programming via ports `0x3C0`–`0x3DF` | **Tier 1 — Required** |
| **VGA Graphics Modes** | Legacy modes (e.g. Mode 12h 640×480×16, Mode 13h 320×200×256) via register sets | **Tier 2 — Optional fallback** |
| **VESA BIOS Extensions (VBE)** | Extended linear framebuffer modes via BIOS `INT 10h` | **Tier 2 — Bootloader-assisted only** |
| **Bochs VBE Extensions** | QEMU/Bochs DISPI interface (I/O `0x1CE`/`0x1CF`, PCI BAR framebuffer) | **Tier 1 — Required for QEMU** |
| **UEFI GOP Framebuffer** | Graphics Output Protocol handoff from UEFI firmware | **Tier 1 — Required for UEFI boot path** |
| **Multiboot2 Framebuffer Tag** | Bootloader-provided linear FB metadata (tag type 8) | **Tier 1 — Required for GRUB boot** |

### 2.2 Explicit Non-Goals (v1)

- 3D acceleration, hardware cursor sprite engine, or multi-monitor spanning.
- Full SVGA/VESA mode enumeration via real-mode BIOS calls from long-mode kernel.
- AMD/NVIDIA/Intel native GPU drivers (deferred to Phase 9+ / userspace `displayd`).
- Wayland compositor (Phase 10A.1); v1 delivers kernel graphical console only.

### 2.3 Platform Matrix

| Platform | Display Hardware | v1 Strategy |
| :--- | :--- | :--- |
| **QEMU x86_64 `-vga std`** | Bochs VGA + VBE | Bochs DISPI + text mode |
| **QEMU x86_64 `-vga vmware`** | VMware SVGA-II | Deferred (Phase 7.2b) |
| **Coreboot + SeaBIOS** | Standard VGA | Text mode + VBE via SeaBIOS handoff |
| **UEFI (OVMF / TianoCore)** | GOP framebuffer | UEFI GOP info passed via boot params |
| **Physical PC (Intel/AMD iGPU)** | UEFI GOP primary; legacy VGA behind PCI bridges | GOP handoff; text mode if firmware leaves it active |
| **AArch64 / RISC-V** | Not VGA | Separate HAL backend (`simplefb`, `virtio-gpu`) — see **`docs/DISPLAY_AARCH64_RISCV_PLAN.md`** (Phase 7.2b) |

---

## 3. Hardware Reference

### 3.1 VGA Text Mode Framebuffer

```text
Physical address: 0x000B8000 (color text buffer)
Dimensions:       80 columns × 25 rows
Cell format:      2 bytes per cell
  Byte 0: ASCII character code
  Byte 1: Attribute (bits 3:0 = foreground, bits 6:4 = background, bit 7 = blink)

Default attribute: 0x07 (light gray on black)
```

The text buffer is directly accessible once paging identity-maps the first 1–4 GiB (already done in `kernel/arch/x86_64/boot.s`).

### 3.2 VGA I/O Port Map (Core Registers)

| Port(s) | Register Block | Purpose |
| :--- | :--- | :--- |
| `0x3C0` / `0x3C1` | Attribute Controller | Color palette, mode control |
| `0x3C2` | Miscellaneous Output | Clock select, I/O vs memory mapping |
| `0x3C4` / `0x3C5` | Sequencer | Timing, plane enable |
| `0x3D4` / `0x3D5` | CRT Controller (CRTC) | Horizontal/vertical timing, scanline |
| `0x3CE` / `0x3CF` | Graphics Controller | Memory access mode, plane logic |
| `0x3DA` | Input Status Register 1 | Vertical retrace polling (flip timing) |

**I/O privilege:** Port access requires Ring 0 or `IOPM` cleared in TSS for userland drivers (future). All v1 access stays in kernel.

### 3.3 Bochs VBE / QEMU Standard VGA (DISPI Interface)

QEMU's `-vga std` exposes **Bochs Display Adapter** extensions:

| Resource | Location |
| :--- | :--- |
| PCI Vendor:Device | `0x1234:0x1111` (legacy) or `0x1AF4:0x1111` (virtio-vga variant) |
| Framebuffer BAR | PCI BAR0 (prefetchable memory, typically 16 MiB) |
| I/O Registers | Index `0x01CE`, Data `0x01CF` (16-bit) |

**Key DISPI registers (index → data):**

| Index | Name | Purpose |
| :---: | :--- | :--- |
| `0x0` | ID | Magic `'V' << 8 | 'B' << 0` = `0x4256` ("VB") |
| `0x1` | XRES | Horizontal resolution |
| `0x2` | YRES | Vertical resolution |
| `0x3` | BPP | Bits per pixel (8, 15, 16, 24, 32) |
| `0x4` | ENABLE | Bit 0 = enable, bit 1 = linear framebuffer |
| `0x5` | BANK | Banking (unused in linear mode) |
| `0x8` | VIRT_WIDTH | Virtual width in pixels |
| `0x9` | VIRT_HEIGHT | Virtual height in pixels |
| `0xA` | X_OFFSET | Panning X |
| `0xB` | Y_OFFSET | Panning Y |

**Recommended v1 mode:** 1024×768×32 bpp, linear framebuffer enabled.

### 3.4 Bootloader-Provided Framebuffer (No Direct VGA Programming)

When Omega is loaded via **Multiboot2** (GRUB) or **UEFI GOP**, the firmware or bootloader has already set a linear mode. The kernel must **not** reprogram VGA hardware; it consumes handoff metadata:

**Multiboot2 Tag Type 8 (Framebuffer):**

```text
framebuffer_addr   : u64   Physical address (0 = none)
framebuffer_pitch  : u32   Bytes per scanline
framebuffer_width  : u32   Pixels
framebuffer_height : u32   Pixels
framebuffer_bpp    : u8    Bits per pixel
framebuffer_type   : u8    1 = indexed, 2 = RGB
color_info         : varies
```

**UEFI GOP** (passed via custom boot params or embedded in a future EFI stub):

```text
fb_base, fb_size, width, height, pixels_per_scanline, pixel_format
```

---

## 4. Architecture Within Omega

### 4.1 Layered Module Design

```text
┌─────────────────────────────────────────────────────────────┐
│  Consumers: kprintf, future compositor, userspace displayd  │
├─────────────────────────────────────────────────────────────┤
│  kernel/display/  — Console, font renderer, blit primitives │
├─────────────────────────────────────────────────────────────┤
│  kernel/sys/framebuffer.hpp — FB info, pixel format, API    │
├─────────────────────────────────────────────────────────────┤
│  hal::Display — init, mode set, putpixel, scroll, flush     │
├─────────────────────────────────────────────────────────────┤
│  Backends (x86_64):                                         │
│    vga_text.cpp    — Text mode 0xB8000                      │
│    bochs_vbe.cpp   — Bochs DISPI linear FB                  │
│    boot_fb.cpp     — Multiboot2 / UEFI GOP handoff          │
│    vga_regs.cpp    — Shared I/O port helpers                │
└─────────────────────────────────────────────────────────────┘
```

This mirrors the existing split between `hal::uart_*` (arch-specific) and `kernel::kprintf` (portable).

### 4.2 Proposed Source Tree

```text
kernel/
├── include/
│   ├── arch/
│   │   └── display.hpp              # HAL display interface
│   └── kernel/
│       ├── framebuffer.hpp          # Pixel format, FB metadata struct
│       └── console.hpp              # Graphical + text console API
├── arch/x86_64/
│   ├── vga_text.cpp                 # Text mode writer
│   ├── vga_regs.cpp                 # Port I/O, wait-for-vsync
│   ├── bochs_vbe.cpp                # Bochs DISPI mode set + PCI probe
│   └── boot_fb.cpp                  # Multiboot2 / boot param FB parser
├── sys/
│   ├── display_console.cpp          # Scrollback, cursor, ANSI subset
│   ├── font.cpp                     # Embedded 8×16 bitmap font
│   └── framebuffer.cpp              # Portable blit, fill, line
└── init/
    └── main.cpp                       # display::init() after VMM
```

### 4.3 Build Integration (`CMakeLists.txt`)

Add to `KERNEL_SOURCES` under the `x86_64` branch:

```cmake
kernel/arch/x86_64/vga_text.cpp
kernel/arch/x86_64/vga_regs.cpp
kernel/arch/x86_64/bochs_vbe.cpp
kernel/arch/x86_64/boot_fb.cpp
kernel/sys/display_console.cpp
kernel/sys/font.cpp
kernel/sys/framebuffer.cpp
```

Extend Multiboot2 header in `boot.s` with **Framebuffer request tag** (type 5, pref width/height/bpp) so GRUB allocates a mode when used.

---

## 5. HAL & Kernel API Specification

### 5.1 HAL Interface (`kernel/include/arch/display.hpp`)

```cpp
namespace hal {

enum class DisplayBackend : uint8_t {
    None = 0,
    VgaText,
    BochsVbe,
    BootFramebuffer,  // Multiboot2 / UEFI GOP
};

struct FramebufferInfo {
    uintptr_t phys_addr;
    uintptr_t virt_addr;   // Kernel virtual mapping
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;       // Bytes per scanline
    uint8_t   bpp;
    uint8_t   red_shift, red_mask;
    uint8_t   green_shift, green_mask;
    uint8_t   blue_shift, blue_mask;
};

struct DisplayCapabilities {
    bool text_mode;
    bool linear_framebuffer;
    bool hardware_cursor;  // false for v1
};

class Display {
public:
    static void init();
    static DisplayBackend active_backend();
    static DisplayCapabilities capabilities();

    // Text mode (VGA only)
    static void text_clear(uint8_t attr);
    static void text_putc(uint8_t col, uint8_t row, char c, uint8_t attr);
    static void text_scroll();

    // Linear framebuffer
    static bool set_mode(uint32_t w, uint32_t h, uint8_t bpp);
    static const FramebufferInfo* framebuffer();
    static void flush();  // Optional cache flush / vsync wait
};

} // namespace hal
```

### 5.2 Portable Console API (`kernel/include/kernel/console.hpp`)

```cpp
namespace display {

enum ConsoleTarget : uint32_t {
    CONSOLE_SERIAL  = 1 << 0,
    CONSOLE_VGA_TEXT = 1 << 1,
    CONSOLE_FRAMEBUFFER = 1 << 2,
    CONSOLE_ALL = 0xFFFFFFFF,
};

class Console {
public:
    static void init(uint32_t targets = CONSOLE_ALL);
    static void putchar(char c);
    static void write(const char* s, size_t len);
    static void clear();
    static void set_target(uint32_t targets);
};

} // namespace display
```

### 5.3 Integration with `kprintf`

Extend `kernel/sys/kprint.cpp` to route output through `display::Console::putchar()` in addition to `hal::uart_putc()`. This gives unified logging across serial and display without duplicating format logic.

---

## 6. Initialization Sequence

### 6.1 Boot-Phase Ordering

Display initialization **must** occur after VMM is ready (framebuffer MMIO may lie above 1 GiB identity map) and **before** subsystems that log heavily (PCI scan, network init).

```text
kernel_main()
  │
  ├─ hal::uart_init()                    # Existing — serial first for early trap debug
  │
  ├─ memory::PhysicalMemoryManager::init()
  ├─ memory::VirtualMemoryManager::init()
  │
  ├─ hal::Display::init()                # NEW — detect backend, map FB
  │     ├─ Parse Multiboot2 FB tag (if present) → BootFramebuffer
  │     ├─ Else probe PCI for Bochs VGA (1234:1111 / 1af4:1111) → BochsVbe
  │     └─ Else fallback → VgaText (assume firmware left text mode active)
  │
  ├─ display::Console::init()            # NEW — wire kprintf targets
  │
  ├─ ... remaining subsystems ...
  └─ idle loop
```

### 6.2 Backend Selection Algorithm

```text
1. IF multiboot2_framebuffer.valid:
     USE BootFramebuffer (map phys → virt, no mode switch)
2. ELSE IF uefi_gop_info.valid:          # Future EFI stub
     USE BootFramebuffer
3. ELSE IF pci_find(0x1234, 0x1111) OR pci_find(0x1AF4, 0x1111):
     USE BochsVbe
       - Read DISPI ID, verify 0x4256
       - Program 1024×768×32, ENABLE linear
       - Map PCI BAR0 via VMM (WC or UC memory type)
4. ELSE:
     USE VgaText
       - Verify text buffer writable (write/read test at 0xB8000)
       - Clear screen (attr 0x07)
```

### 6.3 Framebuffer Memory Mapping

Use `memory::VirtualMemoryManager::map_page()` to map each 4 KiB page of the framebuffer region:

```text
Physical: PCI BAR0 + offset (from Bochs VBE or boot tag)
Virtual:  Fixed kernel window e.g. 0xFFFF8000_00000000 + offset (canonical high half)
Flags:    PAGE_PRESENT | PAGE_WRITABLE
          (Do NOT set PAGE_USER until userspace compositor exists)

Pages:    ceil(pitch × height / 4096), plus guard unmapped pages at end
```

**Cache attribute note:** For x86_64, mark framebuffer PTEs as **Write-Combining (WC)** via PAT or MTRR when available. v1 may use UC (uncacheable) for simplicity in QEMU; document WC as Phase 7.2 optimization.

---

## 7. Implementation Phases

### Phase 7.2a — VGA Text Mode Console

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2a.1** | Port I/O helpers (`inb`/`outb`, `inw`/`outw`) | `vga_regs.cpp` | Unit test: read Input Status `0x3DA` without fault |
| **7.2a.2** | Text buffer address calculation (row-major 160 bytes/row) | `vga_text.cpp` | Write `'A'` at (0,0), read back from `0xB8000` |
| **7.2a.3** | Cursor management (CRTC regs `0x0E`/`0x0F`) | `vga_text.cpp` | Cursor blinks at next character position |
| **7.2a.4** | Scroll (move rows 1–24 → 0–23, clear row 24) | `vga_text.cpp` | Fill 26 lines, verify top line scrolled off |
| **7.2a.5** | `display::Console` text backend + `kprintf` integration | `display_console.cpp`, `kprint.cpp` | Boot banner visible on QEMU `-vga std -display sdl` |
| **7.2a.6** | QEMU test script update | `scripts/test.sh` | CI grep or screenshot match for banner string |

**Exit criteria:** `kprintf("Hello Omega")` appears on serial **and** VGA text console in QEMU.

---

### Phase 7.2b — Bochs VBE Linear Framebuffer

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2b.1** | PCI probe for Bochs VGA; read BAR0 | `bochs_vbe.cpp` | Log BAR0 address in `kprintf` |
| **7.2b.2** | DISPI ID check and mode programming | `bochs_vbe.cpp` | XRES/YRES/BPP/ENABLE registers read back correctly |
| **7.2b.3** | Map framebuffer via VMM | `bochs_vbe.cpp`, `framebuffer.cpp` | Write 0x00FF0000 to pixel (0,0), read back |
| **7.2b.4** | Pixel plot + horizontal/vertical line | `framebuffer.cpp` | Draw red crosshair, visual check |
| **7.2b.5** | Embedded 8×16 bitmap font (ISO 8859-1 subset) | `font.cpp` | Render "Omega" string at (10,10) |
| **7.2b.6** | Graphical console (char grid overlay on FB) | `display_console.cpp` | 80×48 text grid on 1024×768 |
| **7.2b.7** | Dual output: FB primary, serial mirror | `kprint.cpp` | Same text on serial and FB |

**Exit criteria:** Graphical boot banner with colored text on 1024×768×32 FB in QEMU `-vga std`.

---

### Phase 7.2c — Bootloader Framebuffer Handoff

| Task ID | Task | Files | Verification |
| :---: | :--- | :--- | :--- |
| **7.2c.1** | Add Multiboot2 framebuffer request tag to `boot.s` | `boot.s` | GRUB `--info=multiboot2` shows tag |
| **7.2c.2** | Parse Multiboot2 info structure in early C++ | `boot_fb.cpp` | Log width×height×bpp from tag |
| **7.2c.3** | Prefer boot FB over Bochs programming | `display.hpp` init | GRUB boot uses handoff FB without mode switch |
| **7.2c.4** | RGB565 / RGB888 / XRGB8888 pixel format dispatch | `framebuffer.cpp` | Correct colors for each format |
| **7.2c.5** | Document GRUB boot command | `docs/RUNNING.md` | User can reproduce |

**Exit criteria:** Boot via GRUB with pre-set mode; Omega renders to supplied FB without touching DISPI registers.

---

### Phase 7.2d — Hardening & Polish

| Task ID | Task | Verification |
| :---: | :--- | :--- |
| **7.2d.1** | Vertical retrace wait before scroll (`0x3DA` bit 0) | No tearing on text scroll |
| **7.2d.2** | Spinlock for display output (SMP-safe when Phase 7.3 lands) | Concurrent `kprintf` from two threads |
| **7.2d.3** | ANSI escape subset ( `\033[2J` clear, `\033[H` home, `\033[3xm` color) | Shell compatibility baseline |
| **7.2d.4** | Fallback cascade unit tests | Each backend selectable via QEMU flags |
| **7.2d.5** | OVD emulator `--gpu` integration | `python3 -m emulator.ovd_cli start --gpu` shows graphical window |

---

## 8. Font & Rendering Specification

### 8.1 Bitmap Font

| Property | Value |
| :--- | :--- |
| Source | Public-domain **VGA 8×16** font (same glyph layout as PC BIOS) |
| Storage | `static const uint8_t font8x16[256][16]` in `font.cpp` |
| Character cell | 8 px wide × 16 px tall |
| FB text grid | `floor(width / 8)` × `floor(height / 16)` characters |

### 8.2 Pixel Format Handling

| Format | Condition | Pixel Write |
| :--- | :--- | :--- |
| **XRGB8888** | `bpp == 32`, boot tag type RGB | `*(uint32_t*) = 0xFFRRGGBB` |
| **RGB888** | 24 bpp packed | Write 3 bytes per pixel |
| **RGB565** | 16 bpp | Pack `(r>>3)<<11 \| (g>>2)<<5 \| (b>>3)` |
| **Indexed 8** | Legacy | Use fixed 256-color DAC palette table (VGA default) |

### 8.3 Blitting Primitives (v1)

```cpp
namespace display {
    void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void draw_string(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg);
    void blit_raw(const void* src, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
}
```

---

## 9. QEMU & CI Test Plan

### 9.1 QEMU Launch Matrix

| Config | Command Flags | Expected Backend |
| :--- | :--- | :--- |
| Standard VGA + SDL | `-vga std -display sdl` | BochsVbe |
| Standard VGA headless | `-vga std -display none` | BochsVbe (FB writes succeed, no window) |
| Serial only (regression) | `-display none` (no `-vga std`) | VgaText or None |
| GRUB Multiboot2 | GRUB `multiboot2` + FB tag | BootFramebuffer |

### 9.2 Automated Tests (`scripts/test_display.sh`)

```bash
# 1. Build x86_64 kernel
# 2. Boot QEMU with -vga std, serial to file
# 3. Assert serial log contains "[+] Display: BochsVbe 1024x768x32"
# 4. Optional: QEMU monitor 'screendump /tmp/screen.ppm' after 2s
# 5. Assert screendump contains non-zero pixel data in banner region
```

### 9.3 CI Integration (`.github/workflows/ci.yml`)

Add step after `scripts/test.sh`:

```yaml
- name: Run VGA Display Tests
  run: |
    chmod +x scripts/test_display.sh
    ./scripts/test_display.sh
```

---

## 10. Real Hardware Considerations (Phase 9 Preview)

### 10.1 Physical PC Boot Paths

| Firmware | Display Handoff | Omega Action |
| :--- | :--- | :--- |
| **UEFI laptop** | GOP active, VGA bus often disabled | Consume GOP info from EFI stub; skip DISPI |
| **Coreboot + SeaBIOS** | VBE mode or text mode | Multiboot2 FB tag or Bochs-like VBE |
| **Legacy BIOS** | VGA text only | Text mode console until VBE thunk added |

### 10.2 Known Hardware Quirks

- **Apple hardware (Boot Camp era):** No standard VGA on modern Macs — not applicable.
- **Optimus/hybrid graphics:** Framebuffer may be on discrete GPU PCI device; PCI scan must check all display class devices (`class 0x03`).
- **UEFI still owns FB after ExitBootServices:** Some firmwares require OS to re-negotiate; document as known limitation until EFI stub is complete.

### 10.3 PCI Class Code Detection

Extend `hal::PciBus` scan to identify display controllers:

```text
Class 0x03 (Display Controller)
  Subclass 0x00 — VGA compatible
  Subclass 0x01 — XGA
  Subclass 0x02 — 3D controller
```

Match vendor/device against a static table:

| Vendor:Device | Driver Backend |
| :--- | :--- |
| `0x1234:0x1111` | Bochs VBE |
| `0x1AF4:0x1111` | Bochs VBE (virtio-vga) |
| `0x8086:*` | Intel GOP handoff (Phase 9) |
| `0x1002:*` / `0x1022:*` | AMD (Phase 9+) |

---

## 11. Security & Safety

| Concern | Mitigation |
| :--- | :--- |
| **MMIO mapping to userland** | Framebuffer mapped kernel-only in v1; userland compositor gets read-only shared mapping via explicit syscall (Phase 8) |
| **Port I/O from userspace** | All VGA I/O restricted to kernel; future `displayd` server holds capability |
| **Frame buffer bounds** | All draw calls validate `x + w ≤ width`, `y + h ≤ height` |
| **Denial of service (scroll flood)** | Rate-limit scroll in graphical console; bounded scrollback buffer (e.g. 256 lines × 80 cols in kmalloc heap) |
| **SMP races** | `display_lock` spinlock around all console mutations |

---

## 12. Future Evolution Path

```text
Phase 7.2 (this plan)
  └─ Kernel VGA text + Bochs VBE + boot FB console

Phase 9.2
  └─ Userspace displayd server
  └─ Intel/AMD GOP native mode setting
  └─ Double-buffering + vsync IRQ

Phase 10A.1
  └─ Wayland-style compositor on top of FB
  └─ Hardware cursor, multi-head

Phase 10B (Tablet)
  └─ Separate HAL backend (DSI/MIPI) — not VGA
```

---

## 13. Milestone Summary

| Milestone | Deliverable | Target |
| :--- | :--- | :--- |
| **M1** | VGA text mode + serial mirrored `kprintf` | Phase 7.2a |
| **M2** | 1024×768×32 graphical console with font | Phase 7.2b |
| **M3** | Multiboot2 / GRUB framebuffer handoff | Phase 7.2c |
| **M4** | CI display tests + OVD `--gpu` support | Phase 7.2d |
| **M5** | UEFI GOP via EFI stub (separate follow-on) | Phase 9 |

---

## 14. References

| Resource | URL / Location |
| :--- | :--- |
| OSDev Wiki — VGA Hardware | https://wiki.osdev.org/VGA_Hardware |
| OSDev Wiki — Bochs VBE Extensions | https://wiki.osdev.org/Bochs_VBE_Extensions |
| OSDev Wiki — Multiboot2 Framebuffer Tag | https://wiki.osdev.org/Multiboot2#Framebuffer_tag |
| PCI Local Bus Specification (Class Codes) | PCI SIG |
| Omega VMM API | `kernel/include/kernel/vmm.hpp` |
| Omega PCI Scanner | `kernel/arch/x86_64/pci.cpp` |
| Omega Roadmap Phase 7.2 | `docs/ROADMAP.md` |
| AArch64 / RISC-V SDM extension | `docs/DISPLAY_AARCH64_RISCV_PLAN.md` |

---

## Appendix A: VGA Text Mode Attribute Byte

```text
Bit 7:     Blink (text mode) / Bright background (some modes)
Bit 6–4:   Background color (0–7)
Bit 3:     Foreground bright
Bit 2–0:   Foreground color (0–7)

Standard colors:
  0 Black    4 Red
  1 Blue     5 Magenta
  2 Green    6 Brown/Yellow
  3 Cyan     7 Light Gray/White
```

## Appendix B: Sample DISPI Mode-Set Sequence (Bochs VBE)

```text
1. OUT index 0x04 (ENABLE), data 0x00        — disable display
2. OUT index 0x01 (XRES),  data width
3. OUT index 0x02 (YRES),  data height
4. OUT index 0x03 (BPP),    data 32
5. OUT index 0x08 (VIRT_WIDTH), data width
6. OUT index 0x04 (ENABLE), data 0x01 | 0x02 — enable + linear FB
7. Map PCI BAR0, write test pixel
```

## Appendix C: Multiboot2 Framebuffer Request Tag (Add to `boot.s`)

```text
Align 8
Tag type:   5 (Framebuffer)
Tag flags:  0
Tag size:   20
Width:      1024
Height:     768
Depth:      32
```
