#include "kernel/console.hpp"
#include "kernel/framebuffer.hpp"
#include "arch/display.hpp"
#include "arch/uart.hpp"

extern "C" void* memmove(void* dest, const void* src, size_t n);
extern "C" void* memset(void* s, int c, size_t n);

namespace display {

static uint32_t active_targets = CONSOLE_SERIAL;
static uint8_t text_col = 0;
static uint8_t text_row = 0;
static uint8_t text_attr = 0x07;

static uint32_t fb_col = 0;
static uint32_t fb_row = 0;
static uint32_t fb_cols = 0;
static uint32_t fb_rows = 0;

static constexpr uint32_t FB_FG = 0xFFFFFF;
static constexpr uint32_t FB_BG = 0x000000;

static void update_fb_grid() {
    const hal::FramebufferInfo* fb = hal::Display::framebuffer();
    if (fb == nullptr || fb->width == 0) {
        fb_cols = 0;
        fb_rows = 0;
        return;
    }
    fb_cols = fb->width / 8;
    fb_rows = fb->height / 16;
}

static void text_newline() {
    text_col = 0;
    ++text_row;
    if (text_row >= 25) {
        hal::Display::text_scroll_up(text_attr);
        text_row = 24;
    }
    hal::Display::text_set_cursor(text_col, text_row);
}

static void text_putchar_raw(char c) {
    if ((active_targets & CONSOLE_VGA_TEXT) == 0) {
        return;
    }
    if (!hal::Display::capabilities().text_mode) {
        return;
    }

    if (c == '\n') {
        text_newline();
        return;
    }
    if (c == '\r') {
        text_col = 0;
        hal::Display::text_set_cursor(text_col, text_row);
        return;
    }
    if (c == '\t') {
        text_col = static_cast<uint8_t>((text_col + 8) & ~7u);
        if (text_col >= 80) {
            text_newline();
        } else {
            hal::Display::text_set_cursor(text_col, text_row);
        }
        return;
    }

    hal::Display::text_putc(text_col, text_row, c, text_attr);
    ++text_col;
    if (text_col >= 80) {
        text_newline();
    } else {
        hal::Display::text_set_cursor(text_col, text_row);
    }
}

static void fb_scroll_up() {
    const hal::FramebufferInfo* fb = hal::Display::framebuffer();
    if (fb == nullptr || fb_rows <= 1 || fb->virt_addr == 0) {
        return;
    }

    const uint32_t glyph_h = 16;
    const size_t row_bytes = static_cast<size_t>(fb->pitch) * glyph_h;
    const size_t total_bytes = static_cast<size_t>(fb->pitch) * fb->height;
    auto* base = reinterpret_cast<uint8_t*>(fb->virt_addr);

    if (total_bytes > row_bytes) {
        memmove(base, base + row_bytes, total_bytes - row_bytes);
        memset(base + total_bytes - row_bytes, 0, row_bytes);
    }
}

static void fb_newline() {
    fb_col = 0;
    ++fb_row;
    if (fb_rows > 0 && fb_row >= fb_rows) {
        fb_scroll_up();
        fb_row = fb_rows - 1;
    }
}

static void fb_putchar_raw(char c) {
    if ((active_targets & CONSOLE_FRAMEBUFFER) == 0) {
        return;
    }
    if (!framebuffer_active()) {
        return;
    }

    if (c == '\n') {
        fb_newline();
        return;
    }
    if (c == '\r') {
        fb_col = 0;
        return;
    }
    if (c == '\t') {
        fb_col = (fb_col + 8) & ~7u;
        if (fb_cols > 0 && fb_col >= fb_cols) {
            fb_newline();
        }
        return;
    }

    if (c == 0x1B) {
        /* Minimal ANSI subset: ignore escape introducer for now. */
        return;
    }

    const uint32_t x = fb_col * 8;
    const uint32_t y = fb_row * 16;
    draw_char(x, y, c, FB_FG, FB_BG);

    ++fb_col;
    if (fb_cols > 0 && fb_col >= fb_cols) {
        fb_newline();
    }
}

void Console::init(uint32_t targets) {
    active_targets = targets;
    text_col = 0;
    text_row = 0;
    text_attr = 0x07;

    framebuffer_init();
    update_fb_grid();
    fb_col = 0;
    fb_row = 0;

    if ((targets & CONSOLE_VGA_TEXT) && hal::Display::capabilities().text_mode) {
        hal::Display::text_clear(text_attr);
        hal::Display::text_set_cursor(0, 0);
    }

    if ((targets & CONSOLE_FRAMEBUFFER) && framebuffer_active()) {
        const hal::FramebufferInfo* fb = hal::Display::framebuffer();
        if (fb != nullptr) {
            fill_rect(0, 0, fb->width, fb->height, FB_BG);
        }
    }
}

void Console::putchar(char c) {
    if ((active_targets & CONSOLE_SERIAL) != 0) {
        if (c == '\n') {
            hal::uart_putc('\r');
        }
        hal::uart_putc(c);
    }

    text_putchar_raw(c);
    fb_putchar_raw(c);
}

void Console::write(const char* s, size_t len) {
    if (s == nullptr) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        putchar(s[i]);
    }
}

void Console::clear() {
    text_col = 0;
    text_row = 0;
    fb_col = 0;
    fb_row = 0;

    if (hal::Display::capabilities().text_mode) {
        hal::Display::text_clear(text_attr);
    }

    if (framebuffer_active()) {
        const hal::FramebufferInfo* fb = hal::Display::framebuffer();
        if (fb != nullptr) {
            fill_rect(0, 0, fb->width, fb->height, FB_BG);
        }
    }
}

void Console::set_targets(uint32_t targets) {
    active_targets = targets;
}

uint32_t Console::targets() {
    return active_targets;
}

bool Console::self_test() {
    static constexpr char probe[] = "DISPLAY_CONSOLE_OK";
    static constexpr size_t len = sizeof(probe) - 1;

    const uint8_t saved_col = text_col;
    const uint8_t saved_row = text_row;
    const uint32_t saved_fb_col = fb_col;
    const uint32_t saved_fb_row = fb_row;

    write(probe, len);

    bool ok = true;

    if (framebuffer_active()) {
        const hal::FramebufferInfo* fb = hal::Display::framebuffer();
        if (fb != nullptr) {
            const uint32_t x = saved_fb_col * 8;
            const uint32_t y = saved_fb_row * 16;
            const uint8_t* base = reinterpret_cast<const uint8_t*>(fb->virt_addr);
            const uint32_t ppx = fb->bpp / 8;
            bool glyph_visible = false;
            for (uint32_t row = 0; row < 16 && !glyph_visible; ++row) {
                for (uint32_t col = 0; col < 8; ++col) {
                    const size_t offset =
                        static_cast<size_t>(y + row) * fb->pitch + (x + col) * ppx;
                    if (base[offset] != 0) {
                        glyph_visible = true;
                        break;
                    }
                }
            }
            if (!glyph_visible) {
                ok = false;
            }
        }
    } else if (hal::Display::capabilities().text_mode) {
        const uint16_t cell = hal::Display::text_peek(saved_col, saved_row);
        if (static_cast<char>(cell & 0xFF) != probe[0]) {
            ok = false;
        }
    }

    text_col = saved_col;
    text_row = saved_row;
    fb_col = saved_fb_col;
    fb_row = saved_fb_row;

    if (hal::Display::capabilities().text_mode) {
        hal::Display::text_set_cursor(text_col, text_row);
    }

    return ok;
}

} // namespace display
