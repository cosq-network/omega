#include "arch/display.hpp"
#include "kernel/kprint.hpp"
#include "vga_internal.hpp"

namespace hal {
namespace vga {

bool map_framebuffer(FramebufferInfo* info) {
    if (info == nullptr || info->phys_addr == 0 || info->height == 0) {
        return false;
    }

    const uint32_t pitch = info->pitch != 0 ? info->pitch : info->width * (info->bpp / 8);
    const size_t bytes = static_cast<size_t>(pitch) * info->height;
    if (info->width == 0 || info->bpp < 8 || bytes == 0 ||
        info->phys_addr > 0xFFFFFFFFull || bytes > 0x100000000ull - info->phys_addr) return false;

    /* Boot page tables identity-map the low 4 GiB; verify the FB window is accessible. */
    auto* probe = reinterpret_cast<volatile uint32_t*>(info->phys_addr);
    const uint32_t saved = probe[0];
    probe[0] = 0xA5A5A5A5u;
    if (probe[0] != 0xA5A5A5A5u) {
        return false;
    }
    probe[0] = saved;

    (void)bytes;
    info->virt_addr = info->phys_addr;
    info->size = bytes;
    info->pitch = pitch;
    return true;
}

bool bochs_verify_mode(uint32_t width, uint32_t height, uint8_t bpp) {
    return bochs_read(0x1) == width &&
           bochs_read(0x2) == height &&
           bochs_read(0x3) == bpp &&
           (bochs_read(0x4) & 0x0001) != 0;
}

} // namespace vga
} // namespace hal

namespace hal {

static DisplayBackend active = DisplayBackend::None;
static FramebufferInfo fb_info{};
static DisplayCapabilities caps{};

static const char* backend_names[] = {
    "None",
    "VgaText",
    "BochsVbe",
    "BootFramebuffer",
};

void Display::init() {
    active = DisplayBackend::None;
    caps = {};

    const vga::BootFramebufferResult boot_fb = vga::boot_framebuffer_probe();
    if (boot_fb.valid) {
        fb_info = boot_fb.info;
        if (!vga::map_framebuffer(&fb_info)) {
            kernel::kprintf("[!] Display: BootFramebuffer mapping failed\n");
        } else {
            active = DisplayBackend::BootFramebuffer;
            caps.linear_framebuffer = true;
            caps.text_mode = vga::text_buffer_accessible();
            kernel::kprintf("[+] Display: BootFramebuffer %ux%ux%u\n",
                            fb_info.width, fb_info.height, fb_info.bpp);
            if (caps.text_mode) {
                vga::text_clear(0x07);
            }
            return;
        }
    }

    FramebufferInfo bochs_info{};
    if (vga::bochs_set_mode(1024, 768, 32, &bochs_info)) {
        fb_info = bochs_info;
        active = DisplayBackend::BochsVbe;
        caps.linear_framebuffer = true;
        caps.text_mode = vga::text_buffer_accessible();
        kernel::kprintf("[+] Display: BochsVbe %ux%ux%u (FB phys %x)\n",
                        fb_info.width, fb_info.height, fb_info.bpp, fb_info.phys_addr);
        if (caps.text_mode) {
            vga::text_clear(0x07);
        }
        return;
    }

    if (vga::text_buffer_accessible()) {
        active = DisplayBackend::VgaText;
        caps.text_mode = true;
        caps.linear_framebuffer = false;
        vga::text_clear(0x07);
        kernel::kprintf("[+] Display: VgaText 80x25\n");
        return;
    }

    kernel::kprintf("[!] Display: No usable output backend found\n");
}

DisplayBackend Display::active_backend() { return active; }
DisplayCapabilities Display::capabilities() { return caps; }

const char* Display::backend_name() {
    const auto idx = static_cast<uint8_t>(active);
    if (idx >= sizeof(backend_names) / sizeof(backend_names[0])) {
        return "Unknown";
    }
    return backend_names[idx];
}

void Display::text_clear(uint8_t attr) { vga::text_clear(attr); }

void Display::text_putc(uint8_t col, uint8_t row, char c, uint8_t attr) {
    vga::text_putc(col, row, c, attr);
}

void Display::text_set_cursor(uint8_t col, uint8_t row) {
    vga::text_set_cursor(col, row);
}

void Display::text_scroll_up(uint8_t attr) {
    vga::wait_for_vsync();
    vga::text_scroll_up(attr);
}

uint16_t Display::text_peek(uint8_t col, uint8_t row) {
    return vga::text_peek(col, row);
}

bool Display::set_mode(uint32_t width, uint32_t height, uint8_t bpp) {
    FramebufferInfo info{};
    if (!vga::bochs_set_mode(width, height, bpp, &info)) {
        return false;
    }
    fb_info = info;
    active = DisplayBackend::BochsVbe;
    caps.linear_framebuffer = true;
    return true;
}

const FramebufferInfo* Display::framebuffer() {
    if (!caps.linear_framebuffer) {
        return nullptr;
    }
    return &fb_info;
}

void Display::flush() {
    if (caps.linear_framebuffer) {
        asm volatile("" ::: "memory");
    }
}

bool Display::run_self_tests() {
    bool pass = true;

    if (active == DisplayBackend::VgaText) {
        if (!vga::text_self_test()) {
            kernel::kprintf("[TEST][FAIL] VGA text buffer read/write\n");
            pass = false;
        } else {
            kernel::kprintf("[TEST][PASS] VGA text buffer read/write\n");
        }
    } else if (caps.linear_framebuffer) {
        kernel::kprintf("[TEST][SKIP] VGA text buffer (linear framebuffer active)\n");
    } else {
        kernel::kprintf("[TEST][SKIP] VGA text buffer (no VGA backend)\n");
    }

    if (caps.linear_framebuffer && fb_info.phys_addr != 0) {
        if (active == DisplayBackend::BochsVbe && !vga::bochs_verify_mode(fb_info.width, fb_info.height, fb_info.bpp)) {
            kernel::kprintf("[TEST][FAIL] Bochs VBE DISPI register readback\n");
            pass = false;
        } else if (active == DisplayBackend::BochsVbe) {
            kernel::kprintf("[TEST][PASS] Bochs VBE DISPI register readback\n");
        }

        if (active != DisplayBackend::BochsVbe) {
            kernel::kprintf("[TEST][SKIP] Bochs VBE linear framebuffer pixel\n");
        } else if (vga::bochs_self_test(fb_info.phys_addr)) {
            kernel::kprintf("[TEST][PASS] Bochs VBE linear framebuffer pixel\n");
        } else {
            kernel::kprintf("[TEST][FAIL] Bochs VBE linear framebuffer pixel\n");
            pass = false;
        }
    } else {
        kernel::kprintf("[TEST][SKIP] Bochs VBE linear framebuffer pixel\n");
    }

    return pass;
}

} // namespace hal
