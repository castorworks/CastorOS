// ============================================================================
// isr.h - Interrupt Service Routines
// ============================================================================
// This is a wrapper header that includes the architecture-specific ISR header

#ifndef _KERNEL_ISR_H_
#define _KERNEL_ISR_H_

#if defined(ARCH_I686)
    #include <isr.h>  // src/arch/i686/include/isr.h
#elif defined(ARCH_X86_64)
    #include <isr64.h>  // src/arch/x86_64/include/isr64.h
#elif defined(ARCH_ARM64)
    // ARM64 uses different exception handling - provide stub definitions
    #include <types.h>
    
    // Minimal registers_t for ARM64 compatibility
    typedef struct {
        uint64_t x[31];     // X0-X30
        uint64_t sp;
        uint64_t pc;
        uint64_t pstate;
        uint64_t int_no;
        uint64_t err_code;
    } registers_t;
    
    typedef void (*isr_handler_t)(registers_t *regs);
    
    static inline void isr_init(void) {}
    static inline void isr_register_handler(uint8_t n, isr_handler_t handler) {
        (void)n; (void)handler;
    }
#else
    #error "Unknown architecture - cannot include ISR header"
#endif

#endif // _KERNEL_ISR_H_
