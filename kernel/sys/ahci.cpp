#include "kernel/ahci.hpp"
#include "kernel/kprint.hpp"
#include "kernel/dma.hpp"

#if defined(__x86_64__)
#include "arch/pci.hpp"
#endif

namespace ahci {

struct AhciController {
    storage::Device storage_device;
    volatile HbaMem* abar;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
};

[[maybe_unused]] static AhciController controller{};

struct PortContext {
    dma::Buffer clb_dma;
    dma::Buffer fb_dma;
    dma::Buffer ctba_dma[32];
    bool active;
};

[[maybe_unused]] static PortContext ports[32]{};

static storage::Status probe(storage::Device* device) {
    kernel::kprintf("[AHCI] Probing AHCI device %d\n", device->id);
    return storage::Status::Success;
}

static storage::Status start(storage::Device* device) {
    kernel::kprintf("[AHCI] Starting AHCI device %d\n", device->id);
    
    AhciController* ctrl = static_cast<AhciController*>(device->driver_data);
    if (!ctrl || !ctrl->abar) return storage::Status::IoError;

    kernel::kprintf("[AHCI] ABAR at %p, CAP: 0x%x\n", ctrl->abar, ctrl->abar->cap);

    // HBA Reset
    ctrl->abar->ghc |= (1 << 0); // HR bit
    while (ctrl->abar->ghc & (1 << 0)) {}

    // Enable AHCI
    ctrl->abar->ghc |= (1 << 31); // AE bit
    
    kernel::kprintf("[AHCI] HBA reset complete. PI: 0x%x\n", ctrl->abar->pi);

    uint32_t pi = ctrl->abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            uint32_t ssts = ctrl->abar->ports[i].ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;

            if (det == 3 && ipm == 1) {
                kernel::kprintf("[AHCI] Port %d active device found\n", i);
                ports[i].active = true;

                // Stop command engine
                ctrl->abar->ports[i].cmd &= ~(1 << 0); // ST
                ctrl->abar->ports[i].cmd &= ~(1 << 4); // FRE
                while(ctrl->abar->ports[i].cmd & (1 << 15) || ctrl->abar->ports[i].cmd & (1 << 14)) {} // Wait for CR and FR to clear
                
                // Allocate Command List (1K aligned, 1K size for 32 headers)
                if (dma::alloc(&ports[i].clb_dma, 1024, 1024, dma::DMA_COHERENT)) {
                    for(size_t b=0; b<1024; ++b) reinterpret_cast<uint8_t*>(ports[i].clb_dma.virtual_address)[b] = 0;
                    ctrl->abar->ports[i].clb = static_cast<uint32_t>(ports[i].clb_dma.physical_address);
                    ctrl->abar->ports[i].clbu = static_cast<uint32_t>(ports[i].clb_dma.physical_address >> 32);
                }
                
                // Allocate Received FIS (256 aligned, 256 size)
                if (dma::alloc(&ports[i].fb_dma, 256, 256, dma::DMA_COHERENT)) {
                    for(size_t b=0; b<256; ++b) reinterpret_cast<uint8_t*>(ports[i].fb_dma.virtual_address)[b] = 0;
                    ctrl->abar->ports[i].fb = static_cast<uint32_t>(ports[i].fb_dma.physical_address);
                    ctrl->abar->ports[i].fbu = static_cast<uint32_t>(ports[i].fb_dma.physical_address >> 32);
                }

                // Allocate Command Table for slot 0
                if (dma::alloc(&ports[i].ctba_dma[0], 256, 128, dma::DMA_COHERENT)) {
                    for(size_t b=0; b<256; ++b) reinterpret_cast<uint8_t*>(ports[i].ctba_dma[0].virtual_address)[b] = 0;
                    AhciCmdHeader* cmdheader = reinterpret_cast<AhciCmdHeader*>(ports[i].clb_dma.virtual_address);
                    cmdheader[0].ctba = static_cast<uint32_t>(ports[i].ctba_dma[0].physical_address);
                    cmdheader[0].ctbau = static_cast<uint32_t>(ports[i].ctba_dma[0].physical_address >> 32);
                }
                
                // Start command engine
                while (ctrl->abar->ports[i].cmd & (1 << 15)) {} // wait for CR clear
                ctrl->abar->ports[i].cmd |= (1 << 4); // FRE
                ctrl->abar->ports[i].cmd |= (1 << 0); // ST

                // Allocate 512 bytes for IDENTIFY data
                dma::Buffer identify_dma;
                if (dma::alloc(&identify_dma, 512, 512, dma::DMA_COHERENT)) {
                    AhciCmdHeader* cmdheader = reinterpret_cast<AhciCmdHeader*>(ports[i].clb_dma.virtual_address);
                    cmdheader[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
                    cmdheader[0].a = 0; // Not ATAPI
                    cmdheader[0].w = 0; // Read from device
                    cmdheader[0].prdtl = 1; // 1 PRDT entry
                    
                    AhciCmdTable* cmdtbl = reinterpret_cast<AhciCmdTable*>(ports[i].ctba_dma[0].virtual_address);
                    
                    // Setup PRDT
                    cmdtbl->prdt_entry[0].dba = static_cast<uint32_t>(identify_dma.physical_address);
                    cmdtbl->prdt_entry[0].dbau = static_cast<uint32_t>(identify_dma.physical_address >> 32);
                    cmdtbl->prdt_entry[0].dbc = 511; // 512 bytes - 1
                    cmdtbl->prdt_entry[0].i = 1; // Interrupt on completion
                    
                    // Setup FIS
                    FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmdtbl->cfis);
                    for (size_t b = 0; b < sizeof(FisRegH2D); ++b) reinterpret_cast<uint8_t*>(fis)[b] = 0;
                    fis->fis_type = static_cast<uint8_t>(FisType::RegH2D);
                    fis->c = 1; // Command
                    fis->command = static_cast<uint8_t>(AtaCommand::IdentifyDevice);
                    
                    // Wait for port to be ready
                    while ((ctrl->abar->ports[i].tfd & (0x80 | 0x08)) != 0) {} // Wait for BSY and DRQ to clear
                    
                    // Issue command
                    ctrl->abar->ports[i].ci = 1; // Ring doorbell
                    
                    // Wait for completion
                    while (true) {
                        if ((ctrl->abar->ports[i].ci & 1) == 0) break;
                        if (ctrl->abar->ports[i].is & (1 << 30)) { // Error (TFES)
                            kernel::kprintf("[AHCI] Port %d error during IDENTIFY\n", i);
                            break;
                        }
                    }
                    
                    if ((ctrl->abar->ports[i].ci & 1) == 0) {
                        // IDENTIFY complete, read data
                        uint16_t* identify_data = reinterpret_cast<uint16_t*>(identify_dma.virtual_address);
                        uint64_t sectors = 0;
                        if (identify_data[83] & (1 << 10)) { // LBA48 supported
                            sectors = *reinterpret_cast<uint64_t*>(&identify_data[100]);
                        } else {
                            sectors = *reinterpret_cast<uint32_t*>(&identify_data[60]);
                        }
                        kernel::kprintf("[AHCI] Port %d capacity: %llu sectors\n", i, sectors);
                        
                        device->geometry.total_blocks = sectors;
                        device->geometry.logical_block_size = 512;
                        device->geometry.physical_block_size = 512;
                        device->geometry.max_transfer_blocks = 256;
                    }
                    dma::free(&identify_dma);
                }
            }
        }
    }

    return storage::Status::Success;
}

static storage::Status stop(storage::Device* device) {
    kernel::kprintf("[AHCI] Stopping AHCI device %d\n", device->id);
    return storage::Status::Success;
}

static storage::Status reset(storage::Device* device) {
    kernel::kprintf("[AHCI] Resetting AHCI device %d\n", device->id);
    return storage::Status::Success;
}

static storage::Status remove(storage::Device* device) {
    kernel::kprintf("[AHCI] Removing AHCI device %d\n", device->id);
    return storage::Status::Success;
}

static storage::Status submit_request(storage::Device* device, storage::Request* request) {
    if (!device || !request) return storage::Status::NotReady;
    
    AhciController* ctrl = static_cast<AhciController*>(device->driver_data);
    if (!ctrl || !ctrl->abar) return storage::Status::IoError;
    
    int port_index = -1;
    for (int i = 0; i < 32; i++) {
        if (ports[i].active) {
            port_index = i;
            break;
        }
    }
    if (port_index < 0) return storage::Status::NotReady;
    
    volatile HbaPort* port = &ctrl->abar->ports[port_index];
    PortContext* pctx = &ports[port_index];
    
    uint32_t spin = 0;
    while ((port->tfd & (0x80 | 0x08)) != 0 && spin < 1000000) spin++;
    if ((port->tfd & (0x80 | 0x08)) != 0) return storage::Status::Timeout;
    
    bool is_write = request->type == storage::RequestType::Write;
    bool is_read = request->type == storage::RequestType::Read;
    
    if (request->type == storage::RequestType::Flush) {
        AhciCmdHeader* cmdheader = reinterpret_cast<AhciCmdHeader*>(pctx->clb_dma.virtual_address);
        cmdheader[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
        cmdheader[0].a = 0;
        cmdheader[0].w = 0;
        cmdheader[0].prdtl = 0;
        
        AhciCmdTable* cmdtbl = reinterpret_cast<AhciCmdTable*>(pctx->ctba_dma[0].virtual_address);
        FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmdtbl->cfis);
        for (size_t b = 0; b < sizeof(FisRegH2D); ++b) reinterpret_cast<uint8_t*>(fis)[b] = 0;
        fis->fis_type = static_cast<uint8_t>(FisType::RegH2D);
        fis->c = 1;
        fis->command = static_cast<uint8_t>(AtaCommand::FlushCacheExt);
        
        port->ci = 1;
        while (true) {
            if ((port->ci & 1) == 0) break;
            if (port->is & (1 << 30)) {
                if (request->complete) request->complete(request, storage::Status::IoError, request->context);
                return storage::Status::IoError;
            }
        }
        if (request->complete) request->complete(request, storage::Status::Success, request->context);
        return storage::Status::Success;
    }
    
    if (!is_write && !is_read) return storage::Status::Unsupported;
    
    uint32_t byte_count = request->block_count * device->geometry.logical_block_size;
    dma::Buffer data_dma{};
    if (!dma::map(request->buffer, byte_count, &data_dma, is_write ? dma::DMA_READ : dma::DMA_WRITE)) {
        return storage::Status::IoError;
    }
    
    AhciCmdHeader* cmdheader = reinterpret_cast<AhciCmdHeader*>(pctx->clb_dma.virtual_address);
    cmdheader[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
    cmdheader[0].a = 0;
    cmdheader[0].w = is_write ? 1 : 0;
    cmdheader[0].prdtl = 1;
    
    AhciCmdTable* cmdtbl = reinterpret_cast<AhciCmdTable*>(pctx->ctba_dma[0].virtual_address);
    cmdtbl->prdt_entry[0].dba = static_cast<uint32_t>(data_dma.physical_address);
    cmdtbl->prdt_entry[0].dbau = static_cast<uint32_t>(data_dma.physical_address >> 32);
    cmdtbl->prdt_entry[0].dbc = byte_count - 1;
    cmdtbl->prdt_entry[0].i = 1;
    
    FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmdtbl->cfis);
    for (size_t b = 0; b < sizeof(FisRegH2D); ++b) reinterpret_cast<uint8_t*>(fis)[b] = 0;
    fis->fis_type = static_cast<uint8_t>(FisType::RegH2D);
    fis->c = 1;
    
    if (is_write) fis->command = static_cast<uint8_t>(AtaCommand::WriteDmaExt);
    else fis->command = static_cast<uint8_t>(AtaCommand::ReadDmaExt);
    
    uint64_t lba = request->lba;
    fis->lba0 = static_cast<uint8_t>(lba & 0xFF);
    fis->lba1 = static_cast<uint8_t>((lba >> 8) & 0xFF);
    fis->lba2 = static_cast<uint8_t>((lba >> 16) & 0xFF);
    fis->device = 1 << 6;
    fis->lba3 = static_cast<uint8_t>((lba >> 24) & 0xFF);
    fis->lba4 = static_cast<uint8_t>((lba >> 32) & 0xFF);
    fis->lba5 = static_cast<uint8_t>((lba >> 40) & 0xFF);
    fis->countl = static_cast<uint8_t>(request->block_count & 0xFF);
    fis->counth = static_cast<uint8_t>((request->block_count >> 8) & 0xFF);
    
    dma::sync_for_device(&data_dma);
    
    port->ci = 1;
    while (true) {
        if ((port->ci & 1) == 0) break;
        if (port->is & (1 << 30)) {
            kernel::kprintf("[AHCI] Port error during I/O\n");
            if (request->complete) request->complete(request, storage::Status::IoError, request->context);
            return storage::Status::IoError;
        }
    }
    
    dma::sync_for_cpu(&data_dma);
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
    remove // eject maps to remove here for simplicity
};

static const storage::DriverMatch matches[] = {
    { storage::Protocol::AhciAta, 0, 0, 0x01, 0x06, nullptr } // Mass Storage Controller, SATA, AHCI 1.0
};

static const storage::Driver ahci_driver = {
    .name = "ahci",
    .matches = matches,
    .match_count = sizeof(matches) / sizeof(matches[0]),
    .probe = probe,
    .start = start,
    .stop = stop,
    .reset = reset,
    .remove = remove
};

void init() {
    storage::Status status = storage::Manager::register_driver(&ahci_driver);
    if (status != storage::Status::Success) {
        kernel::kprintf("[AHCI] Failed to register AHCI driver\n");
    } else {
        kernel::kprintf("[AHCI] Registered AHCI driver\n");
    }

#if defined(__x86_64__)
    // Scan PCI bus for AHCI controller
    for (uint16_t bus = 0; bus < 32; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            const uint16_t vendor = hal::PciBus::read_config_16(bus, slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;
            
            const uint16_t class_subclass = hal::PciBus::read_config_16(bus, slot, 0, 0x0A);
            const uint8_t class_code = class_subclass >> 8;
            const uint8_t subclass = class_subclass & 0xFF;

            if (class_code == 0x01 && subclass == 0x06) {
                kernel::kprintf("[AHCI] Found controller at %02x:%02x.0\n", bus, slot);

                // Read BAR5 (ABAR)
                uint32_t bar5 = hal::PciBus::read_config_32(bus, slot, 0, 0x24);
                if (bar5 & 0x01) {
                    kernel::kprintf("[AHCI] BAR5 is I/O space, unsupported.\n");
                    continue;
                }
                
                uintptr_t abar = bar5 & ~0xF;

                // Enable memory space and bus mastering
                uint16_t command = hal::PciBus::read_config_16(bus, slot, 0, 0x04);
                hal::PciBus::write_config_16(bus, slot, 0, 0x04, command | 0x0006);

                controller.pci_bus = static_cast<uint8_t>(bus);
                controller.pci_slot = slot;
                controller.pci_func = 0;
                controller.abar = reinterpret_cast<volatile HbaMem*>(abar);

                controller.storage_device.id = 0;
                controller.storage_device.parent_id = 0;
                controller.storage_device.generation = 1;
                controller.storage_device.type = storage::DeviceType::Block;
                controller.storage_device.protocol = storage::Protocol::AhciAta;
                controller.storage_device.flags = storage::DEVICE_WRITABLE;
                controller.storage_device.state = storage::State::Discovered;
                
                // Set name
                controller.storage_device.name[0] = 'a';
                controller.storage_device.name[1] = 'h';
                controller.storage_device.name[2] = 'c';
                controller.storage_device.name[3] = 'i';
                controller.storage_device.name[4] = '0';
                controller.storage_device.name[5] = '\0';
                
                controller.storage_device.ops = &ops;
                controller.storage_device.driver_data = &controller;

                if (storage::Manager::register_device(&controller.storage_device) == storage::Status::Success) {
                    ahci_driver.probe(&controller.storage_device);
                    ahci_driver.start(&controller.storage_device);
                }
                return; // Stop at first controller for now
            }
        }
    }
#endif
}

} // namespace ahci
