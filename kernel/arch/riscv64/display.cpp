#include "arch/display.hpp"
#include "kernel/fdt.hpp"
#include "kernel/kprint.hpp"
#include "kernel/vmm.hpp"
#include "kernel/virtio_gpu.hpp"

namespace hal {

alignas(8) static DisplayBackend active = DisplayBackend::None;
alignas(8) static FramebufferInfo fb_info{};
alignas(8) static DisplayCapabilities caps{};

static bool map_framebuffer(FramebufferInfo* info, uint64_t size) {
    if (info == nullptr || info->phys_addr == 0 || info->width == 0 || info->height == 0) return false;
    const uint32_t pitch = info->pitch != 0 ? info->pitch : info->width * (info->bpp / 8);
    const uint64_t bytes = static_cast<uint64_t>(pitch) * info->height;
    if (size != 0 && bytes > size) return false;
    const uintptr_t first = info->phys_addr & ~static_cast<uintptr_t>(0xFFF);
    const uintptr_t last = (info->phys_addr + bytes + 0xFFF) & ~static_cast<uintptr_t>(0xFFF);
    (void)first;
    (void)last;
    info->virt_addr = info->phys_addr;
    info->pitch = pitch;
    return true;
}

void Display::init() {
    active = DisplayBackend::None; caps = {}; fb_info = {};
    if (virtio_gpu::init(&fb_info)) {
        active = DisplayBackend::VirtioGpu; caps.linear_framebuffer = true;
        return;
    }
    fdt::SimpleFramebuffer simple{};
    if (fdt::find_simple_framebuffer(&simple)) {
        fb_info.phys_addr = simple.phys_addr;
        fb_info.width = simple.width; fb_info.height = simple.height;
        fb_info.pitch = simple.stride; fb_info.bpp = simple.bpp;
        fb_info.red_mask = simple.red_mask; fb_info.green_mask = simple.green_mask; fb_info.blue_mask = simple.blue_mask;
        fb_info.red_shift = simple.red_shift; fb_info.green_shift = simple.green_shift; fb_info.blue_shift = simple.blue_shift;
        if (map_framebuffer(&fb_info, simple.size)) {
            active = DisplayBackend::SimpleFb; caps.linear_framebuffer = true;
            return;
        }
    }
}

DisplayBackend Display::active_backend() { return active; }
DisplayCapabilities Display::capabilities() { return caps; }
const char* Display::backend_name() { return active == DisplayBackend::SimpleFb ? "SimpleFb" : active == DisplayBackend::VirtioGpu ? "VirtioGpu" : "None"; }
void Display::text_clear(uint8_t) {}
void Display::text_putc(uint8_t, uint8_t, char, uint8_t) {}
void Display::text_set_cursor(uint8_t, uint8_t) {}
void Display::text_scroll_up(uint8_t) {}
uint16_t Display::text_peek(uint8_t, uint8_t) { return 0; }
bool Display::set_mode(uint32_t, uint32_t, uint8_t) { return false; }
const FramebufferInfo* Display::framebuffer() { return caps.linear_framebuffer ? &fb_info : nullptr; }
void Display::flush() { if (active == DisplayBackend::VirtioGpu) virtio_gpu::flush(); else asm volatile("" ::: "memory"); }

bool Display::run_self_tests() {
    if (active == DisplayBackend::VirtioGpu) {
        const bool pass = virtio_gpu::self_test();
        kernel::kprintf("[TEST][%s] VirtioGpu display setup\n", pass ? "PASS" : "FAIL");
        return pass;
    }
    if (active != DisplayBackend::SimpleFb) {
        kernel::kprintf("[TEST][SKIP] SimpleFb (no linear framebuffer)\n");
        return true;
    }
    volatile uint8_t* pixel = reinterpret_cast<volatile uint8_t*>(fb_info.virt_addr);
    const uint8_t saved = pixel[0]; pixel[0] = 0x5A;
    const bool pass = pixel[0] == 0x5A; pixel[0] = saved;
    kernel::kprintf("[TEST][%s] SimpleFb pixel read/write\n", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace hal
