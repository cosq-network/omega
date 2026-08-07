# Omega Real-Hardware Validation Matrix

## 1. Purpose

This document defines a practical hardware-validation program for Omega across
x86_64, AArch64, and RISC-V 64. It expands the low-cost board recommendations
into a broader platform strategy covering:

- single-board computers and development boards;
- mobile phones and tablets;
- laptops and desktops;
- display, storage, USB, serial, Ethernet, Wi-Fi, and pointing-device paths;
- firmware, bootloader, recovery, debugging, and test instrumentation;
- staged bring-up from a serial-only kernel to a complete graphical system.

The recommended hardware is intended for engineering validation, not as a
promise that Omega currently boots or supports every listed device. Each
platform requires a board-support package, boot handoff validation, hardware
resource description, and an explicit driver milestone.

## 2. Selection principles

Select hardware using these criteria, in order:

1. **Recoverability:** removable storage, documented recovery mode, and a
   way to reflash without permanently damaging the board.
2. **Debug access:** exposed UART, USB serial/debug, JTAG, or a reliable
   bootloader console.
3. **Open boot path:** UEFI, documented firmware, U-Boot, Tow-Boot, or a
   reproducible vendor boot flow.
4. **Resource visibility:** ACPI/FDT descriptions for memory, interrupts,
   clocks, reset, GPIO, regulators, buses, and display.
5. **Peripheral breadth:** PCIe, USB 2/3, Ethernet, storage, I²C, SPI, GPIO,
   and an accessible display connector or panel.
6. **Availability and replaceability:** multiple suppliers, active community,
   and hardware that can be purchased repeatedly.
7. **Thermal and power practicality:** standard power input, documented
   cooling, and enough sustained performance for kernel builds and tests.
8. **Legal and firmware sustainability:** redistributable documentation and
   a manageable dependency on proprietary blobs.

## 3. Recommended first hardware set

The smallest useful cross-architecture lab is:

| Architecture | First choice | Backup choice | Primary purpose |
| --- | --- | --- | --- |
| x86_64 | ODROID-H4/H4+ | Intel NUC8i5BEK/BEH | UEFI/GOP, PCIe, NVMe, Ethernet, USB, display |
| AArch64 | Raspberry Pi 5 4 GB | Orange Pi 5 8 GB | FDT, UART, SD, USB, Ethernet, framebuffer, GPIO |
| RISC-V 64 | StarFive VisionFive 2 8 GB | Banana Pi BPI-F3 8 GB | OpenSBI, FDT, PLIC, UART, storage, Ethernet, HDMI |

Buy the backup only after the first board reaches serial boot. This avoids
spending on three boards before the boot contract and instrumentation are
stable.

## 3A. India purchase links and indicative price bands

The following price bands are indicative India-market ranges checked on
2026-08-07. They are not quotes or guarantees. Prices vary significantly with
RAM size, storage, cooling, power supply, import duty, GST, shipping, stock,
and whether the seller is an authorized distributor. Confirm the final
checkout price, warranty, board revision, and included accessories before
ordering.

“Board only” excludes power, cooling, storage, case, cables, and display
accessories. “Landed” means a realistic imported or bundled purchase estimate
including common accessories, but not necessarily customs charges.

| Target | India purchase link | Indicative price range | Availability note |
| --- | --- | ---: | --- |
| ODROID-H4 | [ElectroPi India](https://www.electropi.in/odroid-h4-single-board-computer) | ₹20,000–₹35,000 board only; ₹28,000–₹45,000 equipped | Indian stock/importer availability varies; verify whether the listing is H4, H4+, or H4 Ultra |
| Intel NUC8i5BEK/BEH | [Amazon India search](https://www.amazon.in/s?k=Intel+NUC8i5BEK) or [used-market search](https://www.google.com/search?q=Intel+NUC8i5BEK+India+used) | ₹15,000–₹35,000 used, depending on RAM/SSD | Usually second-hand; inspect BIOS password, power adapter, SSD health, and display ports |
| HP EliteDesk 800 G3 Mini/SFF | [HP/Amazon India search](https://www.amazon.in/s?k=HP+EliteDesk+800+G3+Mini) | ₹8,000–₹22,000 used | Confirm exact CPU, RAM, storage, DisplayPort/serial options, and Windows/BIOS lock state |
| Raspberry Pi 5 4 GB | [Robu India](https://robu.in/product/raspberry-pi-5-model-4gb/) | ₹8,000–₹16,000 board only; ₹12,000–₹22,000 equipped | Authorized-channel stock can change quickly; the official cooler and 27 W USB-C supply are recommended |
| Raspberry Pi 5 8 GB | [Robocraze India](https://robocraze.com/products/raspberry-pi-5-8gb) or [Robu India](https://robu.in/product/raspberry-pi-5-model-8gb/) | ₹12,000–₹25,000 board only; ₹17,000–₹32,000 equipped | Compare authorized sellers; avoid listings that omit warranty or use unclear RAM variants |
| Orange Pi 5 8 GB | [Amazon India search](https://www.amazon.in/s?k=Orange+Pi+5+8GB) or [official product documentation](https://orangepi.net/wp-content/uploads/2023/05/OrangePi_5_RK3588S_User-Manual_v1.5.pdf) | ₹14,000–₹28,000 landed; ₹20,000–₹38,000 with NVMe/power/cooling | Often imported; check RK3588S model, RAM, eMMC option, power supply, and customs |
| BeaglePlay | [BeagleBoard India/global purchase page](https://www.beagleboard.org/boards/beagleplay) | ₹12,000–₹25,000 landed | Availability is distributor-dependent; confirm AM625 revision and included wireless/headers |
| StarFive VisionFive 2 8 GB | [Waveshare purchase page](https://www.waveshare.com/visionfive2.htm) | ₹18,000–₹35,000 landed; ₹25,000–₹45,000 with eMMC/PSU/cables | Usually imported; verify 8 GB revision, boot flash, power adapter, and India delivery |
| Banana Pi BPI-F3 8 GB | [Official BPI-F3 page/shop link](https://docs.banana-pi.org/en/BPI-F3/BananaPi_BPI-F3) | ₹18,000–₹35,000 landed; ₹25,000–₹45,000 equipped | Newer RISC-V platform; verify RAM/eMMC/Wi-Fi option and firmware support before purchase |
| Milk-V Mars | [Milk-V Mars documentation/store links](https://milkv.io/docs/mars/overview) | ₹10,000–₹22,000 landed | Stock is less predictable; buy only when board revision and shipping are confirmed |
| PinePhone Pro | [PINE64 product/store page](https://pine64.org/devices/pinephone_pro/) | ₹35,000–₹65,000 landed | Import-dependent; add battery, shipping, duty, and possible keyboard/accessory costs |
| PineTab2 4/8 GB | [PINE64 product/store page](https://pine64.org/devices/pinetab2/) | ₹25,000–₹50,000 landed | Import-dependent; keyboard is commonly part of the tablet package, but verify the listing |
| Fairphone 5 | [Fairphone purchase page](https://www.fairphone.com/) | ₹45,000–₹75,000 if imported | Region, modem bands, warranty, and bootloader-unlock eligibility must be checked |
| Framework Laptop 13 | [Framework Laptop 13](https://frame.work/laptop13?tab=linux) | ₹90,000–₹1,80,000 landed/configured | India availability and warranty can differ from the official order region; import before buying |
| Refurbished business laptop | [Amazon India search](https://www.amazon.in/s?k=refurbished+ThinkPad+T480) | ₹18,000–₹45,000 | Prefer models with UEFI, replaceable NVMe/SATA, HDMI/DP, and a documented Linux profile |
| Refurbished desktop | [Amazon India search](https://www.amazon.in/s?k=refurbished+Dell+OptiPlex+desktop) | ₹10,000–₹35,000 | Confirm PSU, PCIe slots, DisplayPort/HDMI, BIOS password status, and included power cable |

For the three initial targets, a realistic India budget is approximately:

```text
ODROID-H4 / NUC8 + accessories:        ₹25,000–₹45,000
Raspberry Pi 5 4 GB + accessories:     ₹12,000–₹22,000
VisionFive 2 8 GB + accessories:       ₹25,000–₹45,000
```

This excludes a monitor, test host, Ethernet switch, USB-UART adapters, and
instrumentation. A complete first lab should budget approximately ₹70,000–₹1,35,000,
depending mainly on whether the x86 and RISC-V systems are bought used or
imported.

### 3A.1 Safer Indian purchasing order

1. Buy Raspberry Pi 5 from an authorized Indian distributor first.
2. Buy a refurbished Intel NUC/EliteDesk locally, or ODROID-H4 if new-board
   availability is confirmed.
3. Buy VisionFive 2 from a seller that provides India shipping and a clear
   return policy.
4. Add Orange Pi 5 or BPI-F3 only after the first three platforms are capable
   of serial recovery.
5. Treat Pine64, Milk-V, Framework, and Fairphone products as import or
   region-dependent purchases and verify warranty/bootloader conditions first.

### 3A.2 Price-verification checklist

Before paying, record:

- seller name, product URL, and date/time;
- exact board/model/revision and RAM size;
- whether power supply, cooler, case, storage, and antenna are included;
- GST, shipping, customs, and import charges;
- warranty and return terms in India;
- bootloader-lock and recovery implications;
- expected delivery time and replacement availability.

## 4. x86_64 hardware recommendations

### 4.1 ODROID-H4/H4+

The ODROID-H4 family is the preferred new x86_64 development board. It uses an
Intel N97-class processor, supports DDR5 SODIMM memory, PCIe/NVMe, Ethernet,
USB, display output, and board-level storage options. The official product
information also describes dual-BIOS options, which is valuable for recovery.

Recommended validation:

- UEFI boot and framebuffer/GOP handoff;
- serial-over-USB debug during early bring-up;
- PCI enumeration and BAR mapping;
- NVMe and SATA storage;
- USB 2/3 and Type-C behavior;
- Ethernet and Wi-Fi modules;
- 1920×1080 display output through HDMI/DP adapters;
- suspend/resume and reset recovery.

Reference: [ODROID-H4 specifications](https://www.odroid.co.uk/odroid-h4-series/odroid-h4).

### 4.2 Intel NUC8i5BEK/BEH

The NUC8 is a good used-market x86_64 target. It provides conventional Intel
UEFI, integrated graphics, HDMI, USB 3, USB-C/DisplayPort, Ethernet, NVMe,
and a well-understood PC platform. It is useful for validating the difference
between QEMU VGA and firmware-provided GOP framebuffers.

Recommended validation:

- UEFI boot entry and removable-media boot;
- GOP framebuffer at multiple firmware-selected modes;
- ACPI tables and PCIe discovery;
- Intel integrated display handoff without a native GPU driver;
- USB HID, USB storage, Ethernet, and Wi-Fi modules;
- real laptop-style power states where firmware permits.

Reference: [Intel NUC8 user guide](https://www.intel.com/content/dam/support/us/en/documents/mini-pcs/nuc-kits/NUC8ixBEK_UserGuide.pdf).

### 4.3 HP EliteDesk 800 G3 Mini/SFF

This is a practical refurbished-business-PC target. It is useful when serial
access matters because some configurations expose an optional serial port. It
also provides DisplayPort, USB, Ethernet, UEFI, replaceable storage, and a
real desktop PCI/firmware environment.

Recommended validation:

- serial console and firmware setup;
- UEFI/GOP and DisplayPort output;
- legacy BIOS fallback where available;
- SATA/NVMe, USB, Ethernet, and PCI expansion;
- device enumeration under a less controlled OEM firmware configuration.

Reference: [HP EliteDesk specifications](https://support.hp.com/us-en/product/product-specs/hp-elitedesk-800-g3-small-form-factor-pc/model/15257619).

## 5. AArch64 hardware recommendations

### 5.1 Raspberry Pi 5

Raspberry Pi 5 is the primary AArch64 board because it is widely known,
relatively easy to replace, has a documented boot ecosystem, and offers USB,
PCIe, Ethernet, GPIO, UART, I²C, SPI, storage, and dual display output. The
official platform description identifies dual 4Kp60 HDMI output and a
high-speed microSD interface.

Recommended validation:

- AArch64 boot handoff and FDT parsing;
- PL011 or board UART console;
- microSD and USB storage;
- USB 2/3, USB HID, and pointing devices;
- Ethernet and Wi-Fi;
- GPIO, I²C, SPI, and interrupt routing;
- framebuffer display and 1920×1080 output;
- PCIe/NVMe and cache/DMA behavior;
- thermal throttling and reboot behavior.

Reference: [Raspberry Pi 5 product information](https://www.raspberrypi.com/news/introducing-raspberry-pi-5/).

### 5.2 Orange Pi 5

Orange Pi 5 is the higher-performance AArch64 option. Its RK3588S platform
provides multiple Cortex-A cores, USB 2/3, Gigabit Ethernet, HDMI, Type-C,
M.2 PCIe, microSD, GPIO, and high-resolution display capabilities. It is a
strong candidate for validating large framebuffers, NVMe, high-speed USB, and
network concurrency.

Recommended validation:

- Rockchip bootloader and FDT resource handoff;
- UART, GPIO, I²C, SPI, and regulator dependencies;
- M.2 NVMe and USB storage;
- USB 3 and Type-C;
- HDMI display and 1920×1080 scanout;
- Ethernet, Wi-Fi, and DMA;
- panel/display-controller work as a later board-specific milestone.

Reference: [Orange Pi 5 hardware manual](https://orangepi.net/wp-content/uploads/2023/05/OrangePi_5_RK3588S_User-Manual_v1.5.pdf).

### 5.3 BeaglePlay

BeaglePlay is a useful embedded-I/O AArch64 platform rather than a high-end
desktop board. It provides a Texas Instruments AM625-based quad Cortex-A53
platform, HDMI, Ethernet, microSD, USB, UART, I²C, SPI, GPIO, and wireless
expansion. It is particularly valuable for testing platform-resource
descriptions, external sensors, serial devices, and low-power behavior.

Recommended validation:

- FDT and TI platform resource parsing;
- UART, GPIO, I²C, SPI, regulators, and interrupt controllers;
- Ethernet and USB 2;
- HDMI framebuffer handoff;
- touch controller and pointing-device prototypes;
- suspend/resume and power-domain sequencing.

Reference: [BeaglePlay documentation](https://docs.beagleboard.org/boards/beagleplay/index.html).

## 6. RISC-V 64 hardware recommendations

### 6.1 StarFive VisionFive 2

VisionFive 2 is the recommended first RISC-V 64 board. It provides an RV64GC
JH7110, TF-card boot, U-Boot/firmware storage, UART/GPIO, I²C, SPI, USB 3,
dual Gigabit Ethernet, M.2, HDMI, and MIPI DSI. Its HDMI path supports up to
4K@30 or 2K@60, and its display documentation includes 1920×1080 examples.

Recommended validation:

- OpenSBI to S-mode handoff;
- FDT pointer and resource parsing;
- UART, CLINT, PLIC, timer, and interrupt routing;
- TF-card and eMMC storage;
- USB 3, Ethernet, M.2, and PCIe;
- HDMI 1920×1080 display;
- GPIO/I²C/SPI and future touch-controller work;
- RISC-V cache and DMA barriers.

References: [VisionFive 2 specifications](https://doc-en.rvspace.org/VisionFive2/Product_Brief/VisionFive_2/specification_pb.html) and [VisionFive display examples](https://doc-en.rvspace.org/VisionFive2/DG_Display/JH7110_SDK/test_example_display.html).

### 6.2 Banana Pi BPI-F3

BPI-F3 is the more modern, feature-rich RISC-V option. It uses the SpacemiT
K1 octa-core RV64 platform and provides USB 3, PCIe, dual Gigabit Ethernet,
Wi-Fi, microSD, eMMC, multiple UARTs, HDMI, and MIPI DSI. Its documented
display capability reaches 1080p60, making it relevant to Omega’s display and
communications goals.

Recommended validation:

- RV64GCVB/RVA22 boot and toolchain behavior;
- OpenSBI/FDT/PLIC/CLINT integration;
- multiple UARTs and serial-device registry;
- USB 3, Wi-Fi, dual Ethernet, PCIe, and storage;
- HDMI 1920×1080@60 testing;
- MIPI DSI and touchscreen work at a later stage;
- power, thermal, and firmware recovery.

References: [BPI-F3 product documentation](https://docs.banana-pi.org/en/BPI-F3/BananaPi_BPI-F3) and [BPI-F3 getting started](https://docs.banana-pi.org/en/BPI-F3/GettingStarted_BPI-F3).

### 6.3 Milk-V Mars

Milk-V Mars is a compact JH7110-based RV64 alternative. It is suitable as a
backup for VisionFive-style development when available through a regional
distributor. It should be treated as a conditional purchase because stock and
revision availability can change more rapidly than Raspberry Pi or business
PC hardware.

Reference: [Milk-V Mars documentation](https://milkv.io/docs/mars/overview).

Avoid selecting the Milk-V Duo as the primary Omega RISC-V target: it is an
excellent low-cost embedded board, but it is not equivalent to the RV64
general-purpose platform required by the current Omega roadmap.

## 7. Mobile-phone validation

Mobile phones should be a later AArch64 product track, not the first physical
bring-up target. They add secure boot chains, signed firmware, modem firmware,
power domains, battery charging, touchscreen/display controllers, cameras,
thermal management, and vendor-specific boot restrictions.

### 7.1 PinePhone Pro

The PinePhone Pro is the preferred experimental mobile target for open-system
work. It uses an RK3399S 64-bit SoC, exposes a 1440×720 panel, USB-C with USB
3 and DisplayPort alternate mode, microSD, eMMC, Wi-Fi/Bluetooth, modem,
camera, sensors, and UART access through the audio connector. Its documented
boot order includes SPI, eMMC, and microSD, which is valuable for recovery.

Recommended uses:

- AArch64 mobile boot and recovery;
- framebuffer and touchscreen integration;
- USB-C/DP and Type-C policy experiments;
- battery, suspend, wake, GPIO, I²C, and sensor work;
- storage and modem isolation experiments.

Reference: [PinePhone Pro specifications](https://pine64.org/devices/pinephone_pro/) and [PinePhone Pro boot documentation](https://pine64.org/documentation/PinePhone_Pro/_full/).

It should not be used as the first target for a production phone because its
cellular, camera, GPU, and power-management ecosystem is intentionally more
experimental than mainstream Android hardware.

### 7.2 Fairphone 5 or later unlockable Fairphone

Fairphone is a useful mainstream-phone validation candidate when an exact
model, region, and carrier configuration can be verified. Fairphone documents
bootloader unlocking for supported models, including Fairphone 5. It is more
representative of a repairable consumer phone than PinePhone, but its modem,
GPU, display, camera, and power controllers remain vendor-specific.

Reference: [Fairphone bootloader unlocking](https://www.fairphone.com/bootloader-unlocking-code-for-fairphone) and [Fairphone support guidance](https://support.fairphone.com/hc/en-us/articles/10492476238865-How-to-unlock-and-re-lock-the-bootloader).

Use it for:

- bootloader and recovery validation;
- USB-C, storage, display, touchscreen, and input policy;
- power and suspend/resume experiments;
- only later, modem and secure-world integration.

Never buy a phone for Omega without confirming that its bootloader can be
unlocked in the intended sales region and that relocking/recovery is possible.

## 8. Tablet validation

### 8.1 PineTab2

PineTab2 is a good open tablet target for touch-first AArch64 work. It uses an
RK3566 64-bit Arm SoC and is designed around Linux-oriented development. It
should be used for touchscreen, touchpad/keyboard dock, display rotation,
calibration, suspend/wake, USB-C, and low-power input testing.

Reference: [PineTab2 product page](https://pine64.org/devices/pinetab2/) and [PineTab2 documentation](https://pine64.org/documentation/PineTab2/_full/).

### 8.2 Raspberry Pi-based tablet prototypes

A Raspberry Pi 5 combined with a supported HDMI or DSI touchscreen is a more
repeatable tablet-like validation platform than an OEM tablet. It provides
replaceable components, serial recovery, visible GPIO/I²C/SPI wiring, and an
easy path to test Omega’s pointing-device and display plans before dealing
with sealed-device firmware.

Test targets:

- I²C-HID or USB touchscreen;
- calibration and orientation;
- touch-to-display coordinate mapping;
- keyboard/touchpad dock;
- suspend/resume and battery simulation;
- USB-C power/data separation.

## 9. Laptop validation

### 9.1 Framework Laptop 13

The Framework Laptop 13 is the preferred laptop validation platform when the
budget permits. Its modular design, replaceable storage and memory, multiple
USB-C expansion options, documented Linux compatibility, and UEFI-based PC
architecture make it suitable for driver development.

Recommended variants:

- used 11th/12th/13th-generation Intel model for Intel integrated graphics;
- AMD Ryzen 7040-series model for AMD x86_64 and modern PCIe testing;
- a current model only after checking firmware and Linux/Omega development
  requirements.

Reference: [Framework Laptop Linux information](https://frame.work/linux) and [Framework Laptop 13 AMD information](https://frame.work/laptop13?tab=linux).

Validate:

- UEFI/GOP and native panel resolution;
- internal NVMe and removable expansion;
- USB-C, USB 3, DisplayPort Alt Mode, and hotplug;
- touchpad, keyboard, webcam, audio, Ethernet adapters, and Wi-Fi;
- suspend/resume, battery, thermal, and lid events;
- ACPI tables, EC interactions, and power management.

### 9.2 Refurbished business laptops

Low-cost ThinkPad, Dell Latitude, and HP EliteBook models are useful for
repeatable x86_64 laptop testing. Prefer models with:

- documented UEFI settings;
- replaceable NVMe/SATA storage;
- accessible memory;
- an internal Ethernet adapter or reliable USB Ethernet;
- HDMI/DisplayPort;
- a physical serial/debug option or an easy USB-UART path;
- a known-good Linux hardware profile.

Use these systems to test broad ACPI and firmware variation, not as the first
platform for a native GPU driver.

### 9.3 AArch64 laptops

AArch64 laptops should be a later target. Select models with UEFI or a
documented boot chain and a community-supported Linux path. Avoid making
proprietary Snapdragon or Apple laptop firmware the first AArch64 hardware
target; they are valuable later but significantly increase firmware, GPU,
power, and boot complexity.

## 10. Desktop validation

### 10.1 Refurbished x86 desktop

HP EliteDesk, Dell OptiPlex, and Lenovo ThinkCentre systems are excellent
low-cost desktop targets. Choose a model with standard UEFI, DisplayPort or
HDMI, PCIe expansion, NVMe/SATA, USB 3, Ethernet, and a replaceable power
supply where possible.

Use desktops to test:

- PCIe enumeration and BARs;
- NVMe, SATA, and USB storage;
- Intel/AMD Ethernet;
- multiple monitors and EDID;
- PCIe Wi-Fi adapters;
- hotplug and reset behavior;
- UEFI/GOP versus native GPU-driver boundaries.

### 10.2 Custom desktop with an older supported GPU

For later native display work, build a desktop with an older, well-documented
PCIe GPU and an integrated graphics fallback. Do not begin with a new
high-end GPU whose firmware, memory management, and display stack require a
large proprietary driver effort.

Recommended engineering properties:

- two independent display outputs;
- PCIe reset visibility;
- serial console through a motherboard header or add-in card;
- standard ATX power and cooling;
- replaceable RAM and storage;
- a second machine for recovery and serial capture.

## 11. Device-category test matrix

| Capability | SBC | Phone | Tablet | Laptop | Desktop |
| --- | ---: | ---: | ---: | ---: | ---: |
| Early UART | Required | Required where exposed | Required where exposed | USB/serial adapter | Header/serial adapter |
| UEFI/GOP | Optional | Usually absent/vendor-specific | Sometimes absent | Required for x86 target | Required |
| FDT | Primary on many boards | Often vendor-specific | Often vendor-specific | Optional | Optional |
| USB 2 | Required | Required | Required | Required | Required |
| USB 3/Type-C | Preferred | Required | Required | Required | Preferred |
| Ethernet | Preferred | Usually adapter/dock | Preferred | Preferred | Required |
| Wi-Fi | Preferred | Required | Required | Required | Optional PCIe/USB |
| NVMe/PCIe | Preferred | Often constrained | Optional | Required | Required |
| Touchscreen | Optional | Required | Required | Optional | Optional |
| Touchpad | Optional | Optional | Dock-dependent | Required | Optional |
| Display output | HDMI/DSI | MIPI/DP Alt Mode | MIPI/HDMI/USB-C | eDP/HDMI/DP | HDMI/DP |
| Recovery path | SD/U-Boot | SD/bootloader/recovery | SD/bootloader | removable USB/UEFI | removable USB/UEFI |

## 12. Hardware bring-up sequence

Every new device should follow the same sequence:

1. Record board revision, SoC, memory, boot media, firmware version, and
   peripherals.
2. Connect serial capture before changing firmware.
3. Boot the vendor image and save its FDT/ACPI tables and boot log.
4. Confirm recovery and return-to-vendor-image procedures.
5. Boot a minimal Omega image with serial output only.
6. Validate memory map, exception vectors, timer, interrupt controller, and
   scheduler.
7. Add storage and preserve a known-good recovery image.
8. Add display framebuffer handoff or native display controller.
9. Add USB and pointing devices.
10. Add Ethernet, Wi-Fi, and other communications.
11. Exercise suspend, reset, hotplug, thermal, and power-failure paths.
12. Record the board as supported, experimental, or blocked with evidence.

## 13. Required instrumentation

The lab should maintain:

- at least one 3.3 V USB-UART adapter per active board;
- a powered USB hub;
- reliable microSD/eMMC programming tools;
- HDMI and DisplayPort cables/adapters;
- a 1920×1080 monitor;
- Ethernet switch and isolated test network;
- USB protocol analyzer or logic analyzer when USB development begins;
- I²C/SPI-capable logic analyzer for touch and sensor work;
- programmable power measurement for mobile/tablet work;
- spare cooling fans and known-good power supplies;
- a recovery host that can capture serial output and reflash storage.

Never connect a 5 V UART signal to a 3.3 V board header. Verify pinout,
voltage, ground, and direction before powering the target.

## 14. Display and resolution validation

For real hardware, resolution is negotiated with firmware, a panel, a display
controller, or a GPU. It is not guaranteed solely by framebuffer memory.

The minimum display test set is:

- `640×480` fallback;
- `800×600` compatibility mode;
- `1024×768` baseline graphical console;
- `1280×720` widescreen mode;
- `1280×800` laptop/tablet mode;
- `1920×1080` preferred desktop/mobile external-display mode.

For each mode record:

- connector/panel identity;
- refresh rate;
- pixel format and channel order;
- pitch and framebuffer size;
- EDID or panel timing source;
- whether mode was firmware-selected or kernel-programmed;
- display hotplug and recovery behavior.

`1920×1080×32` requires at least `8,294,400` bytes before alignment and
additional buffers. The kernel must validate the actual pitch rather than
assuming `width × 4`.

## 15. Support classification

Every platform must be assigned one status:

| Status | Meaning |
| --- | --- |
| `planned` | Hardware selected but no bring-up work completed |
| `serial-boot` | Omega reaches serial initialization reliably |
| `kernel-smoke` | Memory, interrupts, timer, scheduler, and recovery pass |
| `framebuffer` | Firmware or native framebuffer output works |
| `peripheral` | Storage, USB, Ethernet, and selected input paths work |
| `experimental` | Major paths work but recovery/power or hardware coverage remains |
| `supported` | Documented reproducible build, boot, test, recovery, and release process |
| `blocked` | Required firmware, documentation, hardware, or driver access is unavailable |

Do not mark a board supported solely because a vendor Linux image boots.

## 16. Initial Omega hardware milestones

| Milestone | Platforms | Scope |
| --- | --- | --- |
| H0 | QEMU all ISAs | Cross-architecture contracts and deterministic regression tests |
| H1 | ODROID-H4 or NUC8 | x86_64 UEFI/GOP, serial, PCI, NVMe, USB, Ethernet, display |
| H2 | Raspberry Pi 5 | AArch64 FDT, UART, SD, USB, Ethernet, framebuffer |
| H3 | VisionFive 2 | RISC-V OpenSBI, FDT, PLIC, UART, storage, Ethernet, HDMI |
| H4 | Orange Pi 5 / BPI-F3 | High-speed USB, PCIe/NVMe, Wi-Fi, 1080p display, Type-C |
| H5 | PineTab2 / PinePhone Pro | Touchscreen, sensors, battery, suspend, mobile display/input |
| H6 | Framework/EliteDesk | Laptop/desktop ACPI, UEFI, touchpad, power, hotplug, multiple displays |
| H7 | Fairphone or another unlockable phone | Mainstream mobile boot, display, input, USB-C, power, and security review |

## 17. Purchasing and risk guidance

- Confirm board revision before buying; the same product name may hide
  different SoC, RAM, display, or boot behavior.
- Prefer official stores or known distributors for the first unit.
- Buy a second unit only after confirming that the first unit can be reflashed
  and recovered.
- Budget for power, cooling, storage, cables, and serial hardware; these often
  exceed the bare-board price.
- Avoid sealed phones/tablets with irreversible bootloader locks for early
  kernel development.
- Treat vendor GPU, modem, camera, and Wi-Fi firmware as separate risks.
- Keep a vendor OS image and checksum for every target.
- Record regional restrictions, warranty implications, and bootloader unlock
  requirements before modifying consumer devices.

## 18. Final recommendation

For the first physical Omega lab, use:

```text
x86_64   ODROID-H4 or used Intel NUC8i5BEK
AArch64  Raspberry Pi 5 4 GB
RISC-V   StarFive VisionFive 2 8 GB
```

Then expand with:

```text
AArch64  Orange Pi 5 8 GB and PineTab2
RISC-V   Banana Pi BPI-F3 8 GB
Mobile   PinePhone Pro, then an unlockable Fairphone model
Laptop   Framework Laptop 13
Desktop  refurbished HP EliteDesk/Dell OptiPlex/ThinkCentre
```

This ordering provides the fastest path from QEMU to reproducible real-device
boot, then progressively introduces high-speed I/O, display controllers,
touch, battery, firmware, ACPI, and consumer-device security complexity.

## 19. Related Omega documentation

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md)
- [`docs/ROADMAP.md`](ROADMAP.md)
- [`docs/VGA_DISPLAY_PLAN.md`](VGA_DISPLAY_PLAN.md)
- [`docs/DISPLAY_AARCH64_RISCV_PLAN.md`](DISPLAY_AARCH64_RISCV_PLAN.md)
- [`docs/COMMUNICATIONS_INTEGRATION_PLAN.md`](COMMUNICATIONS_INTEGRATION_PLAN.md)
- [`docs/POINTING_DEVICES_INTEGRATION_PLAN.md`](POINTING_DEVICES_INTEGRATION_PLAN.md)
- [`docs/STORAGE_ARCHITECTURE_PLAN.md`](STORAGE_ARCHITECTURE_PLAN.md)
- [`emulator/README.md`](../emulator/README.md)
- [`scripts/README.md`](../scripts/README.md)
