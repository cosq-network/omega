#ifndef OMEGA_KERNEL_FDT_HPP
#define OMEGA_KERNEL_FDT_HPP

#include "std/cstdint.hpp"

namespace fdt {

struct SimpleFramebuffer {
    uintptr_t phys_addr;
    uint64_t  size;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
    uint8_t   bpp;
    uint8_t   red_mask;
    uint8_t   green_mask;
    uint8_t   blue_mask;
    uint8_t   red_shift;
    uint8_t   green_shift;
    uint8_t   blue_shift;
    bool      alpha;
};

struct VirtioMmioDevice {
    uintptr_t phys_addr;
    uint64_t size;
};

uintptr_t boot_pointer();
void set_boot_pointer(uintptr_t pointer);
bool find_simple_framebuffer(SimpleFramebuffer* out);
bool find_virtio_mmio(VirtioMmioDevice* out, uint32_t ordinal = 0);

} // namespace fdt

extern "C" void omega_set_fdt_pointer(uintptr_t pointer);

#endif
