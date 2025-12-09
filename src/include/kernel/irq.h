// ============================================================================
// irq.h - Hardware Interrupt Requests
// ============================================================================
// This is a wrapper header that includes the architecture-specific IRQ header

#ifndef _KERNEL_IRQ_H_
#define _KERNEL_IRQ_H_

#include <kernel/isr.h>

#if defined(ARCH_I686)
    #include <irq.h>  // src/arch/i686/include/irq.h
#elif defined(ARCH_X86_64)
    #include <irq64.h>  // src/arch/x86_64/include/irq64.h
#elif defined(ARCH_ARM64)
    // ARM64 uses GIC instead of PIC - provide stub definitions
    #include <types.h>
    
    // Generic IRQ numbers for ARM64
    #define IRQ0  0
    #define IRQ1  1
    #define IRQ2  2
    #define IRQ3  3
    #define IRQ4  4
    #define IRQ5  5
    #define IRQ6  6
    #define IRQ7  7
    #define IRQ8  8
    #define IRQ9  9
    #define IRQ10 10
    #define IRQ11 11
    #define IRQ12 12
    #define IRQ13 13
    #define IRQ14 14
    #define IRQ15 15
    
    static inline void irq_init(void) {}
    static inline void irq_register_handler(uint8_t irq, isr_handler_t handler) {
        (void)irq; (void)handler;
    }
    static inline void irq_disable_line(uint8_t irq) { (void)irq; }
    static inline void irq_enable_line(uint8_t irq) { (void)irq; }
    static inline uint64_t irq_get_count(uint8_t irq) { (void)irq; return 0; }
    static inline uint64_t irq_get_timer_ticks(void) { return 0; }
#else
    #error "Unknown architecture - cannot include IRQ header"
#endif

#endif // _KERNEL_IRQ_H_
