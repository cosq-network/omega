#ifndef OMEGA_KERNEL_AHCI_HPP
#define OMEGA_KERNEL_AHCI_HPP

#include "std/cstdint.hpp"
#include "kernel/storage.hpp"

namespace ahci {

// Standard AHCI/ATA definitions
enum class FisType : uint8_t {
    RegH2D = 0x27, // Register FIS - host to device
    RegD2H = 0x34, // Register FIS - device to host
    DmaAct = 0x39, // DMA activate FIS - device to host
    DmaSetup = 0x41, // DMA setup FIS - bidirectional
    Data = 0x46,   // Data FIS - bidirectional
    Bist = 0x58,   // BIST activate FIS - bidirectional
    PioSetup = 0x5F, // PIO setup FIS - device to host
    DevBits = 0xA1, // Set device bits FIS - device to host
};

enum class AtaCommand : uint8_t {
    ReadDma = 0xC8,
    ReadDmaExt = 0x25,
    WriteDma = 0xCA,
    WriteDmaExt = 0x35,
    FlushCacheExt = 0xEA,
    IdentifyDevice = 0xEC,
};

#pragma pack(push, 1)

struct FisRegH2D {
    uint8_t fis_type; // FisType::RegH2D
    uint8_t pmport : 4;
    uint8_t rsv0 : 3;
    uint8_t c : 1;    // 1: Command, 0: Control

    uint8_t command;
    uint8_t featurel;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;

    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;

    uint8_t rsv1[4];
};

struct HbaPort {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
};

struct HbaMem {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t rsv[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    HbaPort ports[32];
};

struct AhciCmdHeader {
    uint8_t cfl : 5;
    uint8_t a : 1;
    uint8_t w : 1;
    uint8_t p : 1;
    uint8_t r : 1;
    uint8_t b : 1;
    uint8_t c : 1;
    uint8_t rsv0 : 1;
    uint8_t pmp : 4;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
};

struct AhciPrdtEntry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc : 22;
    uint32_t rsv1 : 9;
    uint32_t i : 1;
};

struct AhciCmdTable {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    AhciPrdtEntry prdt_entry[1]; // VLA
};

#pragma pack(pop)

void init();

} // namespace ahci

#endif // OMEGA_KERNEL_AHCI_HPP
