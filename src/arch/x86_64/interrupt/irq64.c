/**
 * @file irq64.c
 * @brief Hardware Interrupt Request Implementation (x86_64)
 * 
 * This file implements IRQ handling for x86_64 using the legacy PIC.
 * APIC support is implemented separately in apic.c.
 * 
 * Requirements: 6.3 - Configure PIC/APIC on x86
 */

#include "irq64.h"
#include "isr64.h"
#include "idt64.h"
#include "gdt64.h"
#include <kernel/io.h>
#include <kernel/sync/spinlock.h>
#include <lib/klog.h>

/* ============================================================================
 * PIC Constants
 * ========================================================================== */

/* PIC ports */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* PIC commands */
#define PIC_EOI         0x20    /* End of Interrupt */

/* ICW (Initialization Command Words) */
#define ICW1_ICW4       0x01    /* ICW4 needed */
#define ICW1_INIT       0x10    /* Initialization command */
#define ICW4_8086       0x01    /* 8086/88 mode */

/* ============================================================================
 * Static Data
 * ========================================================================== */

/* IRQ handler functions (for hardware IRQs 0-15) */
static isr_handler_t irq_handlers[16] = {0};

/* Spinlock for IRQ handler registration */
static spinlock_t irq_registry_lock;
static bool irq_registry_lock_initialized = false;

/* IRQ statistics */
static uint64_t irq_counts[16] = {0};

/* Timer tick counter */
static volatile uint64_t timer_ticks = 0;

/* ============================================================================
 * PIC Functions
 * ========================================================================== */

/**
 * @brief Remap PIC to avoid conflict with CPU exceptions
 * 
 * Maps IRQ 0-15 to interrupt vectors 32-47.
 */
static void pic_remap(void) {
    uint8_t mask1, mask2;

    /* Save current interrupt masks */
    mask1 = inb(PIC1_DATA);
    mask2 = inb(PIC2_DATA);

    /* Start initialization sequence (cascade mode) */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    /* ICW2: Set interrupt vector offsets */
    outb(PIC1_DATA, 32);    /* Master PIC: IRQ 0-7 -> INT 32-39 */
    outb(PIC2_DATA, 40);    /* Slave PIC: IRQ 8-15 -> INT 40-47 */

    /* ICW3: Set cascade configuration */
    outb(PIC1_DATA, 0x04);  /* Master: slave on IRQ 2 */
    outb(PIC2_DATA, 0x02);  /* Slave: cascade identity 2 */

    /* ICW4: Set mode */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    /* Restore interrupt masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/**
 * @brief Send End of Interrupt signal to PIC
 */
static void pic_send_eoi(uint8_t irq) {
    /* If IRQ came from slave PIC (8-15), send EOI to both */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}

/* ============================================================================
 * Timer Handler
 * ========================================================================== */

/**
 * @brief Timer interrupt handler (IRQ 0)
 */
static void timer_handler(registers_t *regs) {
    (void)regs;
    timer_ticks++;
    
    /* Call task manager timer tick handler */
    extern void task_timer_tick(void);
    task_timer_tick();
}

/* Forward declaration for scheduler */
extern void schedule_from_irq(registers_t *regs);

/* ============================================================================
 * IRQ Handler
 * ========================================================================== */

/**
 * @brief Common IRQ handler (called from assembly)
 */
void irq64_handler(registers_t *regs) {
    /* Calculate IRQ number (interrupt number - 32) */
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    /* Update statistics */
    if (irq < 16) {
        irq_counts[irq]++;
    }

    /* Call registered handler if present */
    if (irq < 16 && irq_handlers[irq] != 0) {
        isr_handler_t handler = irq_handlers[irq];
        handler(regs);
    } else {
        LOG_WARN_MSG("Unhandled IRQ %u (interrupt %llu)\n", irq, regs->int_no);
    }

    /* Send EOI signal */
    pic_send_eoi(irq);

    /* Try to trigger scheduler after EOI */
    schedule_from_irq(regs);
}

/* ============================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Register an IRQ handler
 */
void irq64_register_handler(uint8_t irq, isr_handler_t handler) {
    if (irq >= 16) {
        return;
    }
    
    /* Ensure lock is initialized */
    if (!irq_registry_lock_initialized) {
        spinlock_init(&irq_registry_lock);
        irq_registry_lock_initialized = true;
    }
    
    /* Use IRQ-safe spinlock */
    bool irq_state;
    spinlock_lock_irqsave(&irq_registry_lock, &irq_state);
    irq_handlers[irq] = handler;
    spinlock_unlock_irqrestore(&irq_registry_lock, irq_state);
}

/**
 * @brief Get port for IRQ line
 */
static inline uint16_t irq_get_port(uint8_t irq) {
    return (irq < 8) ? PIC1_DATA : PIC2_DATA;
}

/**
 * @brief Disable (mask) an IRQ line
 */
void irq64_disable_line(uint8_t irq) {
    if (irq >= 16) {
        return;
    }

    uint16_t port = irq_get_port(irq);
    if (irq >= 8) {
        irq -= 8;
    }

    uint8_t value = inb(port);
    value |= (uint8_t)(1u << irq);
    outb(port, value);
}

/**
 * @brief Enable (unmask) an IRQ line
 */
void irq64_enable_line(uint8_t irq) {
    if (irq >= 16) {
        return;
    }

    uint16_t port = irq_get_port(irq);
    if (irq >= 8) {
        irq -= 8;
    }

    uint8_t value = inb(port);
    value &= (uint8_t)~(1u << irq);
    outb(port, value);
}

/**
 * @brief Initialize IRQ subsystem
 */
void irq64_init(void) {
    LOG_INFO_MSG("Initializing x86_64 IRQ...\n");

    /* Initialize IRQ registry lock */
    spinlock_init(&irq_registry_lock);
    irq_registry_lock_initialized = true;
    LOG_DEBUG_MSG("  IRQ registry lock initialized\n");

    /* Remap PIC */
    pic_remap();
    LOG_DEBUG_MSG("  PIC remapped (IRQ 0-15 -> INT 32-47)\n");

    /* Register all IRQ handlers in IDT (32-47) */
    idt64_set_interrupt_gate(32, (uint64_t)irq0);
    idt64_set_interrupt_gate(33, (uint64_t)irq1);
    idt64_set_interrupt_gate(34, (uint64_t)irq2);
    idt64_set_interrupt_gate(35, (uint64_t)irq3);
    idt64_set_interrupt_gate(36, (uint64_t)irq4);
    idt64_set_interrupt_gate(37, (uint64_t)irq5);
    idt64_set_interrupt_gate(38, (uint64_t)irq6);
    idt64_set_interrupt_gate(39, (uint64_t)irq7);
    idt64_set_interrupt_gate(40, (uint64_t)irq8);
    idt64_set_interrupt_gate(41, (uint64_t)irq9);
    idt64_set_interrupt_gate(42, (uint64_t)irq10);
    idt64_set_interrupt_gate(43, (uint64_t)irq11);
    idt64_set_interrupt_gate(44, (uint64_t)irq12);
    idt64_set_interrupt_gate(45, (uint64_t)irq13);
    idt64_set_interrupt_gate(46, (uint64_t)irq14);
    idt64_set_interrupt_gate(47, (uint64_t)irq15);

    /* Register timer handler (IRQ 0) */
    irq64_register_handler(0, timer_handler);
    LOG_DEBUG_MSG("  Timer handler registered (IRQ 0)\n");

    /* Enable interrupts */
    __asm__ volatile("sti");

    LOG_INFO_MSG("x86_64 IRQ initialized successfully (16 hardware interrupts)\n");
    LOG_DEBUG_MSG("  Interrupts enabled\n");
}

/**
 * @brief Get IRQ count
 */
uint64_t irq64_get_count(uint8_t irq) {
    if (irq < 16) {
        return irq_counts[irq];
    }
    return 0;
}

/**
 * @brief Get timer tick count
 */
uint64_t irq64_get_timer_ticks(void) {
    return timer_ticks;
}
