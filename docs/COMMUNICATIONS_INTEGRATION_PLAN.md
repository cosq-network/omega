# Omega Communications Integration Plan

## 1. Purpose and scope

This document defines the long-term plan for adding generic, industry-aligned
communications support to Omega. The target families are:

- motherboard and SoC serial ports;
- USB 2.0 and USB 3.x host/device operation;
- USB Type-C connector, role, power, and alternate-mode management;
- Ethernet MAC/PHY devices and virtual Ethernet devices;
- Wi-Fi radios operating in the 2.4 GHz and 5 GHz bands; and
- USB-attached serial, networking, storage, and human-interface devices.

The plan deliberately separates a connector or physical medium from the
protocol carried over it. USB Type-C is a connector and port-management
system; it is not itself an alternative to USB 2.0 or USB 3.x. Similarly, an
Ethernet cable, a Wi-Fi radio, and a VirtIO network device must converge on a
common network-device API without pretending that their discovery, power, or
link-management procedures are identical.

This is an implementation plan, not a claim that every listed controller or
protocol is already implemented. The current repository provides early UART
logging, a preliminary network path, interrupt/DMA foundations, PCI discovery,
and QEMU/OVD tooling. The milestones below turn those foundations into a
manageable, testable, and eventually userspace-hosted communications stack.

## 2. Current Omega baseline

### 2.1 Existing foundations

The implementation can build for x86_64, AArch64, and RISC-V 64. Relevant
existing pieces are:

| Existing component | Reuse in communications work | Current limitation |
| --- | --- | --- |
| Per-architecture UART/serial output | Early console, debug transport, first serial driver | Primarily polling/console-oriented; no complete TTY or modem-line API |
| Interrupt HALs and controller work | ISR delivery, deferred work, completion events | SMP, timer preemption, and complete interrupt routing are still future work |
| PCI scanner | PCI/PCIe Ethernet, Wi-Fi, xHCI, and future controller discovery | BAR mapping, MSI/MSI-X, power management, and robust device ownership need completion |
| DMA foundation | Descriptor rings, USB transfer buffers, network RX/TX | IOMMU, cache maintenance, lifetime ownership, and production bounce buffers remain |
| Preliminary network code and VirtIO path | Reference `netdev` and packet-processing implementation | It must be normalized behind a stable device/queue API and validated end to end |
| FDT and MMIO architecture work | AArch64/RISC-V SoC MAC, UART, USB, and PHY discovery | FDT resource parsing and platform clock/reset/regulator support must expand |
| Storage driver registry/lifecycle patterns | Common communications device registry and hotplug model | Communications need link, endpoint, child-device, and recovery events |
| OVD and QEMU scripts | Repeatable serial, Ethernet, USB, and fault-injection test fixtures | Type-C and real Wi-Fi RF behavior require synthetic models or hardware |
| Planned IPC foundation | Migration of drivers into isolated servers | Capability handles, shared-memory grants, and interrupt delivery are not complete |

### 2.2 Architectural boundary

Until Omega has process isolation and IPC, reference drivers may live in the
kernel. They must nevertheless be written as if they will later run in
userspace. Protocol logic must not depend directly on x86 I/O ports, PCI
configuration registers, ARM/RISC-V MMIO addresses, or a particular QEMU
machine. Architecture code owns discovery and hardware primitives; shared
code owns protocol state machines, request validation, queues, and policy
interfaces.

The intended eventual servers are:

```text
        applications / system services
                    |
       devd  netd  wifid  seriald  usbhostd
                    |
              Omega IPC + capabilities
                    |
  netdev / 802.11 / USB core / TTY / Type-C policy
                    |
 PCIe | USB | SDIO | I2C/TCPC | MDIO | SoC MMIO | DMA/IOMMU
```

## 3. Design principles

1. **One generic API, many protocol implementations.** Applications should
   discover capabilities and use stable operations rather than identify a
   device as “the second USB port” or “the e1000.”
2. **Transport, protocol, and policy remain separate.** PCIe, USB, SDIO, and
   MMIO are transports; Ethernet and 802.11 are link protocols; routing,
   authentication, and user permissions are policy.
3. **Asynchronous I/O is the default.** Descriptor rings, URBs, RX queues,
   link events, and hotplug events must not require a busy-waiting caller.
4. **Explicit ownership and lifetime.** Every DMA buffer, transfer, endpoint,
   packet, and device handle has an owner, completion rule, and cancellation
   path.
5. **Capability-driven behavior.** Optional checksum offload, USB speed,
   Wi-Fi bands, Ethernet VLAN support, flow control, and power roles are
   reported as capabilities and negotiated where the standard requires it.
6. **Safe failure is a feature.** Invalid descriptors, malformed packets,
   link loss, unplug, controller reset, and firmware failure must produce a
   bounded error and recoverable state rather than memory corruption.
7. **Standards at the boundary, project policy above it.** The driver must
   follow the applicable standard, while Omega-specific naming, permissions,
   logging, and service IPC remain above the protocol boundary.
8. **Emulation before hardware.** Each milestone gets a deterministic QEMU or
   synthetic backend before physical-board validation is attempted.

## 4. Standards and device-family map

| Device family | Standards/protocols to implement | First reference target | Deferred complexity |
| --- | --- | --- | --- |
| SoC/legacy serial | 16550/NS16550A-compatible UART, ARM PL011, RISC-V SBI/UART variants | Existing x86 COM1, AArch64 PL011, RISC-V console | DMA serial, RS-485, modem control, multiport cards |
| Ethernet | IEEE 802.3 MAC/PHY concepts, MDIO/MII-family management, PCI/PCIe, VirtIO network transport | VirtIO-net, then QEMU e1000 | Board-specific PHYs, PTP, TSN, SR-IOV |
| USB 2.0 | USB 2.0 host transfer model; control, bulk, interrupt, isochronous transfers | xHCI USB2 root port; EHCI only where required | OHCI/UHCI legacy controllers, isochronous audio/video |
| USB 3.x | USB 3.2 SuperSpeed transfer model and xHCI controller programming | QEMU xHCI with USB 3-capable devices | SuperSpeedPlus tuning, streams, USB4 |
| USB Type-C | Type-C CC/orientation/role model, USB PD, TCPC/UCSI integration | Synthetic TCPC state machine and optional I2C TCPC | Full PD certification, alternate modes, retimers, power budgeting |
| Wi-Fi 2.4/5 GHz | IEEE 802.11 MAC management/data model, regulatory control, WPA2/WPA3 policy | Synthetic radio plus a QEMU-visible transport contract | Real firmware, DFS/TPC, roaming, calibration, certification |
| USB serial/network | USB class descriptors and CDC ACM/ECM/NCM where applicable | USB CDC ACM and one network class fixture | Vendor-specific devices and suspend/resume corner cases |

The exact revision of each specification must be pinned in an implementation
ADR before coding a production driver. USB-IF maintains the USB 2.0, USB 3.x,
Type-C, TCPCI, and interoperability documents in its document library. IEEE
publishes the 802.3 and 802.11 standards families; Wi-Fi Alliance material is
useful for certification terminology, but it does not replace the IEEE
specifications or local regulatory requirements.

## 5. Common communications architecture

### 5.1 Device graph and discovery

Introduce a shared communications device graph, modeled after the storage
registry but extended for nesting:

```text
controller -> bus/port -> function/interface -> protocol service -> endpoint
```

Each node has a stable opaque ID, parent ID, vendor/product identity,
location, capabilities, power state, link state, and lifecycle state. A USB
interface, a Type-C port, an Ethernet PHY, and a Wi-Fi virtual interface are
children of different kinds of parent but expose the same discovery and
event-registration concepts.

Required lifecycle states are `discovered`, `matched`, `starting`, `ready`,
`degraded`, `suspended`, `resetting`, `removing`, `removed`, and `failed`.
Transitions must be serialized and observable. Device removal invalidates
new operations while allowing already-owned buffers to complete or cancel.

### 5.2 Common data and operation model

Add freestanding equivalents of the following concepts under a future
`kernel/include/kernel/comm/` namespace:

- `DeviceId`, `ParentId`, `CapabilitySet`, `DeviceDescriptor`;
- `Buffer`, `DmaBuffer`, `ScatterGatherList`, and ownership state;
- `Completion`, `CancelToken`, `Timeout`, and `Event`;
- `Transport`, `Controller`, `Endpoint`, and `ProtocolDriver`;
- `read`, `write`, `submit`, `cancel`, `flush`, `reset`, `suspend`, and
  `resume` operations where meaningful;
- typed status values distinguishing invalid input, busy, no device,
  timeout, protocol error, link down, permission denied, and recovery needed.

The first ABI can be C++ internal interfaces. Before userspace migration,
define a stable C-compatible IPC representation with fixed-width fields,
versioned messages, explicit buffer lengths, and no raw C++ object pointers.

### 5.3 Event model

Events must be queued rather than delivered from hardware interrupt context.
Initial event types should include:

- device add/remove and controller reset;
- RX packet available and TX completion;
- serial input, modem-line change, and break condition;
- link up/down, speed/duplex change, carrier change, and PHY fault;
- USB attach/detach, reset, suspend/resume, transfer completion, and
  endpoint halt;
- Type-C attach/detach, orientation, role, VBUS, PD contract, and power
  fault;
- Wi-Fi scan result, authentication/association state, channel change,
  regulatory change, key failure, and radio recovery.

## 6. Serial-port implementation plan

### C1. Complete the serial abstraction

Create a `serial::Port` interface with configuration and status objects:

- baud rate, data bits, parity, stop bits, and hardware/software flow control;
- blocking, non-blocking, and asynchronous read/write;
- TX/RX ring-buffer sizing and watermark configuration;
- break generation/detection and modem lines where the hardware supports it;
- error counters for framing, parity, overrun, and noise;
- suspend/resume and reset hooks.

Keep `hal::uart_putc()` as the emergency early-console path. The normal
serial driver must be initialized later and must not silently replace early
logging until its buffers and interrupt path are ready.

### C1.1 Architecture adapters

- x86_64: 16550-compatible COM ports, I/O-port and MMIO variants, interrupt
  routing, FIFO probing, and optional PCI multiport discovery.
- AArch64: PL011 first, then a generic MMIO UART descriptor for SoC variants;
  use FDT-provided address, clock, interrupt, and baud information.
- RISC-V: retain SBI console fallback, add a discovered MMIO UART adapter,
  and keep console operation independent of a particular board.

### C1.2 USB serial convergence

Implement USB CDC ACM as a class driver over USB core, exposing the same
serial configuration and stream operations as a native UART. CDC ACM must be
identified by descriptors and class/subclass/protocol, not by a vendor name.
Vendor-specific bridges are a later driver family.

## 7. Ethernet and generic networking plan

### C2. Define `netdev`

Normalize the preliminary network path behind a `net::Device` interface:

- permanent/current MAC address, MTU, link speed, duplex, pause, and carrier;
- RX/TX queue count, queue start/stop, interrupt moderation, and NAPI-like
  polling budget;
- packet buffer ownership and zero-copy eligibility;
- checksum, segmentation, VLAN, multicast, and promiscuous-mode capabilities;
- statistics, link settings, reset, and recovery operations.

The network interface must carry Ethernet frames without forcing every driver
to implement IPv4, IPv6, TCP, or UDP. L2 parsing and L3/L4 stacks become
separate clients. IPv6, ARP/ND, DHCP, DNS, routing, and firewall policy are
not responsibilities of a MAC driver.

### C2.1 Driver order

1. VirtIO-net MMIO/PCI reference driver, because it is deterministic and
   already matches Omega’s QEMU workflow.
2. QEMU e1000 reference driver to exercise PCI BARs, interrupts, and a second
   descriptor model.
3. Generic PCIe Ethernet driver framework with BAR, MSI/MSI-X, DMA, and reset
   services.
4. One real hardware MAC/PHY family on each target ISA, selected only after a
   board support package supplies clocks, reset, GPIO, MDIO, and regulators.

### C2.2 PHY and link management

Keep MAC and PHY responsibilities distinct. Add an MDIO/MII management
interface, PHY identification, autonegotiation, link polling/interrupts,
speed/duplex reporting, and bounded recovery. RGMII/RMII/SGMII details belong
in platform descriptors or PHY drivers, not in the generic socket layer.

## 8. USB host/device architecture

### C3. USB core

Implement a transport-neutral USB core with:

- bus, root hub, hub, device, configuration, interface, endpoint, and
  alternate-setting objects;
- descriptor parsing with strict length/type bounds checks;
- address allocation and enumeration state machine;
- control, bulk, interrupt, and isochronous transfer request objects;
- endpoint halt/recovery, reset, suspend/resume, and cancellation;
- class-driver matching by standard descriptors;
- child-device add/remove events and stable topology paths.

Do not expose controller-specific TRBs or queue heads to class drivers.

### C3.1 Controller order

1. xHCI controller with USB 2 and SuperSpeed root-port support. xHCI gives a
   common modern controller model for USB 2/3 testing.
2. EHCI support only if a target board or required QEMU profile needs a
   separate USB 2 controller.
3. OHCI/UHCI only as compatibility work after the common core is stable.

The xHCI driver must implement capability/operational/runtime register
separation, command and event rings, transfer rings, TRB cycle handling,
port-status changes, endpoint context setup, controller reset, and DMA/cache
rules. Every ring transition needs unit coverage independent of real MMIO.

### C3.2 First USB class drivers

Implement classes in this order:

1. CDC ACM for serial convergence;
2. HID boot keyboard/mouse for input and hotplug validation;
3. Mass Storage using Bulk-Only Transport, then UAS where the USB and SCSI
   layers are ready;
4. CDC ECM/NCM for USB networking;
5. isochronous audio/video only after timing and bandwidth scheduling exist.

USB Mass Storage must reuse the storage abstraction and USB networking must
reuse `netdev`; neither class should create a private block or network API.

## 9. USB Type-C, power, and role management

### C4. Type-C port manager

Model Type-C as a port-management service above the USB host/device
controller. It owns:

- CC1/CC2 attach detection, orientation, and cable/accessory state;
- source/sink and host/device/data-role state;
- VBUS/VCONN policy and advertised/current power limits;
- USB Power Delivery message sequencing and contract state;
- role swaps, hard/soft reset, detach, and fault recovery;
- SuperSpeed lane/mux selection and optional alternate-mode negotiation.

The first implementation should support a conservative fixed-role host and a
synthetic state machine. Production dual-role support requires a TCPC or
UCSI-capable controller, GPIO/ADC/power-regulator integration, and a clear
power-safety policy. Never infer VBUS safety solely from a USB data attach.

### C4.1 TCPC/UCSI boundary

Define a small `typec::PortController` interface so an I2C-attached TCPC,
platform firmware UCSI implementation, and QEMU fake controller can share the
same policy engine. Keep register access, interrupt acknowledgement, and
power-switch control inside the adapter. Expose decoded port state and PD
messages above it.

### C4.2 Alternate modes

DisplayPort and other alternate modes are deferred until Type-C orientation,
PD contracts, lane muxing, and safety faults are reliable. The system should
report unsupported modes explicitly rather than accepting a contract it cannot
drive.

## 10. Wi-Fi 2.4 GHz and 5 GHz plan

### C5. Split radio transport from 802.11 policy

Wi-Fi support has three distinct layers:

1. **Transport:** PCIe, USB, or SDIO queues, interrupts, DMA, firmware
   loading, reset, and power states.
2. **Radio/MAC control:** scan, channel, association, encryption keys,
   transmit/receive queues, rate control, and firmware events.
3. **Policy/service:** regulatory domain, authentication, roaming, saved
   networks, user permissions, and WPA2/WPA3 supplicant behavior.

Expose a generic `wifi::Radio` API for discovery, scan, channel/regulatory
capabilities, authentication/association, key installation, interface mode,
statistics, and recovery. The network stack consumes completed 802.11 data
frames through `netdev`-compatible packet queues; it should not know whether
the radio is PCIe, USB, or SDIO.

### C5.1 Band and regulatory handling

Represent bands and channels as capability data. The driver must enforce the
active regulatory domain, channel width, DFS requirements, transmit-power
limits, and unavailable-channel state. 2.4 GHz and 5 GHz support must not be
hard-coded as a boolean because country and firmware policy affect the valid
channel set.

### C5.2 Security and firmware

- Keep WPA2/WPA3 credential and key policy out of the hardware transport.
- Do not log passphrases, raw key material, or unredacted management frames.
- Validate firmware size, version, transport bounds, and device response;
  later add signature/measurement policy.
- Treat firmware crash, timeout, and malformed event as recoverable device
  faults with bounded reset attempts.
- Begin with a synthetic radio backend and a trace-replay test fixture. Real
  chipsets should be selected only after licensing, firmware redistribution,
  regulatory, and upstream-maintenance decisions are recorded.

## 11. DMA, cache, interrupt, and power requirements

Before high-throughput USB, Ethernet, or Wi-Fi drivers are called production
ready, complete the common DMA contract:

- physical/IOVA mapping and unmapping with ownership checks;
- coherent and non-coherent cache maintenance for all ISAs;
- alignment, boundary, and maximum-segment validation;
- scatter/gather support and bounded descriptor counts;
- bounce buffers when an IOMMU or device addressing limit requires them;
- completion-before-reuse guarantees and double-completion detection;
- optional IOMMU isolation for userspace driver servers.

Interrupt handlers should acknowledge hardware, capture a minimal event, and
schedule deferred processing. MSI/MSI-X, GIC, PLIC, legacy IRQs, and USB port
change interrupts must converge on the same event/completion mechanism.

Power management needs per-controller and per-link states, clock/reset hooks,
runtime idle, suspend/resume, wake sources, and recovery after power loss.
Type-C adds VBUS/VCONN and over-current safety; Wi-Fi adds radio-off,
calibration, and regulatory constraints.

## 12. Userspace and IPC migration

After Phase 7.6 IPC and process isolation are available, migrate protocol and
policy services in stages:

| Service | Initial responsibility | Kernel-resident minimum |
| --- | --- | --- |
| `seriald` | TTY configuration, buffering, permissions, line discipline | UART register adapter and interrupt capability |
| `usbhostd` | Enumeration, class drivers, hotplug policy | xHCI/EHCI register, DMA, and interrupt adapter |
| `netd` | Interfaces, addresses, routes, packet policy | NIC queue and DMA adapter |
| `wifid` | Scan, association, keys, regulatory policy | Firmware transport and protected events |
| `typed` | Type-C/PD policy and port ownership | TCPC register and power-switch capability |
| `devd` | Device graph, naming, permissions, event fan-out | Minimal discovery records and capability broker |

IPC messages must use versioned fixed-size headers, explicit grants for packet
and descriptor memory, quotas per client, cancellation, and backpressure.
No service may receive unrestricted MMIO or physical-memory access.

## 13. QEMU and OVD emulator plan

Extend the existing OVD profile model rather than adding device-specific shell
logic in every script.

### 13.1 Planned profiles

- Serial: `console`, `serial-16550`, `serial-pl011`, `serial-file`, and
  multiport fixtures using QEMU chardevs.
- Ethernet: `virtio-net`, `e1000`, `user`, and optional TAP/bridge mode when
  explicitly enabled by the host.
- USB: `xhci-usb2`, `xhci-usb3`, `ehci-usb2`, `usb-cdc-acm`, `usb-hid`, and
  `usb-mass-storage`.
- Type-C: `typec-fixed-host`, `typec-fixed-device`, and a fake attach/detach/
  PD-event backend for state-machine tests.
- Wi-Fi: `wifi-synthetic-24ghz`, `wifi-synthetic-5ghz`, and trace replay;
  these model control/data events and do not claim to emulate RF behavior.

### 13.2 Emulator requirements

Every profile must support dry-run command inspection, deterministic state
and logs, architecture validation, bounded timeouts, cleanup on failure, and
explicit host-network permissions. QEMU user networking is the safe default;
TAP/bridge access must be opt-in. The emulator should expose attach/detach,
link flap, controller reset, malformed descriptor, timeout, and firmware-fault
injection where QEMU can support it or where a synthetic backend is used.

The GUI should show parent/child topology, profile, link/attach state,
architecture, last error, and logs. It should not imply that a synthetic Wi-Fi
profile provides a real radio or that Type-C power contracts are electrically
enforced by QEMU.

## 14. Verification strategy

### 14.1 Host-side unit tests

Add deterministic tests for:

- serial configuration validation and ring-buffer wraparound;
- packet buffer ownership, queue backpressure, Ethernet parsing, checksums,
  VLAN metadata, and malformed-frame rejection;
- DMA segment coalescing, alignment, cache-state transitions, and reuse;
- USB descriptor parsing, endpoint rules, address allocation, control
  requests, URB cancellation, TRB cycle bits, ring rollover, and event
  decoding;
- Type-C attach/role/PD state transitions, invalid-message handling, and
  power-fault recovery;
- Wi-Fi information-element parsing, channel/regulatory filtering, key-event
  validation, scan state, and firmware event bounds;
- device graph lifecycle, stable IDs, hotplug ordering, and capability checks.

### 14.2 Cross-architecture integration tests

For x86_64, AArch64, and RISC-V, verify at minimum:

| Test family | Required result |
| --- | --- |
| Serial boot and interrupt smoke | Early console remains available; normal serial path reports configuration and RX/TX events |
| VirtIO-net | Device discovery, queue setup, packet TX/RX, link change, reset, and clean shutdown |
| PCI Ethernet | BAR/interrupt discovery and one complete RX/TX path using the selected QEMU model |
| xHCI USB2/USB3 | Controller reset, root-port enumeration, attach/detach, one class transfer, and timeout recovery |
| USB CDC ACM | Same generic serial API as native UART |
| USB networking/storage | Class binding reaches the existing `netdev`/storage abstractions |
| Type-C synthetic | Attach, orientation, role, PD contract, detach, and fault events |
| Wi-Fi synthetic | 2.4/5 GHz capability report, scan, association state, key event, link loss, and recovery |
| OVD/scripts | Dry-run, real QEMU smoke, JSON/state/log inspection, cleanup, and invalid-profile rejection |

Run each test with serial-only fallback and with graphical OVD launch where
available. Tests must not require privileged host networking or a physical
radio in CI.

### 14.3 Robustness and security tests

Add fuzz or property tests for USB descriptors, Ethernet/802.11 headers,
Type-C PD messages, firmware events, and IPC messages. Exercise unplug during
transfer, duplicate completion, DMA map failure, queue exhaustion, link flap,
controller reset, suspend/resume, and malformed capabilities. Enable compiler
warnings and host sanitizers for protocol code where the freestanding build
permits it.

## 15. Phased delivery plan

| Milestone | Scope | Exit criteria |
| --- | --- | --- |
| C0 Communications foundation | Device graph, capability types, event/completion model, DMA contract ADR, test fakes | Host unit suite and all-ISA compile pass |
| C1 Serial | Native UART lifecycle, interrupt RX/TX, configuration, CDC ACM API compatibility | Native and USB serial tests pass on all target ISAs |
| C2 Ethernet | `netdev`, VirtIO-net, e1000 reference, PHY/link events, packet ownership | TX/RX, link flap, reset, and dry-run profiles pass |
| C3 USB core | xHCI, optional EHCI, enumeration, URBs, hubs, hotplug | USB2/USB3 synthetic/QEMU enumeration and cancellation pass |
| C4 USB classes and Type-C | CDC ACM, HID, BOT, Type-C/TCPC abstraction, conservative PD state machine | Class binding and attach/role/fault tests pass |
| C5 Wi-Fi | Synthetic radio, transport contract, scan/association, regulatory and security boundaries | 2.4/5 GHz trace tests pass without real RF dependencies |
| C6 IPC migration | `seriald`, `usbhostd`, `netd`, `wifid`, `typed`, capability grants | User services operate without raw hardware access |
| C7 Hardware enablement | Selected boards, PHY/TCPC adapters, one Wi-Fi chipset, conformance and recovery | Board test matrix, documentation, and security review complete |

Recommended execution order is C0 → C1 → C2 → C3 → C4 → C5 → C6. Work on
USB class drivers may proceed in parallel with Ethernet after the common DMA
and event contracts are frozen. Real Wi-Fi and dual-role Type-C hardware
should not gate early QEMU progress.

## 16. Acceptance criteria and non-goals

The communications architecture is ready for the next production stage when:

- every supported device exposes a stable generic capability and lifecycle
  record;
- no protocol driver depends on architecture-specific registers;
- all asynchronous operations have cancellation, timeout, and removal paths;
- DMA ownership and cache behavior are documented and tested on all ISAs;
- QEMU/OVD can reproduce normal operation and major fault classes without
  privileged host setup;
- unit and integration tests cover native UART, Ethernet, USB, Type-C, and
  synthetic Wi-Fi paths;
- userspace migration has a versioned IPC design; and
- unsupported capabilities are reported explicitly.

This plan does not promise carrier-grade Wi-Fi, USB4, Thunderbolt, Bluetooth,
Ethernet TSN/PTP, modem protocols, or production power-delivery certification
in the first implementation. Those are follow-on programs that depend on
stable foundations, hardware access, firmware licensing, and dedicated
compliance testing.

## 17. Primary references

- [USB-IF document library](https://www.usb.org/documents) — USB 2.0, USB 3.x,
  USB Type-C, TCPCI, UCSI, and interoperability specifications.
- [USB 3.2 overview](https://www.usb.org/usb-32-0) — USB 3.2 terminology and
  SuperSpeed transfer-rate model.
- [USB Type-C system overview](https://www.usb.org/sites/default/files/D1T1-2%20-%20USB%20Type-C%20System%20Overview.pdf)
  — connector, role, and system-management concepts.
- [IEEE 802.3 standards family](https://standards.ieee.org/ieee/802.3/) —
  Ethernet MAC/PHY standard family.
- [IEEE 802.11 standards family](https://standards.ieee.org/ieee/802.11/) —
  wireless LAN standard family.
- [Wi-Fi Alliance Wi-Fi CERTIFIED 6](https://www.wi-fi.org/discover-wi-fi/wi-fi-certified-6)
  — certification terminology and feature overview.
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — Omega HAL and freestanding
  constraints.
- [`docs/STORAGE_ARCHITECTURE_PLAN.md`](STORAGE_ARCHITECTURE_PLAN.md) — the
  sibling transport/protocol/lifecycle design for storage devices.
- [`emulator/README.md`](../emulator/README.md) and
  [`scripts/README.md`](../scripts/README.md) — current OVD and test-runner
  workflows to extend for communications profiles.
