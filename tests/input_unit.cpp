#include "kernel/input.hpp"

static bool expect(bool condition) { return condition; }

int main() {
    if (!expect(sizeof(input::InputEvent) == 64)) return 1;
    input::Manager::init();
    if (!expect(input::Manager::self_test())) return 2;
    input::Manager::emit(input::EVENT_KEY, 7, input::KEY_ENTER, input::FLAG_PRESSED);
    if (!expect(input::Manager::available() == 1)) return 3;
    input::InputEvent event{};
    if (!expect(input::Manager::read(&event, 1) == 1 && event.device_id == 7 &&
                event.code == input::KEY_ENTER && event.version == input::ABI_VERSION)) return 4;
    uint8_t previous[6]{};
    uint8_t keyboard[8]{0, 0, 4, 0, 0, 0, 0, 0};
    input::InputEvent decoded[4]{};
    if (!expect(input::decode_boot_keyboard(keyboard, 8, previous, decoded, 4) == 1 &&
                decoded[0].code == 4 && decoded[0].flags == input::FLAG_PRESSED)) return 5;
    uint8_t mouse[3]{1, 4, 0};
    if (!expect(input::decode_boot_mouse(mouse, 3, 0, decoded, 4) == 2 &&
                decoded[0].type == input::EVENT_BUTTON && decoded[1].value0 == 4)) return 6;
    input::Ps2Decoder ps2;
    ps2.keyboard_byte(0x1c);
    if (!expect(input::Manager::available() == 1)) return 7;
    return 0;
}
