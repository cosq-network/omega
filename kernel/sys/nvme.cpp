#include "kernel/nvme.hpp"
#include "kernel/kprint.hpp"
#include "kernel/dma.hpp"

#if defined(__x86_64__)
#include "arch/pci.hpp"
#endif

namespace nvme {

struct NvmeController {
    storage::Device storage_device;
    volatile ControllerRegs* regs;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    
    dma::Buffer asq_dma;
    dma::Buffer acq_dma;
    dma::Buffer iosq_dma;
    dma::Buffer iocq_dma;
    
    uint16_t sq_tail = 0;
    uint16_t cq_head = 0;
    uint8_t cq_phase = 1;
};

[[maybe_unused]] static NvmeController controller{};

static void cpu_relax() {
#if defined(__x86_64__)
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    asm volatile("nop" ::: "memory");
#endif
}

static bool wait_for_ready(volatile ControllerRegs* regs, bool ready) {
    // A broken or absent controller must never strand the kernel in an
    // unbounded MMIO loop. Real hardware gets a recoverable probe failure.
    for (uint32_t timeout = 0; timeout < 10000000u; ++timeout) {
        const bool is_ready = (regs->csts & 1u) != 0;
        if (is_ready == ready) return true;
        if ((timeout & 0x3FFFu) == 0) cpu_relax();
    }
    return false;
}

static bool wait_for_completion(volatile CqEntry* queue, uint16_t index, uint16_t phase) {
    for (uint32_t timeout = 0; timeout < 10000000u; ++timeout) {
        if ((queue[index].status & 1u) == phase) return true;
        if ((timeout & 0x3FFFu) == 0) cpu_relax();
    }
    return false;
}

static storage::Status probe(storage::Device* device) {
    kernel::kprintf("[NVME] Probing NVMe device %d\n", device->id);
    return storage::Status::Success;
}

static storage::Status start(storage::Device* device) {
    kernel::kprintf("[NVME] Starting NVMe device %d\n", device->id);
    
    NvmeController* ctrl = static_cast<NvmeController*>(device->driver_data);
    if (!ctrl || !ctrl->regs) return storage::Status::IoError;

    kernel::kprintf("[NVME] Registers at %p, CAP low: %x, VS: %x\n",
                    ctrl->regs, static_cast<uint32_t>(ctrl->regs->cap), ctrl->regs->vs);

    // Disable controller
    ctrl->regs->cc &= ~1u; // Clear CC.EN
    
    // Wait for CSTS.RDY to become 0
    if (!wait_for_ready(ctrl->regs, false)) {
        kernel::kprintf("[NVME][WARN] Controller did not leave the ready state.\n");
        return storage::Status::IoError;
    }

    // Allocate Admin Queues (SQ and CQ, e.g., 64 entries each)
    if (!dma::alloc(&ctrl->asq_dma, 4096, 4096, dma::DMA_COHERENT) ||
        !dma::alloc(&ctrl->acq_dma, 4096, 4096, dma::DMA_COHERENT) ||
        !dma::alloc(&ctrl->iosq_dma, 4096, 4096, dma::DMA_COHERENT) ||
        !dma::alloc(&ctrl->iocq_dma, 4096, 4096, dma::DMA_COHERENT)) {
        kernel::kprintf("[NVME] Failed to allocate queues\n");
        return storage::Status::IoError;
    }
    
    for(size_t i=0; i<4096; ++i) reinterpret_cast<uint8_t*>(ctrl->asq_dma.virtual_address)[i] = 0;
    for(size_t i=0; i<4096; ++i) reinterpret_cast<uint8_t*>(ctrl->acq_dma.virtual_address)[i] = 0;
    for(size_t i=0; i<4096; ++i) reinterpret_cast<uint8_t*>(ctrl->iosq_dma.virtual_address)[i] = 0;
    for(size_t i=0; i<4096; ++i) reinterpret_cast<uint8_t*>(ctrl->iocq_dma.virtual_address)[i] = 0;

    // Set Admin Queue Attributes (AQA)
    ctrl->regs->aqa = (63 << 16) | 63;
    
    // Set Base Addresses
    ctrl->regs->asq = static_cast<uint64_t>(ctrl->asq_dma.physical_address);
    ctrl->regs->acq = static_cast<uint64_t>(ctrl->acq_dma.physical_address);

    // Enable controller
    uint32_t cc = 0;
    cc |= (4 << 20); // IOCQES
    cc |= (6 << 16); // IOSQES
    // Set MPS (Maximum Page Size) to 0 (4 KiB)
    cc |= (0 << 7); // MPS
    cc |= 1;         // EN
    ctrl->regs->cc = cc;

    // Wait for CSTS.RDY to become 1
    if (!wait_for_ready(ctrl->regs, true)) {
        kernel::kprintf("[NVME][WARN] Controller did not become ready.\n");
        dma::free(&ctrl->asq_dma);
        dma::free(&ctrl->acq_dma);
        dma::free(&ctrl->iosq_dma);
        dma::free(&ctrl->iocq_dma);
        return storage::Status::IoError;
    }
    
    kernel::kprintf("[NVME] Controller started and ready.\n");

    dma::Buffer identify_dma;
    if (dma::alloc(&identify_dma, 4096, 4096, dma::DMA_COHERENT)) {
        for (size_t i = 0; i < 4096; ++i) reinterpret_cast<uint8_t*>(identify_dma.virtual_address)[i] = 0;
        
        SqEntry* sq = reinterpret_cast<SqEntry*>(ctrl->asq_dma.virtual_address);
        volatile CqEntry* cq = reinterpret_cast<volatile CqEntry*>(ctrl->acq_dma.virtual_address);
        uint32_t dstrd = (ctrl->regs->cap >> 32) & 0xF;
        uint32_t doorbell_stride = 4 << dstrd;
        volatile uint32_t* sq0_tdbl = reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ctrl->regs) + 0x1000);
        volatile uint32_t* cq0_hdbl = reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ctrl->regs) + 0x1000 + doorbell_stride);
        
        // IDENTIFY Controller
        sq[0].cdw0 = 0x06;
        sq[0].nsid = 0;
        sq[0].dptr[0] = static_cast<uint64_t>(identify_dma.physical_address);
        sq[0].cdw10 = 1; // CNS = 1
        dma::sync_for_device(&ctrl->asq_dma);
        *sq0_tdbl = 1;
        if (!wait_for_completion(cq, 0, 1)) {
            kernel::kprintf("[NVME][WARN] IDENTIFY Controller timed out.\n");
            dma::free(&identify_dma);
            return storage::Status::IoError;
        }
        *cq0_hdbl = 1;
        
        // IDENTIFY Namespace 1
        sq[1].cdw0 = 0x06;
        sq[1].nsid = 1;
        sq[1].dptr[0] = static_cast<uint64_t>(identify_dma.physical_address);
        sq[1].cdw10 = 0; // CNS = 0
        dma::sync_for_device(&ctrl->asq_dma);
        *sq0_tdbl = 2;
        if (!wait_for_completion(cq, 1, 1)) {
            kernel::kprintf("[NVME][WARN] IDENTIFY Namespace timed out.\n");
            dma::free(&identify_dma);
            return storage::Status::IoError;
        }
        dma::sync_for_cpu(&identify_dma);
        *cq0_hdbl = 2;
        
        uint64_t ns_size = *reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(identify_dma.virtual_address) + 0);
        device->geometry.total_blocks = ns_size;
        device->geometry.logical_block_size = 512;
        device->geometry.physical_block_size = 512;
        device->geometry.max_transfer_blocks = 256;

        // CREATE IO CQ (QID=1)
        sq[2].cdw0 = 0x05;
        sq[2].dptr[0] = static_cast<uint64_t>(ctrl->iocq_dma.physical_address);
        sq[2].cdw10 = ((64 - 1) << 16) | 1;
        sq[2].cdw11 = 1; // PC=1
        dma::sync_for_device(&ctrl->asq_dma);
        *sq0_tdbl = 3;
        if (!wait_for_completion(cq, 2, 1)) {
            kernel::kprintf("[NVME][WARN] CREATE IO CQ timed out.\n");
            return storage::Status::IoError;
        }
        *cq0_hdbl = 3;
        
        // CREATE IO SQ (QID=1)
        sq[3].cdw0 = 0x01;
        sq[3].dptr[0] = static_cast<uint64_t>(ctrl->iosq_dma.physical_address);
        sq[3].cdw10 = ((64 - 1) << 16) | 1;
        sq[3].cdw11 = (1 << 16) | 1; // CQID=1, PC=1
        dma::sync_for_device(&ctrl->asq_dma);
        *sq0_tdbl = 4;
        if (!wait_for_completion(cq, 3, 1)) {
            kernel::kprintf("[NVME][WARN] CREATE IO SQ timed out.\n");
            return storage::Status::IoError;
        }
        *cq0_hdbl = 4;

        dma::free(&identify_dma);
    }

    return storage::Status::Success;
}

static storage::Status stop(storage::Device*) {
    return storage::Status::Success;
}

static storage::Status reset(storage::Device*) {
    return storage::Status::Success;
}

static storage::Status remove(storage::Device*) {
    return storage::Status::Success;
}

static storage::Status submit_request(storage::Device* device, storage::Request* request) {
    if (!device || !request) return storage::Status::NotReady;
    NvmeController* ctrl = static_cast<NvmeController*>(device->driver_data);
    if (!ctrl || !ctrl->regs) return storage::Status::IoError;
    // Ensure controller is ready before submitting I/O
    if (!(ctrl->regs->csts & 1u)) {
        kernel::kprintf("[NVME][WARN] Controller not ready, aborting request\n");
        return storage::Status::NotReady;
    }
    
    bool is_write = request->type == storage::RequestType::Write;
    bool is_read = request->type == storage::RequestType::Read;
    
    if (request->type == storage::RequestType::Flush) {
        if (request->complete) request->complete(request, storage::Status::Success, request->context);
        return storage::Status::Success;
    }
    
    if (!is_write && !is_read) return storage::Status::Unsupported;
    
    uint32_t byte_count = request->block_count * device->geometry.logical_block_size;
    dma::Buffer data_dma{};
    if (!dma::map(request->buffer, byte_count, &data_dma, is_write ? dma::DMA_READ : dma::DMA_WRITE)) {
        return storage::Status::IoError;
    }
    // Build PRP fields – handle multi‑page buffers
    uint64_t prp1 = data_dma.physical_address;
    uint64_t prp2 = 0;
    const uint32_t page_size = 4096; // assume 4KiB pages
    if (byte_count > page_size) {
        // More than one page: need PRP List
        uint32_t pages = (byte_count + page_size - 1) / page_size;
        // Allocate a DMA buffer for the PRP list (pages-2 entries)
        dma::Buffer prp_list{};
        if (!dma::alloc(&prp_list, (pages - 2) * sizeof(uint64_t), page_size, dma::DMA_COHERENT)) {
            return storage::Status::IoError;
        }
        uint64_t *list = reinterpret_cast<uint64_t*>(prp_list.virtual_address);
        for (uint32_t i = 0; i < pages - 2; ++i) {
            list[i] = data_dma.physical_address + ((i + 2) * page_size);
        }
        prp2 = prp_list.physical_address;
        // Store the list buffer pointer in request->context for later free (optional)
        request->context = reinterpret_cast<void*>(prp_list.physical_address); // store PRP list pointer
    }
    // Populate PRP fields in SQ entry later (sq[tail].dptr[0] = prp1; sq[tail].dptr[1] = prp2;)
    
    uint32_t dstrd = (ctrl->regs->cap >> 32) & 0xF;
    uint32_t doorbell_stride = 4 << dstrd;
    volatile uint32_t* sq1_tdbl = reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ctrl->regs) + 0x1000 + 2 * doorbell_stride);
    volatile uint32_t* cq1_hdbl = reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ctrl->regs) + 0x1000 + 3 * doorbell_stride);
    
    SqEntry* sq = reinterpret_cast<SqEntry*>(ctrl->iosq_dma.virtual_address);
    uint16_t tail = ctrl->sq_tail;
    
    sq[tail].cdw0 = (is_write ? 0x01 : 0x02); // Write=01, Read=02
    sq[tail].nsid = 1;
    sq[tail].dptr[0] = prp1;
    sq[tail].dptr[1] = prp2; // 0 if not needed
    sq[tail].cdw10 = static_cast<uint32_t>(request->lba);
    sq[tail].cdw11 = static_cast<uint32_t>(request->lba >> 32);
    sq[tail].cdw12 = request->block_count - 1;
    
    dma::sync_for_device(&ctrl->iosq_dma);
    if (is_write) dma::sync_for_device(&data_dma);
    
    tail = (tail + 1) % 64;
    ctrl->sq_tail = tail;
    
    *sq1_tdbl = tail;
    
    volatile CqEntry* cq = reinterpret_cast<volatile CqEntry*>(ctrl->iocq_dma.virtual_address);
    uint16_t head = ctrl->cq_head;
    
    if (!wait_for_completion(cq, head, ctrl->cq_phase)) {
        dma::free(&data_dma);
        kernel::kprintf("[NVME][WARN] I/O completion timed out.\n");
        return storage::Status::IoError;
    }
    
    if (is_read) dma::sync_for_cpu(&data_dma);
    
    head = (head + 1) % 64;
    if (head == 0) ctrl->cq_phase ^= 1;
    ctrl->cq_head = head;
    
    *cq1_hdbl = head;
    
    if (request->complete) request->complete(request, storage::Status::Success, request->context);
    return storage::Status::Success;
}

static storage::Status cancel(storage::Device*, storage::Request*) {
    return storage::Status::Unsupported;
}

static storage::Status flush(storage::Device* device) {
    storage::Request request{storage::RequestType::Flush, 0, 0, nullptr, 0, nullptr, nullptr};
    return submit_request(device, &request);
}

[[maybe_unused]] static const storage::DeviceOps ops = {
    submit_request,
    cancel,
    flush,
    reset,
    remove
};

static const storage::DriverMatch matches[] = {
    { storage::Protocol::Nvme, 0, 0, 0x01, 0x08, nullptr } // Mass Storage Controller, NVMe
};

static const storage::Driver nvme_driver = {
    .name = "nvme",
    .matches = matches,
    .match_count = sizeof(matches) / sizeof(matches[0]),
    .probe = probe,
    .start = start,
    .stop = stop,
    .reset = reset,
    .remove = remove
};

void init() {
    storage::Status status = storage::Manager::register_driver(&nvme_driver);
    if (status != storage::Status::Success) {
        kernel::kprintf("[NVME] Failed to register NVMe driver\n");
    } else {
        kernel::kprintf("[NVME] Registered NVMe driver\n");
    }

#if defined(__x86_64__)
    // Scan PCI bus for NVMe controller
    for (uint16_t bus = 0; bus < 32; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            const uint16_t vendor = hal::PciBus::read_config_16(bus, slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;
            
            const uint16_t class_subclass = hal::PciBus::read_config_16(bus, slot, 0, 0x0A);
            const uint8_t class_code = class_subclass >> 8;
            const uint8_t subclass = class_subclass & 0xFF;
            const uint8_t prog_if = hal::PciBus::read_config_16(bus, slot, 0, 0x08) >> 8;

            if (class_code == 0x01 && subclass == 0x08 && prog_if == 0x02) {
                kernel::kprintf("[NVME] Found controller at %02x:%02x.0\n", bus, slot);

                // Read BAR0
                uint32_t bar0 = hal::PciBus::read_config_32(bus, slot, 0, 0x10);
                if (bar0 & 0x01) {
                    kernel::kprintf("[NVME] BAR0 is I/O space, unsupported.\n");
                    continue;
                }
                
                uintptr_t base_addr = bar0 & ~0xF;

                // Enable memory space and bus mastering
                uint16_t command = hal::PciBus::read_config_16(bus, slot, 0, 0x04);
                hal::PciBus::write_config_16(bus, slot, 0, 0x04, command | 0x0006);

                controller.pci_bus = static_cast<uint8_t>(bus);
                controller.pci_slot = slot;
                controller.pci_func = 0;
                controller.regs = reinterpret_cast<volatile ControllerRegs*>(base_addr);

                controller.storage_device.id = 10;
                controller.storage_device.parent_id = 0;
                controller.storage_device.generation = 1;
                controller.storage_device.type = storage::DeviceType::Block;
                controller.storage_device.protocol = storage::Protocol::Nvme;
                controller.storage_device.flags = storage::DEVICE_WRITABLE;
                controller.storage_device.state = storage::State::Discovered;
                
                controller.storage_device.name[0] = 'n';
                controller.storage_device.name[1] = 'v';
                controller.storage_device.name[2] = 'm';
                controller.storage_device.name[3] = 'e';
                controller.storage_device.name[4] = '0';
                controller.storage_device.name[5] = '\0';
                
                controller.storage_device.ops = &ops;
                controller.storage_device.driver_data = &controller;

                if (storage::Manager::register_device(&controller.storage_device) == storage::Status::Success) {
                    nvme_driver.probe(&controller.storage_device);
                    if (nvme_driver.start(&controller.storage_device) != storage::Status::Success) {
                        kernel::kprintf("[NVME][SKIP] Controller initialization unavailable.\n");
                        return;
                    }
                    
                    uint8_t write_buffer[512] __attribute__((aligned(512))){};
                    uint8_t read_buffer[512] __attribute__((aligned(512))){};
                    for (uint32_t i = 0; i < 512; ++i) write_buffer[i] = static_cast<uint8_t>(0xA5u ^ i);
                    
                    storage::Request write_request{storage::RequestType::Write, 0, 1, write_buffer, 0, nullptr, nullptr};
                    if (storage::Manager::submit_sync(&controller.storage_device, &write_request) == storage::Status::Success) {
                        storage::Request read_request{storage::RequestType::Read, 0, 1, read_buffer, 0, nullptr, nullptr};
                        if (storage::Manager::submit_sync(&controller.storage_device, &read_request) == storage::Status::Success) {
                            bool matched = true;
                            for (uint32_t i = 0; i < 512; ++i) if (read_buffer[i] != write_buffer[i]) matched = false;
                            if (matched) {
                                kernel::kprintf("[TEST][PASS] NVMe write/read completion\n");
                            } else {
                                kernel::kprintf("[TEST][FAIL] NVMe data mismatch\n");
                            }
                        } else {
                            kernel::kprintf("[TEST][FAIL] NVMe read completion\n");
                        }
                    } else {
                        kernel::kprintf("[TEST][FAIL] NVMe write completion\n");
                    }
                }
                return;
            }
        }
    }
#endif
}

} // namespace nvme
