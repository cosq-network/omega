#include "arch/input.hpp"
#include "kernel/kprint.hpp"
namespace hal {
void input_init() { kernel::kprintf("[+] AArch64 portable input adapter initialized (synthetic/HID-ready).\n"); }
void input_poll() {}
}
