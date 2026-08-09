# Omega Keyboard, HID, and Pointing Devices Integration Plan

## Implementation status

The first input slice is now implemented. `kernel/include/kernel/input.hpp`
defines the versioned 64-byte event ABI and bounded queue; boot keyboard and
mouse HID report decoders are portable across all three target architectures;
x86_64 has a polling PS/2 keyboard/mouse backend; and AArch64/RISC-V have
portable synthetic/HID-ready adapters. `SYS_INPUT_READ`, `SYS_INPUT_POLL`, and
`SYS_INPUT_SUBSCRIBE` are reserved in the common Omega syscall extension range.
USB/xHCI transport, native non-x86 interrupt delivery, hotplug, and userspace
`inputd` remain subsequent milestones.

## 1. Purpose and scope

This document defines the plan for supporting pointing and touch-input
devices in Omega:

- USB and Bluetooth-style HID mice;
- legacy PS/2 mice and touchpads;
- internal I²C-HID and SPI touchpads;
- USB and embedded capacitive touchscreens;
- multi-touch contacts, gestures, buttons, wheels, styluses, and trackpoints;
- emulated devices exposed by QEMU and the Omega Virtual Device emulator.

The implementation will provide one generic input model above several
transport drivers. A mouse is not defined by USB, a touchpad is not defined by
I²C, and a touchscreen is not defined by a particular vendor controller. The
transport discovers and moves reports; HID and device-specific protocol code
decodes them; the input service applies calibration, coordinate policy,
gestures, permissions, and desktop/mobile behavior.

The remaining sections describe the staged expansion from this foundation to
complete USB, touch, hotplug, and userspace input support.

## 2. Goals and non-goals

### Goals

1. Support relative pointing, absolute touch, multi-touch, buttons, wheels,
   gestures, and basic stylus/tool identity through a common API.
2. Reuse the communications USB/HID architecture rather than creating a
   mouse-specific USB stack.
3. Support x86_64, AArch64, and RISC-V through architecture-neutral input
   code and small platform adapters.
4. Make hotplug, suspend/resume, device removal, malformed reports, and
   controller reset safe and observable.
5. Provide deterministic host and QEMU tests before requiring physical
   hardware.
6. Preserve a userspace migration boundary so policy and gesture processing
   can later run in an isolated `inputd` service.

### Non-goals for the first implementation

- full pen/active-digitizer feature parity;
- haptic/force-feedback certification;
- vendor-specific gesture engines for every touchpad;
- camera-based or external optical touch systems;
- Bluetooth controller support before the generic HID input API is stable;
- compositor-specific event formats leaking into kernel drivers.

## 3. Standards and protocol families

| Device/path | Primary protocol boundary | Initial implementation |
| --- | --- | --- |
| USB mouse | USB HID class, HID report descriptor, HID Usage Tables | Boot-protocol mouse plus report-protocol parser |
| USB touchpad | USB HID, Digitizer/Touch Pad usages | Generic HID reports, then selected quirk layer |
| USB touchscreen | USB HID digitizer or vendor protocol | HID digitizer first |
| PS/2 mouse/touchpad | i8042 controller and PS/2 packet protocols | Three-button mouse, wheel mouse, basic touchpad packet path |
| Internal touchpad | I²C-HID or vendor protocol over I²C/SPI | I²C-HID transport, then one documented vendor adapter |
| Embedded touchscreen | I²C/SPI controller, GPIO interrupt, reset GPIO | Controller-neutral touch frame interface |
| Future wireless HID | HID transport over Bluetooth or other radio service | Deferred until wireless HID transport exists |

USB HID is self-describing through descriptors and reports. The generic HID
layer must parse usages and fields rather than assuming a fixed byte layout.
The [USB-IF HID specifications and tools](https://www.usb.org/hid) define the
USB HID class and usage model. The [HID Usage Tables](https://www.usb.org/documents)
define standard mouse, digitizer, and touchscreen meanings. I²C and SPI are
transport buses; their controller-specific framing belongs below the HID or
touch protocol boundary.

## 4. Target architecture

```text
Applications / compositor / accessibility / games
                         |
             inputd: policy, calibration, gestures
                         |
             generic pointing-input event API
                         |
 HID core | PS/2 core | touch-frame core | device quirks
       |          |             |
 USB HID     i8042/PS2      I2C-HID / SPI / GPIO IRQ
       |
 USB core / xHCI / EHCI / USB hubs
                         |
              DMA, interrupts, power, IPC
```

### 4.1 Layer responsibilities

**Transport adapters** discover controllers, configure interrupts, move bytes,
perform DMA/cache operations, and expose reset/power hooks. They do not map
buttons or calculate gestures.

**Protocol drivers** parse HID descriptors/reports, PS/2 packets, I²C-HID
frames, or a documented touchscreen frame format. They produce normalized
contacts or relative motion.

**Input core** assigns stable device IDs, validates events, timestamps them,
tracks contact slots, handles device lifecycle, and publishes events.

**Policy/user services** apply coordinate transforms, acceleration, palm
rejection, tap/scroll settings, gesture recognition, accessibility behavior,
focus routing, permissions, and compositor integration.

## 5. Generic pointing-input model

The implemented `kernel/include/kernel/input.hpp` API uses fixed-width Omega
ABI types. Future device-specific extensions should remain convertible to a
versioned C-compatible IPC ABI.

### 5.1 Device descriptor

Each device reports:

- stable opaque `DeviceId` and parent/bus ID;
- vendor/product/version identifiers when available;
- transport (`usb`, `ps2`, `i2c`, `spi`, `synthetic`);
- device kind (`mouse`, `touchpad`, `touchscreen`, `trackpoint`, `stylus`);
- relative/absolute axes and valid ranges;
- button, wheel, pressure, distance, tilt, and tool capabilities;
- maximum contacts and supported contact fields;
- physical dimensions and resolution when known;
- coordinate orientation, panel bounds, and calibration metadata;
- power, wake, and hotplug capabilities.

Unknown optional fields must be ignored safely. A device with no physical
dimensions may still report normalized coordinates, but calibration policy
must mark the result as approximate.

### 5.2 Event types

Use a timestamped event envelope:

```text
InputEvent {
    device_id
    sequence
    timestamp
    type
    flags
    payload
}
```

Required event families:

- `REL_X`, `REL_Y`, `REL_WHEEL`, `REL_HWHEEL`;
- `ABS_X`, `ABS_Y`, pressure, contact size, distance, tilt, and rotation;
- `TOUCH_BEGIN`, `TOUCH_UPDATE`, `TOUCH_END`, `TOUCH_CANCEL`;
- contact slot and tracking ID changes;
- left/right/middle/extra button press/release;
- tool type (`finger`, `stylus`, `palm`, `unknown`);
- device add/remove, suspend/resume, reset, and fault;
- synchronization/frame boundary events;
- overflow, dropped-event, and timestamp-quality indicators.

The event stream should preserve device identity and raw normalized data. It
must not encode window coordinates or application focus in the low-level
driver.

### 5.3 Queue and backpressure rules

Input is latency-sensitive but bounded. Implement:

- per-device ring buffers with configurable limits;
- event coalescing for high-rate motion updates where safe;
- never coalesce button transitions or touch begin/end events;
- overflow counters and an explicit synchronization/reset event;
- reader backpressure and cancellation;
- interrupt-context capture followed by deferred parsing;
- sequence numbers to detect loss and stale frames.

## 6. HID core and report parsing

### 6.1 HID descriptor parser

Implement a bounds-checked parser for short and long HID items, including:

- usage page and usage assignment;
- collections and application/physical collection nesting;
- report IDs;
- input/output/feature fields;
- logical and physical min/max values;
- report size/count, signedness, and bit offsets;
- unit and exponent metadata where useful;
- multiple reports sharing one interface.

Reject truncated, contradictory, overlarge, or deeply nested descriptors. The
parser must never read beyond the supplied descriptor or allocate based on an
unchecked device value.

### 6.2 Report decoder

The decoder should produce a generic field map and then a pointing-device
interpretation. It must support:

- boot-protocol three-button mouse;
- wheel and horizontal-wheel usages;
- report IDs and packed non-byte-aligned fields;
- relative signed motion;
- absolute X/Y digitizer coordinates;
- contact count, contact ID, tip switch, in-range, and confidence;
- pressure, width/height, orientation, and tool type when present;
- vendor-defined fields preserved for a quirk driver, not silently guessed.

### 6.3 Quirk system

Quirks must be declarative where possible and narrowly scoped by vendor,
product, version, descriptor hash, or transport. A quirk may correct a known
axis inversion, report-ID defect, missing contact-count field, or required
initialization sequence. Every quirk needs a regression test and a reason;
generic parsing must remain the default.

## 7. Mouse implementation

### M1. USB mouse

Implement USB HID boot mouse support over the planned USB core:

1. enumerate HID interface and interrupt IN endpoint;
2. fetch and parse the report descriptor;
3. configure boot or report protocol as required;
4. submit one or more interrupt transfers;
5. decode relative X/Y, wheel, and buttons;
6. recover endpoint halt and resubmit after completion;
7. remove the device safely on unplug.

Report-protocol devices must not be forced into boot protocol if doing so
would discard required capabilities. The generic path should support common
wheel and extra-button reports before vendor extensions.

### M2. PS/2 mouse

Add an i8042 controller abstraction with bounded command/response handling,
status/error checks, controller reset, device identification, and interrupt
delivery. Support standard three-button packets first, then wheel and extra
buttons. Handle partial packets and resynchronization after parity or timeout
errors.

PS/2 is legacy and platform-specific. It must be optional, isolated from the
generic input API, and disabled when firmware/device discovery says no i8042
exists.

### M3. Motion policy

Keep raw relative motion in the driver. Later `inputd` policy may provide:

- acceleration curves;
- natural or conventional wheel direction;
- pointer speed and handedness;
- button remapping;
- high-resolution wheel accumulation;
- accessibility features such as dwell click and sticky buttons.

## 8. Touchpad implementation

### 8.1 Transport order

1. USB HID touchpad for external and easy-to-emulate devices.
2. I²C-HID touchpad with ACPI/FDT discovery, interrupt GPIO, reset, and
   power/clock integration.
3. One selected vendor protocol only after generic HID and I²C-HID are stable.
4. SPI touchpad adapter where a target board requires it.

The transport must expose report retrieval and power/reset operations; the
touchpad policy must remain above it.

### 8.2 Contact tracking

Normalize each contact to:

- tracking ID and slot;
- active/inactive state;
- X/Y position;
- pressure and contact major/minor dimensions when available;
- tool type and confidence;
- frame sequence and timestamp.

If a device lacks stable contact IDs, the input core may use bounded nearest-
neighbor association, but it must mark the association as synthesized and
allow cancellation when confidence is low. Do not manufacture precise IDs
from ambiguous packets without exposing the limitation.

### 8.3 Touchpad policy

Implement policy in stages:

- button zones and physical button state;
- one-finger motion and click;
- two-finger scrolling;
- tap-to-click and click-and-drag;
- pinch/zoom, rotate, three-finger navigation, and configurable gestures;
- palm rejection and edge suppression;
- disable-while-typing integration with keyboard events;
- per-device profiles and user permissions.

Gestures should consume normalized contact frames, not raw USB/I²C bytes.
They must be testable from recorded traces without hardware.

### 8.4 Palm rejection and ambiguity

Palm rejection must be conservative and reversible. Use contact size, tool
type, pressure, edge position, keyboard activity, and motion history only when
the device reports reliable data. A rejected contact must generate a clear
internal reason and never be confused with a finger release.

## 9. Touchscreen implementation

### 9.1 Embedded panel path

Define a `touch::Controller` adapter for common embedded requirements:

- I²C or SPI command/data transport;
- GPIO interrupt line;
- reset GPIO and power regulator;
- panel dimensions and orientation from FDT/ACPI/platform data;
- firmware/configuration download where required;
- frame acquisition, checksum/CRC validation, and error recovery;
- suspend/resume and wake-on-touch policy.

The driver must not assume that a touch controller’s address, IRQ polarity,
coordinate range, or maximum contacts is globally fixed.

### 9.2 Coordinate pipeline

Use explicit stages:

```text
raw controller coordinates
        -> range normalization
        -> calibration matrix
        -> orientation/rotation
        -> display/panel transform
        -> compositor/application coordinates
```

Store calibration as versioned metadata. Support translation, scale, axis
swap, axis inversion, and a 2D affine matrix. Validate transformed points
against configured bounds and preserve raw values for diagnostics.

### 9.3 Multi-display and hotplug

A touchscreen must identify its associated panel/display when possible. If the
display is hotplugged or rotated, `inputd` should update the transform without
changing the device identity. If association is unknown, expose the device
as an independent absolute input source and require explicit policy.

## 10. Architecture and board integration

### x86_64

- PS/2 i8042 optional legacy path;
- PCI/USB HID through xHCI/EHCI;
- ACPI resource and GPIO/I²C discovery for internal touch devices;
- APIC/MSI routing for USB and I²C controllers;
- QEMU PS/2 and USB mouse/touch fixtures.

### AArch64

- USB HID through QEMU `virt` xHCI or board controller;
- FDT-described I²C/SPI controller, GPIO IRQ, reset, regulator, and panel;
- GIC interrupt routing and DMA/cache maintenance;
- touchscreen/display transform tied to the framebuffer or future display
  service.

### RISC-V 64

- QEMU `virt` or board-specific USB/I²C/SPI controller descriptors;
- PLIC interrupt routing and platform GPIO/reset resources;
- explicit cache-coherency and DMA mapping rules;
- synthetic input backend remains available when the board lacks a supported
  controller.

The generic input core must compile and run in host tests without any of these
architecture adapters.

## 11. Power, reliability, and security

### Power and lifecycle

Support `probe`, `start`, `ready`, `suspend`, `resume`, `reset`, `remove`, and
`failed` states. On suspend, stop new transfers, cancel or drain owned work,
save configuration, and put the controller into the correct low-power state.
On resume, revalidate descriptors and device identity rather than trusting
stale report state.

### Reliability

Handle:

- USB unplug during an active report transfer;
- I²C arbitration loss, NACK, timeout, and bus recovery;
- SPI framing or CRC failure;
- GPIO interrupt storms or stuck-low lines;
- PS/2 parity and incomplete packet errors;
- malformed HID reports and impossible coordinates;
- event queue overflow and duplicate completion;
- controller reset and firmware/configuration failure.

All recovery loops require bounded retry counts and a visible fault state.

### Security and privacy

Pointing devices can affect focus, activation, authentication dialogs, and
accessibility workflows. Therefore:

- raw input access requires an explicit capability/permission;
- untrusted applications receive policy-filtered events, not unrestricted raw
  device streams;
- device names and serials must not leak more identity than needed;
- firmware/configuration blobs are size-checked and eventually authenticated;
- diagnostic logs must avoid sensitive coordinate traces by default;
- injected/synthetic events must be distinguishable from physical events;
- privileged pointer capture must be visible and revocable.

## 12. IPC and userspace design

After Omega IPC and process isolation are available, move policy and most
protocol processing to an `inputd` service:

```text
kernel: transport/DMA/IRQ capability
        |
        +-- hidserv / ps2serv / touchserv adapters
        |
      inputd: device graph, normalization, calibration, gestures, policy
        |
  compositor / applications / accessibility services
```

The IPC ABI should provide:

- device enumeration and capability queries;
- event subscription filtered by device, kind, or capability;
- shared ring buffers with sequence numbers and quotas;
- explicit raw-event permission;
- calibration/profile read/write with validation;
- suspend/resume, reset, and diagnostics requests;
- cancellation and disconnect semantics.

Do not pass C++ object pointers, physical addresses, or unrestricted MMIO
handles across IPC.

## 13. QEMU and OVD emulator plan

Extend OVD with explicit input profiles and topology information:

| Profile | Purpose |
| --- | --- |
| `ps2-mouse` | Relative motion, buttons, wheel, packet resynchronization |
| `usb-hid-mouse` | USB HID boot/report protocol and hotplug |
| `usb-hid-touchpad` | Multi-contact HID reports and gesture traces |
| `usb-hid-touchscreen` | Absolute digitizer reports and calibration |
| `i2c-hid-touchpad` | Synthetic internal touchpad with GPIO IRQ/reset events |
| `spi-touchscreen` | Frame checksum, interrupt, and timeout tests |
| `input-trace` | Deterministic replay of mouse/touch/gesture sequences |

The emulator and scripts should support:

- `--input-profile`, `--input-device`, and repeatable device attachment;
- dry-run command inspection and JSON configuration/state output;
- attach/detach, reset, suspend/resume, IRQ storm, timeout, malformed-report,
  and queue-overflow fault injection;
- mouse move/button/wheel event generation;
- multi-touch begin/update/end/cancel traces;
- orientation, calibration, panel-size, and display-association settings;
- deterministic timestamps and trace replay for CI;
- safe cleanup and no privileged host input capture by default.

QEMU’s standard PS/2 and USB input devices should validate transport wiring.
Synthetic I²C/SPI and touch traces are needed for internal touchscreen paths;
they should be clearly labeled as synthetic and must not imply physical
touch-controller electrical behavior.

## 14. Verification strategy

### 14.1 Host unit tests

Add tests for:

- HID short/long item parsing and descriptor bounds;
- signed/unsigned packed field extraction and report IDs;
- mouse button, motion, wheel, and malformed packet decoding;
- PS/2 packet synchronization and error recovery;
- I²C-HID header/report validation and SPI frame CRC/checksum handling;
- contact-slot tracking, synthesized IDs, cancellation, and frame ordering;
- coordinate normalization, affine transforms, rotation, and clipping;
- palm rejection and gesture state machines from recorded traces;
- event-ring wraparound, coalescing, overflow, and backpressure;
- device lifecycle, hotplug, reset, and stale-event rejection;
- permissions, raw-event capability checks, and synthetic-event marking.

### 14.2 Cross-architecture integration tests

Run on x86_64, AArch64, and RISC-V:

| Test | Acceptance result |
| --- | --- |
| Boot console plus input discovery | Input initialization does not break serial/display boot |
| PS/2 mouse | Motion, buttons, wheel, malformed packet, and unplug/reset behavior |
| USB HID mouse | Enumeration, interrupt IN reports, hotplug, endpoint recovery |
| USB touchpad | Multi-contact reports reach normalized input events |
| USB touchscreen | Absolute coordinates and calibration transform are correct |
| Synthetic I²C/SPI touch | IRQ, reset, timeout, frame validation, and recovery behavior |
| Suspend/resume | Device returns with valid state and no stale contacts |
| OVD/scripts | Profile validation, dry-run, event injection, logs, cleanup, JSON state |

The CI path must not require physical input devices, host GUI access, or
privileged device capture.

### 14.3 Robustness and security tests

Fuzz HID descriptors/reports, PS/2 byte streams, I²C/SPI frames, calibration
metadata, trace files, and IPC messages. Exercise extreme coordinates,
negative ranges, duplicate tracking IDs, contact-count mismatches, timestamps
that move backwards, and event queues under sustained pressure.

## 15. Delivery milestones

| Milestone | Scope | Exit criteria |
| --- | --- | --- |
| P0 Input foundation | Device graph, event envelope, queues, lifecycle, capabilities | Host tests and all-ISA compile pass |
| P1 HID core | Descriptor parser, report decoder, USB HID transport binding | Fuzz-safe parser and USB mouse fixture |
| P2 Mouse | USB/PS2 mouse, buttons, wheel, recovery | QEMU and cross-architecture mouse tests pass |
| P3 Touch core | Contact frames, slots, normalization, calibration | Replayed multi-touch traces pass deterministically |
| P4 Touchpad | USB HID, I²C-HID, gestures, palm policy | Touchpad traces and hotplug/suspend tests pass |
| P5 Touchscreen | I²C/SPI adapters, GPIO IRQ, panel transform | Synthetic touchscreen and one reference board pass |
| P6 IPC/inputd | Userspace event service, permissions, profiles | Applications receive policy-filtered events |
| P7 Hardware hardening | Selected boards, vendor quirks, power and conformance | Board matrix, recovery, security, and documentation review |

Recommended order is P0 → P1 → P2 → P3 → P4/P5 → P6 → P7. USB HID mouse
work can start once the communications USB core is available. Embedded
touchscreen work should wait for GPIO, I²C/SPI, reset, regulator, and display
panel resource descriptions.

## 16. Acceptance criteria

The pointing-device subsystem is ready for production-oriented hardware work
when:

- all supported transports converge on one normalized event API;
- HID parsing is descriptor-driven, bounded, and fuzz-tested;
- mouse, touchpad, and touchscreen hotplug/removal paths are safe;
- multi-touch tracking and calibration are deterministic and testable;
- PS/2 remains optional and cannot contaminate modern transport code;
- all-ISA tests cover x86_64, AArch64, and RISC-V;
- OVD can reproduce normal and fault paths without privileged host setup;
- raw input and event injection are capability-controlled; and
- the future `inputd` IPC ABI is versioned and independent of kernel object
  layouts.

## 17. References

- [USB-IF HID specifications and tools](https://www.usb.org/hid)
- [USB-IF document library](https://www.usb.org/documents)
- [Linux HID transport architecture](https://docs.kernel.org/hid/hid-transport.html)
- [Linux multi-touch protocol reference](https://www.kernel.org/doc/html/latest/input/multi-touch-protocol.html)
- [Linux I²C subsystem overview](https://docs.kernel.org/i2c/summary.html)
- [`docs/COMMUNICATIONS_INTEGRATION_PLAN.md`](COMMUNICATIONS_INTEGRATION_PLAN.md)
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md)
- [`docs/ROADMAP.md`](ROADMAP.md)
- [`emulator/README.md`](../emulator/README.md)
- [`scripts/README.md`](../scripts/README.md)
