#ifndef OMEGA_KERNEL_HEAP_HPP
#define OMEGA_KERNEL_HEAP_HPP

#include "std/cstdint.hpp"

namespace memory {

class HeapAllocator {
public:
    static void init(uintptr_t heap_start, size_t heap_size);
    static void* kmalloc(size_t size);
    static void kfree(void* ptr);

private:
    struct BlockHeader {
        size_t size;
        bool is_free;
        BlockHeader* next;
    };

    static BlockHeader* free_list_head;
};

} // namespace memory

extern "C" {
    void* kmalloc(size_t size);
    void kfree(void* ptr);
}

#endif // OMEGA_KERNEL_HEAP_HPP
