#include "kernel/input.hpp"

namespace input {
namespace {
EventQueue g_queue;
uint64_t g_sequence = 0;
uint64_t g_subscription = ~0ull;

static bool contains(const uint8_t* keys, uint8_t key) {
    for (size_t i = 0; i < 6; ++i) if (keys[i] == key) return true;
    return false;
}

static uint16_t ps2_key(uint8_t code, bool extended) {
    if (extended) {
        switch (code) { case 0x48: return KEY_UP; case 0x50: return KEY_DOWN;
            case 0x4b: return KEY_LEFT; case 0x4d: return KEY_RIGHT; default: return 0; }
    }
    if (code >= 0x1e && code <= 0x26) return static_cast<uint16_t>(0x04 + code - 0x1e);
    if (code >= 0x10 && code <= 0x19) return static_cast<uint16_t>(0x14 + code - 0x10);
    if (code >= 0x02 && code <= 0x0b) return static_cast<uint16_t>(0x1e + code - 0x02);
    switch (code) {
        case 0x01: return KEY_ESC; case 0x1c: return KEY_ENTER; case 0x0e: return KEY_BACKSPACE;
        case 0x0f: return KEY_TAB; case 0x39: return KEY_SPACE; case 0x2a: return KEY_LEFTSHIFT;
        case 0x36: return KEY_RIGHTSHIFT; case 0x1d: return KEY_LEFTCTRL; case 0x38: return KEY_LEFTALT;
        default: return 0;
    }
}
}

bool EventQueue::push(const InputEvent& event) {
    if (count_ == CAPACITY) { tail_ = (tail_ + 1) % CAPACITY; --count_; }
    events_[head_] = event; head_ = (head_ + 1) % CAPACITY; ++count_; return true;
}
size_t EventQueue::read(InputEvent* events, size_t capacity) {
    if (!events || capacity == 0) return 0;
    size_t n = capacity < count_ ? capacity : count_;
    for (size_t i = 0; i < n; ++i) { events[i] = events_[tail_]; tail_ = (tail_ + 1) % CAPACITY; }
    count_ -= n; return n;
}
size_t EventQueue::available() const { return count_; }
void EventQueue::clear() { head_ = tail_ = count_ = 0; }

void Manager::init() { g_queue.clear(); g_sequence = 0; g_subscription = ~0ull; }
bool Manager::emit(uint32_t type, uint32_t device_id, uint16_t code, uint16_t flags,
                  int32_t value0, int32_t value1, int32_t value2) {
    if (type >= 64 || ((g_subscription >> type) & 1u) == 0) return false;
    InputEvent e{}; e.version = ABI_VERSION; e.size = sizeof(InputEvent); e.type = type;
    e.device_id = device_id; e.sequence = ++g_sequence; e.code = code; e.flags = flags;
    e.value0 = value0; e.value1 = value1; e.value2 = value2; return g_queue.push(e);
}
size_t Manager::read(InputEvent* events, size_t capacity) { return g_queue.read(events, capacity); }
size_t Manager::available() { return g_queue.available(); }
int64_t Manager::subscribe(uint64_t mask) { uint64_t old = g_subscription; g_subscription = mask; return static_cast<int64_t>(old); }
uint64_t Manager::subscription() { return g_subscription; }
EventQueue& Manager::queue() { return g_queue; }

size_t decode_boot_keyboard(const uint8_t* report, size_t length, const uint8_t* previous,
                            InputEvent* events, size_t capacity) {
    if (!report || length < 8 || !events || !previous) return 0;
    size_t n = 0; uint8_t modifiers = report[0];
    for (size_t i = 0; i < 6; ++i) { uint8_t key = report[2 + i]; if (!key) continue;
        if (!contains(previous, key) && n < capacity) { events[n++] = {}; events[n-1].type = EVENT_KEY; events[n-1].code = key; events[n-1].flags = FLAG_PRESSED; }
    }
    for (size_t i = 0; i < 6; ++i) { uint8_t key = previous[i]; if (!key) continue;
        if (!contains(report + 2, key) && n < capacity) { events[n++] = {}; events[n-1].type = EVENT_KEY; events[n-1].code = key; events[n-1].flags = FLAG_RELEASED; }
    }
    if (modifiers && n < capacity) {
        events[n] = {}; events[n].type = EVENT_KEY; events[n].code = 0;
        events[n].flags = FLAG_MODIFIER; events[n].value0 = modifiers; ++n;
    }
    return n;
}

size_t decode_boot_mouse(const uint8_t* report, size_t length, uint8_t previous_buttons,
                         InputEvent* events, size_t capacity) {
    if (!report || length < 3 || !events || capacity == 0) return 0;
    size_t n = 0; uint8_t buttons = report[0] & 7;
    for (uint8_t bit = 0; bit < 3 && n < capacity; ++bit) if (((buttons ^ previous_buttons) >> bit) & 1) {
        events[n] = {}; events[n].type = EVENT_BUTTON; events[n].code = bit;
        events[n].flags = (buttons & (1u << bit)) ? FLAG_PRESSED : FLAG_RELEASED; ++n;
    }
    if (n < capacity && report[1]) { events[n] = {}; events[n].type = EVENT_REL; events[n].code = REL_X; events[n].value0 = static_cast<int8_t>(report[1]); ++n; }
    if (n < capacity && report[2]) { events[n] = {}; events[n].type = EVENT_REL; events[n].code = REL_Y; events[n].value0 = -static_cast<int8_t>(report[2]); ++n; }
    return n;
}

void Ps2Decoder::keyboard_byte(uint8_t byte) {
    if (byte == 0xe0) { extended_ = true; return; }
    bool released = (byte & 0x80) != 0; uint8_t code = byte & 0x7f; uint16_t key = ps2_key(code, extended_); extended_ = false;
    if (key) Manager::emit(EVENT_KEY, 1, key, released ? FLAG_RELEASED : FLAG_PRESSED,
                           released ? 0 : 1, 0, 0);
}
void Ps2Decoder::mouse_byte(uint8_t byte) {
    if (mouse_index_ == 0 && (byte & 8) == 0) return;
    mouse_packet_[mouse_index_++] = byte;
    if (mouse_index_ == 3) { int dx = static_cast<int8_t>(mouse_packet_[1]); int dy = -static_cast<int8_t>(mouse_packet_[2]);
        if (dx) Manager::emit(EVENT_REL, 2, REL_X, 0, dx); if (dy) Manager::emit(EVENT_REL, 2, REL_Y, 0, dy);
        static uint8_t buttons = 0; uint8_t now = mouse_packet_[0] & 7;
        for (uint8_t bit = 0; bit < 3; ++bit) if (((buttons ^ now) >> bit) & 1)
            Manager::emit(EVENT_BUTTON, 2, bit, (now & (1u << bit)) ? FLAG_PRESSED : FLAG_RELEASED);
        buttons = now; mouse_index_ = 0;
    }
}

bool Manager::self_test() {
    init();
    uint8_t old[6]; for (size_t i = 0; i < 6; ++i) old[i] = 0;
    uint8_t report[8]; for (size_t i = 0; i < 8; ++i) report[i] = 0; report[2] = 4;
    InputEvent out[4];
    size_t n = decode_boot_keyboard(report, 8, old, out, 4);
    return n == 1 && out[0].type == EVENT_KEY && out[0].code == 4;
}
}
