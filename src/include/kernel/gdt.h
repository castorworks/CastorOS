// ============================================================================
// gdt.h - Global Descriptor Table & Task State Segment
// ============================================================================
// This is a wrapper header that includes the architecture-specific GDT header

#ifndef _KERNEL_GDT_H_
#define _KERNEL_GDT_H_

#if defined(ARCH_I686)
    #include <gdt.h>  // src/arch/i686/include/gdt.h
#elif defined(ARCH_X86_64)
    #include <gdt64.h>  // src/arch/x86_64/include/gdt64.h
#elif defined(ARCH_ARM64)
    // ARM64 does not use GDT - provide stub or empty definitions
    #include <types.h>
    // Stub definitions for ARM64 compatibility
    #define GDT_KERNEL_CODE_SEGMENT  0
    #define GDT_KERNEL_DATA_SEGMENT  0
    #define GDT_USER_CODE_SEGMENT    0
    #define GDT_USER_DATA_SEGMENT    0
    static inline void gdt_init_all_with_tss(uint32_t kernel_stack, uint16_t kernel_ss) {
        (void)kernel_stack; (void)kernel_ss;
    }
    static inline void tss_set_kernel_stack(uint32_t kernel_stack) {
        (void)kernel_stack;
    }
#else
    #error "Unknown architecture - cannot include GDT header"
#endif

#endif // _KERNEL_GDT_H_
