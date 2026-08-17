#ifndef OMEGA_KERNEL_TMPFS_HPP
#define OMEGA_KERNEL_TMPFS_HPP

#include "std/cstdint.hpp"

namespace vfs { struct VfsNode; }

namespace tmpfs {

// Mount an in-memory writable filesystem at `path` (e.g. "/tmp") under the
// VFS root. Returns true on success. All files created under the mount live
// in kernel heap memory (no backing block device), which is exactly what an
// on-target compiler needs for scratch output.
bool mount(const char* path);

} // namespace tmpfs

#endif // OMEGA_KERNEL_TMPFS_HPP
