#include "arch/pci.hpp"
#include "kernel/kprint.hpp"

namespace hal {

static constexpr uint16_t CONFIG_ADDRESS = 0xCF8;
static constexpr uint16_t CONFIG_DATA    = 0xCFC;

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t PciBus::read_config_32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
#if defined(__x86_64__)
    uint32_t address = (static_cast<uint32_t>(bus) << 16) |
                       (static_cast<uint32_t>(dev) << 11) |
                       (static_cast<uint32_t>(func) << 8) |
                       (offset & 0xFC) | 0x80000000;
    outl(CONFIG_ADDRESS, address);
    return inl(CONFIG_DATA);
#else
    (void)bus; (void)dev; (void)func; (void)offset;
    return 0xFFFFFFFF;
#endif
}

uint16_t PciBus::read_config_16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = read_config_32(bus, dev, func, offset);
    return static_cast<uint16_t>((val >> ((offset & 2) * 8)) & 0xFFFF);
}

void PciBus::scan() {
    kernel::kprintf("[+] Scanning PCI Bus Configuration Space...\n");
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            uint16_t vendor_id = read_config_16(static_cast<uint8_t>(bus), dev, 0, 0x00);
            if (vendor_id != 0xFFFF) {
                uint16_t device_id = read_config_16(static_cast<uint8_t>(bus), dev, 0, 0x02);
                kernel::kprintf("    Found PCI Device [%u:%u:0] Vendor: %x, Device: %x\n",
                                bus, dev, vendor_id, device_id);
            }
        }
    }
}

} // namespace hal
