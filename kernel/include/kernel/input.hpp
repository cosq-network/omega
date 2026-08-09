#ifndef OMEGA_KERNEL_INPUT_HPP
#define OMEGA_KERNEL_INPUT_HPP

#include "std/cstdint.hpp"

namespace input {

// This is a deliberately fixed-width, versioned ABI. It is shared by kernel
// drivers and userspace, independent of the host compiler or architecture.
struct InputEvent {
    uint16_t version;
    uint16_t size;
    uint32_t type;
    uint32_t device_id;
    uint32_t reserved0;
    uint64_t sequence;
    uint64_t timestamp;
    uint16_t code;
    uint16_t flags;
    int32_t value0;
    int32_t value1;
    int32_t value2;
    uint32_t reserved[4];
} __attribute__((packed));

static_assert(sizeof(InputEvent) == 64, "InputEvent ABI must remain 64 bytes");

enum : uint16_t { ABI_VERSION = 1 };
enum EventType : uint32_t {
    EVENT_KEY = 1,
    EVENT_REL = 2,
    EVENT_BUTTON = 3,
    EVENT_DEVICE = 4,
    EVENT_SYN = 5,
};
enum EventFlags : uint16_t {
    FLAG_PRESSED = 1u << 0,
    FLAG_RELEASED = 1u << 1,
    FLAG_REPEAT = 1u << 2,
    FLAG_MODIFIER = 1u << 3,
};
enum KeyCode : uint16_t {
    KEY_ESC = 0x29, KEY_ENTER = 0x28, KEY_BACKSPACE = 0x2a,
    KEY_TAB = 0x2b, KEY_SPACE = 0x2c,
    KEY_LEFTCTRL = 0xe0, KEY_LEFTSHIFT = 0xe1, KEY_LEFTALT = 0xe2,
    KEY_RIGHTCTRL = 0xe4, KEY_RIGHTSHIFT = 0xe5, KEY_RIGHTALT = 0xe6,
    KEY_UP = 0x52, KEY_DOWN = 0x51, KEY_LEFT = 0x50, KEY_RIGHT = 0x4f,
};
enum RelativeCode : uint16_t { REL_X = 0, REL_Y = 1, REL_WHEEL = 8 };

class EventQueue {
public:
    static constexpr size_t CAPACITY = 256;
    bool push(const InputEvent& event);
    size_t read(InputEvent* events, size_t capacity);
    size_t available() const;
    void clear();

private:
    InputEvent events_[CAPACITY]{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
};

class Manager {
public:
    static void init();
    static bool emit(uint32_t type, uint32_t device_id, uint16_t code,
                     uint16_t flags, int32_t value0 = 0, int32_t value1 = 0,
                     int32_t value2 = 0);
    static size_t read(InputEvent* events, size_t capacity);
    static size_t available();
    static int64_t subscribe(uint64_t mask);
    static uint64_t subscription();
    static bool self_test();
    static EventQueue& queue();
};

// Decode standard USB HID boot reports. This is also the portable contract
// used by future USB transports on all three supported architectures.
size_t decode_boot_keyboard(const uint8_t* report, size_t length,
                            const uint8_t* previous_keys, InputEvent* events,
                            size_t capacity);
size_t decode_boot_mouse(const uint8_t* report, size_t length,
                         uint8_t previous_buttons, InputEvent* events,
                         size_t capacity);

class Ps2Decoder {
public:
    void keyboard_byte(uint8_t byte);
    void mouse_byte(uint8_t byte);
private:
    bool extended_ = false;
    uint8_t mouse_packet_[3]{};
    uint8_t mouse_index_ = 0;
};

} // namespace input

#endif
