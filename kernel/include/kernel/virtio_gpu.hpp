#ifndef OMEGA_KERNEL_VIRTIO_GPU_HPP
#define OMEGA_KERNEL_VIRTIO_GPU_HPP

#include "arch/display.hpp"

namespace virtio_gpu {

bool init(hal::FramebufferInfo* out);
bool flush();
bool self_test();

} // namespace virtio_gpu

#endif
