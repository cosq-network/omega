#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

namespace hal {

struct IdtEntry {
    uint16_t isr_low;   // Lower 16 bits of ISR address
    uint16_t kernel_cs; // Kernel code segment selector
    uint8_t  ist;       // Interrupt Stack Table offset
    uint8_t  attributes;// Type and attributes
    uint16_t isr_mid;   // Middle 16 bits of ISR address
    uint32_t isr_high;  // Higher 32 bits of ISR address
    uint32_t reserved;  // Reserved
} __attribute__((packed));

struct IdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static IdtEntry idt[256];
static IdtPointer idtr;

void interrupts_init() {
    idtr.limit = sizeof(idt) - 1;
    idtr.base = reinterpret_cast<uint64_t>(&idt);

    // Populate empty IDT entries
    for (int i = 0; i < 256; ++i) {
        idt[i].isr_low = 0;
        idt[i].kernel_cs = 0x08;
        idt[i].ist = 0;
        idt[i].attributes = 0x8E; // Present, Ring 0, Interrupt Gate
        idt[i].isr_mid = 0;
        idt[i].isr_high = 0;
        idt[i].reserved = 0;
    }

    // Load IDTR register
    asm volatile("lidt %0" : : "m"(idtr));

    kernel::kprintf("[+] x86_64 Interrupt Descriptor Table (IDT) Initialized.\n");
    kernel::kprintf("    IDT Base: %x, Limit: %u\n", idtr.base, idtr.limit);
}

void interrupts_enable() {
    asm volatile("sti");
}

void interrupts_disable() {
    asm volatile("cli");
}

} // namespace hal
