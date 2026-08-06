#include "kernel/userland.hpp"
#include "kernel/kprint.hpp"

extern "C" void jump_to_userland(uintptr_t user_entry, uintptr_t user_stack) {
    (void)user_stack;
    auto fn = reinterpret_cast<void(*)()>(user_entry);
    fn();
}

namespace userland {

void UserlandManager::init() {
    kernel::kprintf("[+] Userland Mode Manager (Ring 3 / EL0) Initialized.\n");
}

void UserlandManager::enter_userland(uintptr_t user_entry, uintptr_t user_stack) {
    kernel::kprintf("[+] Transitioning CPU to Userland Privilege (Entry: %x, Stack: %x)\n", user_entry, user_stack);
    jump_to_userland(user_entry, user_stack);
}

} // namespace userland
