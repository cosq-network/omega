#ifndef OMEGA_HAL_PCI_HPP
#define OMEGA_HAL_PCI_HPP

#include "std/cstdint.hpp"

namespace hal {

struct PciDevice {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
};

class PciBus {
public:
    static void scan();
    static uint16_t read_config_16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
    static uint32_t read_config_32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
};

} // namespace hal

#endif // OMEGA_HAL_PCI_HPP
