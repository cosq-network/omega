#include "kernel/memory.hpp"
#include "kernel/kprint.hpp"

namespace memory {

uint8_t* PhysicalMemoryManager::bitmap = nullptr;
size_t PhysicalMemoryManager::total_frames = 0;
size_t PhysicalMemoryManager::free_frames = 0;
size_t PhysicalMemoryManager::bitmap_size = 0;
uintptr_t PhysicalMemoryManager::frame_base = 0;
uint16_t PhysicalMemoryManager::refcounts[16384]{};

void PhysicalMemoryManager::init(uintptr_t mem_start, size_t mem_size) {
    total_frames = mem_size / PAGE_SIZE;
    frame_base = mem_start;
#if defined(__x86_64__)
    // The bitmap is kept in kernel-owned storage so the kernel image and heap
    // cannot overwrite allocator metadata; x86 frames still begin at the
    // Keep allocations above the linked kernel image and the x86 initrd
    // handoff window (0x600000). The original 2 MiB base overlaps .bss and
    // silently corrupts allocator/process metadata.
    frame_base = 0x800000ull;
#endif
#if defined(__aarch64__)
    frame_base = 0x42000000ull;
#elif defined(__riscv)
    frame_base = 0x82000000ull;
#endif
    bitmap_size = total_frames / 8;

    bitmap = reinterpret_cast<uint8_t*>(mem_start);
    for (size_t i = 0; i < 16384; ++i) refcounts[i] = 0;

    // Mark all frames as allocated initially
    for (size_t i = 0; i < bitmap_size; ++i) {
        bitmap[i] = 0xFF;
    }

    // Start with every managed frame free, then reserve the low bootstrap
    // frames and the physical frames containing the bitmap itself.  The
    // bitmap is not necessarily located at frame zero in the linked kernel;
    // failing to reserve its actual location lets userspace overwrite the
    // allocator metadata as soon as that frame is allocated.
    free_frames = 0;
    for (size_t i = 0; i < total_frames; ++i) {
        clear_bit(i);
        ++free_frames;
    }
    const size_t bitmap_frames = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    auto reserve = [&](size_t first, size_t count) {
        for (size_t i = first; i < total_frames && i - first < count; ++i) {
            if (!test_bit(i)) {
                set_bit(i);
                --free_frames;
            }
        }
    };
    reserve(0, bitmap_frames);
    if (mem_start >= frame_base) {
        reserve((mem_start - frame_base) / PAGE_SIZE, bitmap_frames);
    }

    kernel::kprintf("[+] Physical Memory Manager initialized.\n");
    kernel::kprintf("    Total Frames: %u (%u MB)\n", total_frames, (total_frames * PAGE_SIZE) / (1024 * 1024));
    kernel::kprintf("    Bitmap Size: %u bytes (%u frames)\n", bitmap_size, bitmap_frames);
}

uintptr_t PhysicalMemoryManager::alloc_frame() {
    for (size_t i = 0; i < total_frames; ++i) {
        if (!test_bit(i)) {
            set_bit(i);
            refcounts[i] = 1;
            --free_frames;
            return frame_base + i * PAGE_SIZE;
        }
    }
    kernel::kprintf("[!] Out of Physical Memory Frames!\n");
    return 0; // Out of memory
}

void PhysicalMemoryManager::retain_frame(uintptr_t frame_addr) {
    if (frame_addr < frame_base) return;
    const size_t frame = (frame_addr - frame_base) / PAGE_SIZE;
    if (frame < total_frames && test_bit(frame) && refcounts[frame] != 0xffff) ++refcounts[frame];
}

void PhysicalMemoryManager::free_frame(uintptr_t frame_addr) {
    if (frame_addr < frame_base) return;
    size_t frame = (frame_addr - frame_base) / PAGE_SIZE;
    if (frame < total_frames && test_bit(frame) && refcounts[frame] > 1) {
        --refcounts[frame];
    } else if (frame < total_frames && test_bit(frame)) {
        refcounts[frame] = 0;
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
