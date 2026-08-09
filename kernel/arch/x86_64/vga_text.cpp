#include "vga_internal.hpp"

namespace hal {
namespace vga {

static constexpr uintptr_t VGA_TEXT_BUFFER = 0x000B8000;
static constexpr uint32_t VGA_TEXT_COLS   = 80;
static constexpr uint32_t VGA_TEXT_ROWS   = 25;
static constexpr uint32_t VGA_TEXT_CELLS  = VGA_TEXT_COLS * VGA_TEXT_ROWS;

static volatile uint16_t* text_buffer() {
    return reinterpret_cast<volatile uint16_t*>(VGA_TEXT_BUFFER);
}

static uint16_t make_cell(char c, uint8_t attr) {
    return static_cast<uint16_t>(static_cast<uint8_t>(c)) |
           (static_cast<uint16_t>(attr) << 8);
}

bool text_buffer_accessible() {
    const uint8_t original_cursor = crtc_read(0x0F);
    crtc_write(0x0F, 0x55);
    const bool low_ok = crtc_read(0x0F) == 0x55;
    crtc_write(0x0F, 0xAA);
    const bool high_ok = crtc_read(0x0F) == 0xAA;
    crtc_write(0x0F, original_cursor);
    if (!low_ok || !high_ok) return false;
    volatile uint16_t* buf = text_buffer();
    const uint16_t original = buf[0];
    buf[0] = make_cell('T', 0x07);
    const bool ok = (buf[0] == make_cell('T', 0x07));
    buf[0] = original;
    return ok;
}

void text_set_cursor(uint8_t col, uint8_t row) {
    const uint16_t pos = static_cast<uint16_t>(row * VGA_TEXT_COLS + col);
    crtc_write(0x0F, static_cast<uint8_t>(pos & 0xFF));
    crtc_write(0x0E, static_cast<uint8_t>((pos >> 8) & 0xFF));
}

void text_clear(uint8_t attr) {
    volatile uint16_t* buf = text_buffer();
    const uint16_t blank = make_cell(' ', attr);
    for (uint32_t i = 0; i < VGA_TEXT_CELLS; ++i) {
        buf[i] = blank;
    }
    text_set_cursor(0, 0);
}

void text_putc(uint8_t col, uint8_t row, char c, uint8_t attr) {
    if (col >= VGA_TEXT_COLS || row >= VGA_TEXT_ROWS) {
        return;
    }
    text_buffer()[row * VGA_TEXT_COLS + col] = make_cell(c, attr);
}

void text_scroll_up(uint8_t attr) {
    volatile uint16_t* buf = text_buffer();
    for (uint32_t row = 1; row < VGA_TEXT_ROWS; ++row) {
        for (uint32_t col = 0; col < VGA_TEXT_COLS; ++col) {
            buf[(row - 1) * VGA_TEXT_COLS + col] = buf[row * VGA_TEXT_COLS + col];
        }
    }
    const uint16_t blank = make_cell(' ', attr);
    const uint32_t last_row = VGA_TEXT_ROWS - 1;
    for (uint32_t col = 0; col < VGA_TEXT_COLS; ++col) {
        buf[last_row * VGA_TEXT_COLS + col] = blank;
    }
}

uint16_t text_peek(uint8_t col, uint8_t row) {
    if (col >= VGA_TEXT_COLS || row >= VGA_TEXT_ROWS) {
        return 0;
    }
    return text_buffer()[row * VGA_TEXT_COLS + col];
}

bool text_self_test() {
    if (!text_buffer_accessible()) {
        return false;
    }
    text_putc(79, 24, 'X', 0x07);
    const bool ok = text_buffer()[24 * VGA_TEXT_COLS + 79] == make_cell('X', 0x07);
    text_putc(79, 24, ' ', 0x07);
    return ok;
}

uint32_t text_cols() { return VGA_TEXT_COLS; }
uint32_t text_rows() { return VGA_TEXT_ROWS; }

} // namespace vga
} // namespace hal
