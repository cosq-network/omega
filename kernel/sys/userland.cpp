#include "kernel/userland.hpp"
#include "kernel/kprint.hpp"

namespace userland {

void UserlandManager::init() {
    kernel::kprintf("[+] Userland Mode Manager (Ring 3 / EL0) Initialized.\n");
}

void UserlandManager::enter_userland(uintptr_t user_entry, uintptr_t user_stack) {
    kernel::kprintf("[+] Transitioning CPU to Userland Privilege (Entry: %x, Stack: %x)\n", user_entry, user_stack);
    jump_to_userland(user_entry, user_stack);
}

} // namespace userland
