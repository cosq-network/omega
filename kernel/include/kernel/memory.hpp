#ifndef OMEGA_KERNEL_MEMORY_HPP
#define OMEGA_KERNEL_MEMORY_HPP

#include "std/cstdint.hpp"

namespace memory {

static constexpr uint64_t PAGE_SIZE = 4096; // 4KiB Physical Frame Size

class PhysicalMemoryManager {
public:
    static void init(uintptr_t mem_start, size_t mem_size);
    static uintptr_t alloc_frame();
    static void retain_frame(uintptr_t frame_addr);
    static void free_frame(uintptr_t frame_addr);
    static size_t get_free_frames();
    static size_t get_total_frames();

private:
    static uint8_t* bitmap;
    static size_t total_frames;
    static size_t free_frames;
    static size_t bitmap_size;
    static uintptr_t frame_base;
    static uint16_t refcounts[16384];

    static inline void set_bit(size_t bit) {
        bitmap[bit / 8] |= (1 << (bit % 8));
    }

    static inline void clear_bit(size_t bit) {
        bitmap[bit / 8] &= ~(1 << (bit % 8));
    }

    static inline bool test_bit(size_t bit) {
        return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
    }
};

} // namespace memory

#endif // OMEGA_KERNEL_MEMORY_HPP
