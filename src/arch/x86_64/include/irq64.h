/**
 * @file irq64.h
 * @brief Hardware Interrupt Requests (x86_64)
 * 
 * This header defines the IRQ handling for x86_64 architecture.
 * Supports both legacy PIC and APIC interrupt controllers.
 * 
 * Requirements: 6.3 - Configure PIC/APIC on x86
 */

#ifndef _ARCH_X86_64_IRQ64_H_
#define _ARCH_X86_64_IRQ64_H_

#include <types.h>
#include "isr64.h"

/* ============================================================================
 * IRQ Numbers (remapped to vectors 32-47)
 * ========================================================================== */

#define IRQ0  32    /* Timer */
#define IRQ1  33    /* Keyboard */
#define IRQ2  34    /* Cascade (slave PIC) */
#define IRQ3  35    /* COM2/COM4 */
#define IRQ4  36    /* COM1/COM3 */
#define IRQ5  37    /* LPT2 */
#define IRQ6  38    /* Floppy */
#define IRQ7  39    /* LPT1 / Spurious */
#define IRQ8  40    /* RTC */
#define IRQ9  41    /* Free */
#define IRQ10 42    /* Free */
#define IRQ11 43    /* Free */
#define IRQ12 44    /* PS/2 Mouse */
#define IRQ13 45    /* FPU */
#define IRQ14 46    /* Primary ATA */
#define IRQ15 47    /* Secondary ATA */

/* ============================================================================
 * API Functions
 * ========================================================================== */

/**
 * @brief Initialize IRQ subsystem
 * 
 * Remaps PIC and registers all IRQ handlers in IDT.
 */
void irq64_init(void);

/**
 * @brief Register an IRQ handler
 * @param irq IRQ number (0-15)
 * @param handler Handler function
 */
void irq64_register_handler(uint8_t irq, isr_handler_t handler);

/**
 * @brief Disable (mask) an IRQ line
 * @param irq IRQ number (0-15)
 */
void irq64_disable_line(uint8_t irq);

/**
 * @brief Enable (unmask) an IRQ line
 * @param irq IRQ number (0-15)
 */
void irq64_enable_line(uint8_t irq);

/**
 * @brief Get IRQ count
 * @param irq IRQ number (0-15)
 * @return Number of times this IRQ has fired
 */
uint64_t irq64_get_count(uint8_t irq);

/**
 * @brief Get timer tick count
 * @return Number of timer ticks since boot
 */
uint64_t irq64_get_timer_ticks(void);

/* Compatibility wrappers */
#define irq_init() irq64_init()
#define irq_register_handler(irq, h) irq64_register_handler(irq, h)
#define irq_disable_line(irq) irq64_disable_line(irq)
#define irq_enable_line(irq) irq64_enable_line(irq)
#define irq_get_count(irq) irq64_get_count(irq)
#define irq_get_timer_ticks() irq64_get_timer_ticks()

/* ============================================================================
 * IRQ Entry Points (defined in irq64_asm.asm)
 * ========================================================================== */

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#endif /* _ARCH_X86_64_IRQ64_H_ */
