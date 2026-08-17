#ifndef OMEGA_KERNEL_USERLAND_HPP
#define OMEGA_KERNEL_USERLAND_HPP

#include "std/cstdint.hpp"

namespace userland {

class UserlandManager {
public:
    static void init();
    static void init_x86_syscall_stack();
    static void init_aarch64_exception_stack();
    static void init_riscv_exception_stack();
    static void enter_userland(uintptr_t user_entry, uintptr_t user_stack);
    static void enter_userland(uintptr_t user_entry, uintptr_t user_stack, uintptr_t tls_base);
    static void enter_userland_from_syscall(uintptr_t user_entry, uintptr_t user_stack, uintptr_t tls_base);
    static void set_fs_base(uintptr_t base);
};

} // namespace userland

extern "C" void jump_to_userland(uintptr_t user_entry, uintptr_t user_stack);
extern "C" void jump_to_userland_tls(uintptr_t user_entry, uintptr_t user_stack, uintptr_t tls_base);
extern "C" void aarch64_enter_userland(uintptr_t user_entry, uintptr_t user_stack);
extern "C" void aarch64_enter_userland_tls(uintptr_t user_entry, uintptr_t user_stack, uintptr_t tls_base);
extern "C" void riscv_enter_userland(uintptr_t user_entry, uintptr_t user_stack);
extern "C" void riscv_enter_userland_tls(uintptr_t user_entry, uintptr_t user_stack, uintptr_t tls_base);

#endif // OMEGA_KERNEL_USERLAND_HPP
