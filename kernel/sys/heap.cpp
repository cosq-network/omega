#include "kernel/heap.hpp"
#include "kernel/kprint.hpp"

namespace memory {

HeapAllocator::BlockHeader* HeapAllocator::free_list_head = nullptr;

void HeapAllocator::init(uintptr_t heap_start, size_t heap_size) {
    free_list_head = reinterpret_cast<BlockHeader*>(heap_start);
    free_list_head->size = heap_size - sizeof(BlockHeader);
    free_list_head->is_free = true;
    free_list_head->next = nullptr;

    kernel::kprintf("[+] Kernel Heap Allocator initialized.\n");
    kernel::kprintf("    Heap Start: %x, Total Heap Size: %u bytes\n", heap_start, heap_size);
}

void* HeapAllocator::kmalloc(size_t size) {
    if (size == 0) return nullptr;

    // Align size to 8 bytes
    size = (size + 7) & ~7;

    BlockHeader* current = free_list_head;
    while (current) {
        if (current->is_free && current->size >= size) {
            // Can block be split?
            if (current->size >= size + sizeof(BlockHeader) + 16) {
                BlockHeader* next_block = reinterpret_cast<BlockHeader*>(
                    reinterpret_cast<uintptr_t>(current) + sizeof(BlockHeader) + size
                );
                next_block->size = current->size - size - sizeof(BlockHeader);
                next_block->is_free = true;
                next_block->next = current->next;

                current->size = size;
                current->next = next_block;
            }
            current->is_free = false;
            return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(current) + sizeof(BlockHeader));
        }
        current = current->next;
    }

    kernel::kprintf("[!] Out of Kernel Heap Memory!\n");
    return nullptr;
}

void HeapAllocator::kfree(void* ptr) {
    if (!ptr) return;

    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<uintptr_t>(ptr) - sizeof(BlockHeader)
    );
    header->is_free = true;

    // Coalesce adjacent free blocks
    BlockHeader* current = free_list_head;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            current->size += sizeof(BlockHeader) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

} // namespace memory

extern "C" {
    void* kmalloc(size_t size) {
        return memory::HeapAllocator::kmalloc(size);
    }
    void kfree(void* ptr) {
        memory::HeapAllocator::kfree(ptr);
    }
}
