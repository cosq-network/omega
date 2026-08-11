#ifndef OMEGA_KERNEL_USERLAND_HPP
#define OMEGA_KERNEL_USERLAND_HPP

#include "std/cstdint.hpp"

namespace userland {

class UserlandManager {
public:
    static void init();
    static void init_x86_syscall_stack();
    static void enter_userland(uintptr_t user_entry, uintptr_t user_stack);
};

} // namespace userland

extern "C" void jump_to_userland(uintptr_t user_entry, uintptr_t user_stack);

#endif // OMEGA_KERNEL_USERLAND_HPP
