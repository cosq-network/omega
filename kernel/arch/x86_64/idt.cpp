#include "arch/interrupts.hpp"
#include "kernel/kprint.hpp"

extern "C" void x86_timer_interrupt_stub();

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

static void set_gate(uint8_t vector, uintptr_t handler) {
    idt[vector].isr_low = static_cast<uint16_t>(handler & 0xffff);
    idt[vector].isr_mid = static_cast<uint16_t>((handler >> 16) & 0xffff);
    idt[vector].isr_high = static_cast<uint32_t>(handler >> 32);
}

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

    set_gate(32, reinterpret_cast<uintptr_t>(&x86_timer_interrupt_stub));

    // Load IDTR register
    asm volatile("lidt %0" : : "m"(idtr));

    kernel::kprintf("[+] x86_64 Interrupt Descriptor Table (IDT) Initialized.\n");
    kernel::kprintf("    IDT Base: %x, Limit: %u\n", idtr.base, idtr.limit);
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    const uint32_t raw_divisor = 1193182u / frequency_hz;
    const uint16_t divisor = static_cast<uint16_t>(raw_divisor > 0xffff ? 0xffff : raw_divisor);

    // Remap the legacy PIC: master IRQs 0..7 -> 32..39, slave -> 40..47.
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x11)), "Nd"(0x20));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x11)), "Nd"(0xA0));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x20)), "Nd"(0x21));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x28)), "Nd"(0xA1));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x04)), "Nd"(0x21));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x02)), "Nd"(0xA1));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x01)), "Nd"(0x21));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x01)), "Nd"(0xA1));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0xFE)), "Nd"(0x21));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0xFF)), "Nd"(0xA1));

    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x36)), "Nd"(0x43));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(divisor & 0xff)), "Nd"(0x40));
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(divisor >> 8)), "Nd"(0x40));
    kernel::kprintf("[+] x86_64 PIT timer initialized at %u Hz (IRQ0/vector 32).\n", frequency_hz);
}

void interrupts_enable() {
    asm volatile("sti");
}

void interrupts_disable() {
    asm volatile("cli");
}

} // namespace hal
