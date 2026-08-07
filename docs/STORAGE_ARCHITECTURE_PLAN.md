# Omega Storage Architecture Plan

## 1. Purpose and Design Goals

This document defines the storage architecture for Omega across x86_64,
AArch64, and RISC-V 64. It is the authoritative plan for adding support for
NVMe SSDs, SATA SSDs and HDDs, SD and microSD cards, USB flash drives, CD/DVD
media, virtual disks, and future storage devices without coupling filesystems
or applications to a particular controller.

The implementation is protocol-oriented. A physical label such as “SSD” or
“microSD” does not define a driver boundary. The driver boundary is the
industrial transport and command protocol used to access the medium.

The storage subsystem must provide:

- A common asynchronous block I/O API with synchronous convenience wrappers.
- Plug-and-play driver discovery, matching, binding, reset, and removal.
- A layered device graph from bus/controller to filesystem mount.
- Read-only support before write support for every new device family.
- Hotplug-safe lifecycle handling for removable media.
- Architecture-neutral protocol code with small architecture-specific HALs.
- DMA and cache-management interfaces that can later be backed by an IOMMU.
- A stable boundary suitable for moving drivers from the kernel into
  userspace storage servers after IPC and process isolation mature.

The first implementation targets interoperable core profiles rather than
every optional feature in each specification.

## 2. Current Omega State

Omega currently has:

- A minimal VFS root node and callback-based file operations.
- A memory-backed initrd path.
- PCI configuration scanning on x86_64 and placeholder PCI implementations on
  AArch64/RISC-V.
- Architecture-specific boot, interrupt, UART, PMM, and early VMM code.
- A placeholder VirtIO network path.
- A guarded experimental VirtIO-GPU MMIO implementation.
- Bootable FAT32 disk-image generation for QEMU and firmware experiments.

Omega does not yet have:

- A block-device abstraction.
- A storage device registry or device graph.
- A reusable VirtIO transport implementation.
- NVMe, AHCI, ATA, ATAPI, SDHCI, USB host-controller, or USB Mass Storage
  drivers.
- GPT, MBR, FAT32, ext4, ISO9660, or UDF filesystem implementations in the
  VFS.
- A complete cross-architecture DMA/IOMMU layer.
- Production userspace IPC for driver servers.

The storage work therefore begins with the common interfaces and testable
software backends before hardware-specific drivers are added.

### Current implementation status

The first bring-up slice now exists in the kernel:

- Common storage request, status, geometry, device, and driver types.
- Device registry and lifecycle state tracking.
- DMA allocation/mapping abstraction with identity-map and bounce-buffer
  foundations.
- Synthetic writable memory block device used only for storage-core tests.
- GPT/MBR metadata parser foundation.
- Experimental VirtIO-Block MMIO implementation behind
  `-DENABLE_EXPERIMENTAL_VIRTIO_BLOCK=ON`.
- Shared write policy with writable/read-only capability checks, FUA/barrier
  request flags, and flush dispatch.
- Synthetic backend write/flush verification on all three architectures.

VirtIO-Block queue completion is not yet enabled by default because the QEMU
MMIO transport variant still needs completion validation. NVMe, AHCI/SATA,
ATAPI, SDHCI, USB Mass Storage, and filesystem mounting remain subsequent
milestones in this document.

## 3. Device Categories and Protocol Mapping

| Requested device | Physical/host transport | Command protocol | Omega driver family |
|---|---|---|---|
| NVMe SSD | PCIe | NVMe | `nvme` |
| SATA SSD | PCIe | AHCI + ATA/SATA | `ahci_ata` |
| SATA HDD | PCIe | AHCI + ATA/SATA | `ahci_ata` |
| CD/DVD drive | PCIe/AHCI | ATAPI, ATA PACKET, MMC | `ahci_atapi` |
| SD card | SDHCI or platform SD host | SD Physical Layer | `sdhci_sd` |
| microSD card | SDHCI or platform SD host | SD Physical Layer | `sdhci_sd` |
| USB flash drive | USB/xHCI | USB MSC + BOT/UAS + SCSI | `usb_storage` |
| USB CD/DVD drive | USB/xHCI | USB MSC + SCSI/MMC | `usb_storage` + optical commands |
| QEMU virtual disk | PCI or MMIO | VirtIO-Block | `virtio_blk` |

SD and microSD use the same driver. SATA SSDs and HDDs use the same ATA/AHCI
driver. USB flash and USB optical devices share USB transport and Mass Storage
protocol code, then diverge at the SCSI/MMC command layer.

Deferred protocol families are SAS, eMMC, UFS, vendor RAID, FireWire storage,
and hardware-specific flash controllers.

## 4. Layered Architecture

```text
PCI / Device Tree / USB / SD discovery
                    │
                    ▼
          Controller and transport HAL
                    │
                    ▼
          Protocol-specific storage driver
       VirtIO | NVMe | ATA | ATAPI | SD | SCSI
                    │
                    ▼
             Physical block device
                    │
                    ▼
            Partition and media layer
                 GPT / MBR / optical
                    │
                    ▼
             Filesystem block client
          FAT32 | ext4 | ISO9660 | UDF
                    │
                    ▼
                    VFS
                    │
                    ▼
                Applications
```

The common block layer owns request validation, device identity, completion
status, timeout policy, and lifecycle state. A protocol driver owns command
encoding, queue management, controller reset, and protocol error decoding. A
filesystem never accesses PCI BARs, controller registers, or protocol
descriptors directly.

## 5. Common Storage API

The initial public kernel interface belongs in
`kernel/include/kernel/storage.hpp`. It must remain freestanding and avoid
host-library dependencies.

### 5.1 Device and protocol types

```cpp
enum class StorageDeviceType : uint8_t {
    Block,
    Optical,
    Removable,
    Virtual
};

enum class StorageProtocol : uint8_t {
    Memory,
    VirtioBlock,
    Nvme,
    AhciAta,
    Atapi,
    Sdhci,
    UsbMassStorage
};
```

The implementation must also expose device capabilities such as removable,
read-only, flush support, discard support, optical media, and hotplug support.

### 5.2 Geometry

All logical block addressing uses 64-bit LBAs.

```cpp
struct StorageGeometry {
    uint64_t total_blocks;
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint32_t max_transfer_blocks;
};
```

The common layer supports 512-byte and 4096-byte logical sectors. Drivers must
report native geometry and must not silently pretend that a 4096-byte device
has 512-byte physical sectors.

### 5.3 Requests and completions

```cpp
enum class StorageRequestType : uint8_t {
    Read,
    Write,
    Flush,
    Identify,
    Eject
};

struct StorageRequest {
    StorageRequestType type;
    uint64_t lba;
    uint32_t block_count;
    void* buffer;
    uint32_t flags;
    void (*complete)(StorageRequest*, StorageStatus, void* context);
    void* context;
};
```

Rules:

- Read and write requests must be aligned to the device logical block size.
- Requests must be bounded by `max_transfer_blocks`.
- LBA plus count overflow must be rejected before reaching a driver.
- A request is completed exactly once.
- Completion callbacks run in the storage completion context, not inside an
  arbitrary register access path.
- Synchronous wrappers may block the caller but must use the same async path.
- Reads are enabled first; writes are rejected unless the device and mount
  policy explicitly permit them.

### 5.4 Status values

The common status model must distinguish:

- Success.
- Invalid request.
- Out of bounds.
- Not ready.
- Timeout.
- Device removed.
- Media changed.
- Unsupported operation.
- Protocol error.
- Controller reset required.
- DMA mapping failure.
- Permission/read-only rejection.
- I/O error.

Protocol-specific status and sense data may be retained in a diagnostic
structure, but filesystem and VFS code consume the common status.

### 5.5 Device operations

```cpp
struct StorageDeviceOps {
    StorageStatus (*submit)(StorageDevice*, StorageRequest*);
    StorageStatus (*cancel)(StorageDevice*, StorageRequest*);
    StorageStatus (*flush)(StorageDevice*);
    StorageStatus (*reset)(StorageDevice*);
    StorageStatus (*eject)(StorageDevice*);
};
```

`flush` and `eject` may return unsupported for devices without the relevant
capability. A driver must never report successful flush when it has not
completed the protocol’s required cache/barrier operation.

## 6. Device Graph and Plug-and-Play Binding

The storage manager publishes a graph rather than a flat list.

```text
PCI host/controller
  └── storage controller
      └── physical device or namespace
          └── partition or optical medium
              └── filesystem mount
```

Each node has:

- Stable device ID.
- Parent ID.
- Device type and protocol.
- Model and serial strings when available.
- Geometry and capabilities.
- Current lifecycle state.
- Media generation number.
- Driver owner.

### 6.1 Driver matching

Each driver declares match entries for its discovery buses:

- PCI vendor/device IDs.
- PCI class/subclass/programming interface.
- Device Tree compatible strings.
- USB class/subclass/protocol.
- SDHCI controller identity.
- VirtIO transport and device ID.

The storage manager selects the most specific match, calls `probe`, then calls
`start` only after resources and DMA state are available.

### 6.2 Lifecycle

```text
discovered → probing → ready → quiescing → removed
                         │
                         └── resetting → ready
```

Required lifecycle callbacks:

- `probe` — inspect identity without publishing children.
- `start` — allocate queues, enable interrupts, and publish devices.
- `stop` — quiesce requests and disable the controller.
- `reset` — recover a stalled controller or transport.
- `remove` — unregister children and release resources.
- `media_changed` — invalidate partitions/filesystems and re-enumerate.

## 7. DMA, MMIO, and Interrupt Abstraction

Storage drivers must not assume that a kernel virtual address is DMA-capable.

```cpp
struct DmaBuffer {
    uintptr_t virtual_address;
    uintptr_t physical_address;
    size_t size;
    size_t alignment;
    uint32_t flags;
};

StorageStatus dma_alloc(DmaBuffer*, size_t size, size_t alignment,
                        uint32_t flags);
StorageStatus dma_map(void* address, size_t size, DmaBuffer*);
void dma_sync_for_device(DmaBuffer*);
void dma_sync_for_cpu(DmaBuffer*);
void dma_free(DmaBuffer*);
```

Initial implementation:

- Use contiguous low-memory bounce buffers where direct mapping is not yet
  safe.
- Enforce alignment and maximum sizes per controller.
- Add AArch64 cache maintenance and RISC-V fencing hooks.
- Preserve a future direct-map/IOMMU implementation behind the same API.
- Reject DMA buffers outside the configured physical address window.

Interrupt support must provide registration, acknowledgement, deferred
completion, and polling fallback. Every queue-based driver needs a bounded
timeout and a reset escalation path.

## 8. Protocol Driver Plans

### 8.1 VirtIO-Block

Target the VirtIO 1.2 core block profile over PCI and MMIO.

Implementation order:

1. Extract reusable transport and split-virtqueue code from the current
   VirtIO-GPU bring-up path.
2. Discover PCI VirtIO devices and FDT `virtio,mmio` devices.
3. Negotiate VERSION_1 and required block features.
4. Read capacity, block size, and configuration flags.
5. Implement `IN` reads and bounded completion polling/interrupts.
6. Implement reset and queue recovery.
7. Add writes, flush, and barriers after read-only validation.

Commands:

- `VIRTIO_BLK_T_IN`.
- `VIRTIO_BLK_T_OUT`.
- `VIRTIO_BLK_T_FLUSH`.
- `VIRTIO_BLK_T_GET_ID` where supported.

### 8.2 NVMe

Target NVMe 1.4+ interoperable admin and NVM read functionality.

Initialization:

```text
PCI discovery → BAR0 mapping → controller reset → CAP/VS/CC/CSTS
→ admin queue allocation → controller enable → IDENTIFY controller
→ IDENTIFY namespaces → I/O queue creation → namespace publication
```

Initial implementation:

- One admin queue.
- One I/O submission/completion queue pair.
- MSI-X with fallback interrupt mode.
- PRP-based DMA.
- Identify controller and namespace.
- Read command.
- Completion phase-bit tracking.
- Namespace geometry and sector-size discovery.
- Timeout, abort, and controller-reset escalation.

Deferred NVMe features include SGLs, multiple queue scaling, write cache,
Dataset Management/TRIM, reservations, protection information, and advanced
power states.

### 8.3 AHCI/SATA

Target AHCI 1.3.1 with ATA 48-bit LBA reads.

Initialization:

```text
PCI AHCI discovery → ABAR mapping → HBA reset → implemented-port scan
→ SATA/ATAPI signature detection → command-list/FIS setup
→ IDENTIFY → child-device publication
```

SATA disk support:

- IDENTIFY DEVICE.
- READ DMA EXT.
- 48-bit LBA.
- Native sector-size detection.
- Port reset and link recovery.
- Write DMA EXT and FLUSH CACHE in the controlled-write phase.
- NCQ and TRIM later.

SATA SSDs and HDDs use the same driver. Media type is metadata, not a new
block API.

### 8.4 ATAPI CD/DVD

ATAPI shares AHCI transport but uses ATA PACKET and MMC commands.

Initial commands:

- TEST UNIT READY.
- REQUEST SENSE.
- INQUIRY.
- READ CAPACITY.
- READ(10).
- READ CD.
- READ TOC.
- GET CONFIGURATION.
- START STOP UNIT.

Initial behavior is read-only with media-change and eject support where
available. CD/DVD write support is deferred.

### 8.5 SDHCI and SD/microSD

SD and microSD use one driver over SDHCI or a board-specific SD host
controller.

Initialization:

```text
controller reset → power/clock setup → CMD0 → CMD8
→ ACMD41 negotiation → CID/CSD/SCR → RCA/select
→ bus-width setup → block-device publication
```

Initial commands include single- and multi-block reads, status, stop, and
card identification. Card detect, reinsertion, timeout retry, and controller
reset are mandatory. Writes and erase operations are part of the controlled
write phase.

### 8.6 USB Mass Storage

USB storage is layered:

```text
xHCI → USB enumeration/hubs → MSC interface → BOT or UAS → SCSI commands
```

Initial implementation:

- xHCI controller.
- USB device and hub enumeration.
- Mass Storage Class detection.
- Bulk-Only Transport.
- CBW/data/CSW transactions.
- SCSI INQUIRY, TEST UNIT READY, READ CAPACITY, READ(10), and READ(16).
- Stall recovery and Mass Storage Reset.
- Disconnect/reconnect handling.

UAS, streams, and queue scaling follow after BOT correctness. USB optical
devices reuse the transport and SCSI/MMC command layer.

## 9. Partitions and Filesystems

### 9.1 Partition layer

The partition layer publishes child block devices.

Initial support:

- GPT primary header.
- GPT backup header.
- Header and partition-array CRC validation.
- Protective MBR.
- 512-byte and 4096-byte sectors.
- Partition GUIDs and attributes.
- Legacy MBR read-only fallback for common removable media.

### 9.2 Filesystems

Initial read-only filesystems:

- FAT32 for existing Omega boot images and removable media.
- ext4 for native disk deployments.
- ISO9660 for CD media.
- UDF for DVD media.

Each filesystem must validate superblocks, bounds, block sizes, directory
structures, and allocation metadata before dereferencing media-provided
offsets.

The common write layer is now present. It rejects writes to read-only devices,
requires an explicit writable capability, translates FUA/barrier requests into
flush operations, and exposes a separate flush request. The synthetic backend
and VirtIO-Block command path implement writes; VirtIO runtime completion is
still experimental. NVMe, AHCI, SDHCI, USB, and optical write commands remain
disabled until their respective drivers exist and pass recovery tests.

## 10. Hotplug and Recovery

Full hotplug support is required for removable devices.

Rules:

- New requests fail with `DeviceRemoved` after removal.
- Outstanding requests complete exactly once with cancellation/removal
  status.
- Filesystems are disconnected before child devices disappear.
- Open handles cannot access an old media generation after replacement.
- Reinserted media receives a new generation number.
- Planned removal flushes writable media before quiescing.
- Surprise removal must not panic the kernel.

Event sources include PCI link/hotplug, NVMe events, AHCI port changes,
SDHCI card detect, xHCI device events, USB reset/reconnect, and optical media
polling where hardware does not provide notification.

## 11. Architecture Strategy

### x86_64

- Retain legacy PCI configuration for initial support.
- Add PCIe ECAM for modern NVMe/AHCI/xHCI systems.
- Route MSI/MSI-X through the APIC.
- Map controller BARs through the VMM.

### AArch64

- Parse PCIe ECAM regions from Device Tree.
- Route interrupts through the GIC.
- Discover SDHCI and VirtIO-MMIO through Device Tree.
- Add cache maintenance for DMA.

### RISC-V 64

- Parse PCIe ECAM regions from Device Tree.
- Route interrupts through the PLIC and future MSI support.
- Discover SDHCI and VirtIO-MMIO through Device Tree.
- Add fencing/cache hooks for DMA.

Protocol logic must be shared. Architecture files provide only discovery,
MMIO, interrupts, DMA, reset, and power-management hooks.

## 12. Implementation Milestones

### M1: Storage core and DMA — IN PROGRESS

- Common storage types and request API.
- Device graph and driver registry.
- Async completions and synchronous wrappers.
- Timeout/cancellation status model.
- DMA abstraction with bounce buffers.
- Synthetic memory-backed block device.

Current result: synthetic reads and bounds checks pass on x86_64, AArch64, and
RISC-V. Cancellation, timeout, and removal tests remain to be expanded as the
async scheduler and hardware queue paths mature.

### M2: Partition and read-only filesystem layer — PARTIAL

- GPT/MBR.
- FAT32.
- ext4.
- ISO9660/UDF.
- Mount and unmount lifecycle.

Current result: GPT/MBR metadata parsing is present. FAT32, ext4, ISO9660, UDF,
and VFS mount integration remain outstanding.

### M3: VirtIO-Block — EXPERIMENTAL

- Shared VirtIO transport.
- PCI/MMIO discovery.
- Read path.
- Queue reset/recovery.
- QEMU tests on all ISAs.

Current result: VirtIO-MMIO discovery, feature negotiation, queue setup,
capacity discovery, and block request encoding are implemented behind
`ENABLE_EXPERIMENTAL_VIRTIO_BLOCK`. Default boot remains safe until queue
completion is validated on the target QEMU transports.

### M4: NVMe

- PCI discovery and controller initialization.
- Admin and I/O queues.
- Namespace publication.
- PRP reads.
- Reset recovery.

Exit criterion: QEMU NVMe and one physical NVMe reference device pass reads.

### M5: AHCI/SATA/ATAPI

- AHCI controller and SATA ports.
- SATA SSD/HDD reads.
- ATAPI packet commands.
- CD/DVD reads.

Exit criterion: SATA disk and optical image tests pass through the common VFS.

### M6: SDHCI

- Controller discovery.
- SD/microSD initialization.
- Reads and media replacement.

Exit criterion: a QEMU or reference-board SDHCI device passes card lifecycle
and filesystem tests.

### M7: USB storage

- xHCI.
- Hubs.
- MSC BOT.
- SCSI block commands.
- USB flash and USB optical devices.

Exit criterion: USB insertion, read, disconnect, and reinsertion pass without
kernel failure.

### M8: Controlled writes — PARTIAL

- Common block writes.
- Flush/barriers.
- FAT32 writes.
- ext4 journal-aware writes.
- GPT updates.
- Explicit writable-mount policy.

Current result: common write policy and synthetic write/flush tests pass.
Filesystem writes, hardware-driver writes, reset behavior, and power-loss
testing remain outstanding.

### M9: Performance and userspace migration

- NVMe multiqueue.
- AHCI NCQ.
- VirtIO multiqueue.
- USB UAS.
- Request merging and read-ahead.
- Userspace `storaged` service over IPC.
- Capability-controlled raw-device access.

## 13. Testing and Validation

### Unit tests

- LBA overflow and bounds.
- Block-size translation.
- GPT CRC and backup recovery.
- MBR fallback.
- FAT32 cluster chains.
- ext4 extent traversal.
- ISO9660/UDF descriptors.
- Request completion exactly-once behavior.
- Timeout and cancellation.
- Device-generation invalidation.
- DMA alignment and bounce buffers.

The initial automated commands are:

```bash
./scripts/test_storage_unit.sh
./scripts/test_storage.sh
```

`test_storage_unit.sh` compiles the common storage and partition code with a
host-side fake block device. `test_storage.sh` repeats the in-kernel storage
self-test under x86_64, AArch64, and RISC-V QEMU boots, and verifies that the
experimental VirtIO-Block path at least cross-compiles for AArch64 and
RISC-V.

### QEMU integration matrix

| Architecture | Required virtual storage tests |
|---|---|
| x86_64 | VirtIO-Block, NVMe, AHCI SATA, ATAPI CD/DVD, xHCI USB |
| AArch64 | VirtIO-MMIO, VirtIO PCI, PCIe storage where available, SDHCI DT |
| RISC-V | VirtIO-MMIO, VirtIO PCI, PCIe storage where available, SDHCI DT |

Every test must verify device discovery, geometry, read completion, partition
discovery, filesystem mounting, file contents, timeout recovery, and removal
where the emulator supports it.

### Hardware validation

Maintain optional reference tests for:

- PCIe NVMe SSD.
- SATA SSD and HDD.
- USB 3 flash drive.
- SD and microSD cards.
- SATA or USB optical drive.
- One AArch64 SDHCI board.
- One RISC-V PCIe or SDHCI board.

## 14. Security and Reliability Requirements

- Validate every media-provided length, offset, count, and pointer-derived
  range.
- Never expose DMA buffers without ownership and mapping validation.
- Keep new storage devices read-only until their write path is explicitly
  enabled.
- Bound all hardware waits.
- Reset stalled controllers instead of spinning indefinitely.
- Reject unsupported protocol features safely.
- Use device generations to prevent stale-handle access.
- Preserve protocol diagnostics without leaking controller internals into VFS.
- Keep the driver interface suitable for future capability-based IPC.

## 15. Deferred Scope

The following are intentionally not part of the initial listed-device plan:

- SAS and enterprise multipath.
- Hardware RAID and vendor RAID metadata.
- eMMC and UFS.
- FireWire storage.
- Vendor-specific flash translation layers.
- NVMe fabrics.
- Optical writing and packet writing.
- Full IOMMU enforcement before the DMA abstraction is stable.
- Production userspace drivers before IPC and process isolation are complete.
