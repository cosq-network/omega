#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"

namespace memory {

uint8_t* PhysicalMemoryManager::bitmap = nullptr;
size_t PhysicalMemoryManager::total_frames = 0;
size_t PhysicalMemoryManager::free_frames = 0;
size_t PhysicalMemoryManager::bitmap_size = 0;

void PhysicalMemoryManager::init(uintptr_t mem_start, size_t mem_size) {
    total_frames = mem_size / PAGE_SIZE;
    free_frames = total_frames;
    bitmap_size = total_frames / 8;

    bitmap = reinterpret_cast<uint8_t*>(mem_start);

    // Mark all frames as allocated initially
    for (size_t i = 0; i < bitmap_size; ++i) {
        bitmap[i] = 0xFF;
    }

    // Reserve bitmap memory frames
    size_t bitmap_frames = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = bitmap_frames; i < total_frames; ++i) {
        clear_bit(i);
    }

    kernel::kprintf("[+] Physical Memory Manager initialized.\n");
    kernel::kprintf("    Total Frames: %u (%u MB)\n", total_frames, (total_frames * PAGE_SIZE) / (1024 * 1024));
    kernel::kprintf("    Bitmap Size: %u bytes (%u frames)\n", bitmap_size, bitmap_frames);
}

uintptr_t PhysicalMemoryManager::alloc_frame() {
    for (size_t i = 0; i < total_frames; ++i) {
        if (!test_bit(i)) {
            set_bit(i);
            --free_frames;
            return i * PAGE_SIZE;
        }
    }
    kernel::kprintf("[!] Out of Physical Memory Frames!\n");
    return 0; // Out of memory
}

void PhysicalMemoryManager::free_frame(uintptr_t frame_addr) {
    size_t frame = frame_addr / PAGE_SIZE;
    if (frame < total_frames && test_bit(frame)) {
        clear_bit(frame);
        ++free_frames;
    }
}

size_t PhysicalMemoryManager::get_free_frames() {
    return free_frames;
}

size_t PhysicalMemoryManager::get_total_frames() {
    return total_frames;
}

} // namespace memory
