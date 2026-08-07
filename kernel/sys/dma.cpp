#include "kernel/dma.hpp"
#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"

namespace dma {
namespace {
static uintptr_t align_up(uintptr_t value, size_t alignment) {
    if (alignment == 0) alignment = 1;
    return (value + alignment - 1) & ~(alignment - 1);
}
}

void init() {
    kernel::kprintf("[+] Storage DMA abstraction initialized.\n");
}

bool alloc(Buffer* buffer, size_t size, size_t alignment, uint32_t flags) {
    if (!buffer || size == 0 || alignment == 0 || (alignment & (alignment - 1))) return false;
    const size_t total = size + alignment - 1;
    void* base = kmalloc(total);
    if (!base) return false;
    const uintptr_t address = align_up(reinterpret_cast<uintptr_t>(base), alignment);
    if ((flags & DMA_LOW) && address > 0xFFFFFFFFull) {
        kfree(base);
        return false;
    }
    buffer->virtual_address = address;
    buffer->physical_address = address;
    buffer->allocation_base = reinterpret_cast<uintptr_t>(base);
    buffer->size = size;
    buffer->alignment = alignment;
    buffer->flags = flags;
    return true;
}

bool map(void* address, size_t size, Buffer* buffer, uint32_t flags) {
    if (!address || !buffer || size == 0) return false;
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    buffer->virtual_address = value;
    // Early bring-up uses an identity physical map. Keep the byte offset;
    // hardware buffers are not required to begin on a page boundary.
    buffer->physical_address = value;
    buffer->allocation_base = 0;
    buffer->size = size;
    buffer->alignment = 1;
    buffer->flags = flags;
    return true;
}

void sync_for_device(Buffer* buffer) {
    if (!buffer) return;
#if defined(__aarch64__)
    asm volatile("dsb sy" ::: "memory");
#elif defined(__riscv)
    asm volatile("fence rw, rw" ::: "memory");
#else
    asm volatile("mfence" ::: "memory");
#endif
}

void sync_for_cpu(Buffer* buffer) {
    sync_for_device(buffer);
}

void free(Buffer* buffer) {
    if (!buffer) return;
    if (buffer->allocation_base) kfree(reinterpret_cast<void*>(buffer->allocation_base));
    buffer->virtual_address = 0;
    buffer->physical_address = 0;
    buffer->allocation_base = 0;
    buffer->size = 0;
}

} // namespace dma
