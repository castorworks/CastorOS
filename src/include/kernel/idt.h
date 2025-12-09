// ============================================================================
// idt.h - Interrupt Descriptor Table
// ============================================================================
// This is a wrapper header that includes the architecture-specific IDT header

#ifndef _KERNEL_IDT_H_
#define _KERNEL_IDT_H_

#if defined(ARCH_I686)
    #include <idt.h>  // src/arch/i686/include/idt.h
#elif defined(ARCH_X86_64)
    #include <idt64.h>  // src/arch/x86_64/include/idt64.h
#elif defined(ARCH_ARM64)
    // ARM64 does not use IDT - provide stub or empty definitions
    #include <types.h>
    static inline void idt_init(void) {}
    static inline void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
        (void)num; (void)base; (void)selector; (void)flags;
    }
#else
    #error "Unknown architecture - cannot include IDT header"
#endif

#endif // _KERNEL_IDT_H_
