#include "arch/pci.hpp"
#include "vga_internal.hpp"

namespace hal {
namespace vga {

static constexpr uint16_t BOCHS_VENDOR_1 = 0x1234;
static constexpr uint16_t BOCHS_DEVICE_1 = 0x1111;
static constexpr uint16_t BOCHS_VENDOR_2 = 0x1AF4;
static constexpr uint16_t BOCHS_DEVICE_2 = 0x1111;

static constexpr uint16_t DISPI_INDEX_ID         = 0x0;
static constexpr uint16_t DISPI_INDEX_XRES       = 0x1;
static constexpr uint16_t DISPI_INDEX_YRES       = 0x2;
static constexpr uint16_t DISPI_INDEX_BPP        = 0x3;
static constexpr uint16_t DISPI_INDEX_ENABLE     = 0x4;
static constexpr uint16_t DISPI_INDEX_VIRT_WIDTH   = 0x8;
static constexpr uint16_t DISPI_INDEX_VIRT_HEIGHT = 0x9;

static constexpr uint16_t DISPI_ID_MIN = 0xB0C0;
static constexpr uint16_t DISPI_ID_MAX = 0xB0C2;
static constexpr uint16_t DISPI_ENABLE_ACTIVE = 0x0001;
static constexpr uint16_t DISPI_ENABLE_LFB    = 0x0040;

static bool find_bochs_device(uint8_t* out_bus, uint8_t* out_dev) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            const uint16_t vendor = PciBus::read_config_16(static_cast<uint8_t>(bus), dev, 0, 0x00);
            if (vendor == 0xFFFF) {
                continue;
            }
            const uint16_t device = PciBus::read_config_16(static_cast<uint8_t>(bus), dev, 0, 0x02);
            if ((vendor == BOCHS_VENDOR_1 && device == BOCHS_DEVICE_1) ||
                (vendor == BOCHS_VENDOR_2 && device == BOCHS_DEVICE_2)) {
                *out_bus = static_cast<uint8_t>(bus);
                *out_dev = dev;
                return true;
            }
        }
    }
    return false;
}

static uintptr_t read_bar0(uint8_t bus, uint8_t dev) {
    const uint32_t bar = PciBus::read_config_32(bus, dev, 0, 0x10);
    if (bar == 0 || bar == 0xFFFFFFFF) {
        return 0xFD000000; /* Common QEMU default when BAR is unset. */
    }
    return static_cast<uintptr_t>(bar & ~0xFULL);
}

bool bochs_probe(uintptr_t* out_fb_phys) {
    if (out_fb_phys == nullptr) {
        return false;
    }

    uint8_t bus = 0;
    uint8_t dev = 0;
    const bool pci_found = find_bochs_device(&bus, &dev);

    const uint16_t id = bochs_read(DISPI_INDEX_ID);
    const bool id_ok =
        (id >= DISPI_ID_MIN && id <= DISPI_ID_MAX) || id == 0x4256;

    if (!pci_found && !id_ok) {
        return false;
    }

    if (pci_found) {
        *out_fb_phys = read_bar0(bus, dev);
    } else {
        *out_fb_phys = 0xFD000000;
    }
    return *out_fb_phys != 0;
}

bool bochs_set_mode(uint32_t width, uint32_t height, uint8_t bpp, FramebufferInfo* out) {
    uintptr_t fb_phys = 0;
    if (!bochs_probe(&fb_phys) || out == nullptr) {
        return false;
    }

    bochs_write(DISPI_INDEX_ENABLE, 0);
    bochs_write(DISPI_INDEX_XRES, static_cast<uint16_t>(width));
    bochs_write(DISPI_INDEX_YRES, static_cast<uint16_t>(height));
    bochs_write(DISPI_INDEX_BPP, bpp);
    bochs_write(DISPI_INDEX_VIRT_WIDTH, static_cast<uint16_t>(width));
    bochs_write(DISPI_INDEX_VIRT_HEIGHT, static_cast<uint16_t>(height));
    bochs_write(DISPI_INDEX_ENABLE, DISPI_ENABLE_ACTIVE | DISPI_ENABLE_LFB);

    const uint16_t enable = bochs_read(DISPI_INDEX_ENABLE);
    if ((enable & DISPI_ENABLE_ACTIVE) == 0) {
        return false;
    }
    if (bochs_read(DISPI_INDEX_XRES) != width ||
        bochs_read(DISPI_INDEX_YRES) != height ||
        bochs_read(DISPI_INDEX_BPP) != bpp) {
        return false;
    }

    out->phys_addr   = fb_phys;
    out->virt_addr   = fb_phys;
    out->width       = width;
    out->height      = height;
    out->pitch       = width * (bpp / 8);
    out->bpp         = bpp;
    out->red_mask    = 0xFF;
    out->green_mask  = 0xFF;
    out->blue_mask   = 0xFF;
    return map_framebuffer(out);
}

bool bochs_self_test(uintptr_t fb_phys) {
    auto* fb = reinterpret_cast<volatile uint32_t*>(fb_phys);
    const uint32_t original = fb[0];
    fb[0] = 0x00FF0000;
    const bool ok = fb[0] == 0x00FF0000;
    fb[0] = original;
    return ok;
}

} // namespace vga
} // namespace hal
