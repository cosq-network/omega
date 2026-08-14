#ifndef OMEGA_KERNEL_NVME_HPP
#define OMEGA_KERNEL_NVME_HPP

#include "std/cstdint.hpp"
#include "kernel/storage.hpp"

namespace nvme {

#pragma pack(push, 1)

// NVMe Controller Registers (BAR 0)
struct ControllerRegs {
    uint64_t cap;       // Controller Capabilities
    uint32_t vs;        // Version
    uint32_t intms;     // Interrupt Mask Set
    uint32_t intmc;     // Interrupt Mask Clear
    uint32_t cc;        // Controller Configuration
    uint32_t csts;      // Controller Status
    uint32_t nssr;      // NVM Subsystem Reset
    uint32_t aqa;       // Admin Queue Attributes
    uint32_t rsvd1;     // Reserved
    uint64_t asq;       // Admin Submission Queue Base Address
    uint64_t acq;       // Admin Completion Queue Base Address
    uint32_t cmbloc;    // Controller Memory Buffer Location
    uint32_t cmbsz;     // Controller Memory Buffer Size
    uint32_t bpinfo;    // Boot Partition Information
    uint32_t bprsel;    // Boot Partition Read Select
    uint64_t bpmbl;     // Boot Partition Memory Buffer Location
    uint64_t cmbmsc;    // Controller Memory Buffer Memory Space Control
    uint32_t cmbsts;    // Controller Memory Buffer Status
    uint8_t rsvd[0xF00 - 0x5C];
    uint32_t sq0_tdbl;  // SQ0 Tail Doorbell
    uint32_t cq0_hdbl;  // CQ0 Head Doorbell
    // Other doorbells follow based on Doorbell Stride (DSTRD) from CAP
};

// Submission Queue Entry
struct SqEntry {
    uint32_t cdw0;      // Command Dword 0 (Opcode, Flags, CID)
    uint32_t nsid;      // Namespace Identifier
    uint64_t rsvd2[2];
    uint64_t mptr;      // Metadata Pointer
    uint64_t dptr[2];   // Data Pointer (PRP1 and PRP2)
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};

// Completion Queue Entry
struct CqEntry {
    uint32_t cdw0;      // Command Specific
    uint32_t rsvd1;
    uint16_t sqhd;      // SQ Head Pointer
    uint16_t sqid;      // SQ Identifier
    uint16_t cid;       // Command Identifier
    uint16_t status;    // Status Field (and Phase Tag)
};

#pragma pack(pop)

void init();

} // namespace nvme

#endif // OMEGA_KERNEL_NVME_HPP
