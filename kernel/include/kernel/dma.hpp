#ifndef OMEGA_KERNEL_DMA_HPP
#define OMEGA_KERNEL_DMA_HPP

#include "std/cstdint.hpp"

namespace dma {

enum Flags : uint32_t {
    DMA_COHERENT = 1u << 0,
    DMA_LOW      = 1u << 1,
    DMA_READ     = 1u << 2,
    DMA_WRITE    = 1u << 3
};

struct Buffer {
    uintptr_t virtual_address;
    uintptr_t physical_address;
    uintptr_t allocation_base;
    size_t size;
    size_t alignment;
    uint32_t flags;
};

void init();
bool alloc(Buffer* buffer, size_t size, size_t alignment, uint32_t flags);
bool map(void* address, size_t size, Buffer* buffer, uint32_t flags);
void sync_for_device(Buffer* buffer);
void sync_for_cpu(Buffer* buffer);
void free(Buffer* buffer);

} // namespace dma

#endif // OMEGA_KERNEL_DMA_HPP
