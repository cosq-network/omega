#include "arch/pci.hpp"
#include "kernel/kprint.hpp"

namespace hal {

uint32_t PciBus::read_config_32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    (void)bus; (void)dev; (void)func; (void)offset;
    return 0xFFFFFFFF;
}

uint16_t PciBus::read_config_16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    (void)bus; (void)dev; (void)func; (void)offset;
    return 0xFFFF;
}

void PciBus::scan() {
    kernel::kprintf("[+] AArch64 Device Tree / PCI Bus Scanner Initialized.\n");
}

} // namespace hal
