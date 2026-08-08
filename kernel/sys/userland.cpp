#include "kernel/userland.hpp"
#include "kernel/kprint.hpp"

extern "C" void jump_to_userland(uintptr_t user_entry, uintptr_t user_stack) {
    // A C++ call is not a privilege transition: it would execute an
    // untrusted entry point in the kernel address space.  Architecture
    // specific EL0/Ring-3 entry is enabled only once its trap frame, user
    // selectors, and address-space activation are installed.
    (void)user_entry;
    (void)user_stack;
    kernel::kprintf("[!] Userland entry rejected: native privilege transition is not installed.\n");
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
