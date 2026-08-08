#ifndef OMEGA_KERNEL_EXT4_HPP
#define OMEGA_KERNEL_EXT4_HPP

#include "kernel/storage.hpp"
#include "kernel/vfs.hpp"

namespace ext4 {

storage::Status mount(storage::Device* device, vfs::VfsNode** root);
bool mounted();

} // namespace ext4

#endif // OMEGA_KERNEL_EXT4_HPP
