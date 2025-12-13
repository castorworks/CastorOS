/**
 * @file context64.h
 * @brief x86_64 Architecture-Specific Context Structure
 * 
 * Defines the CPU context structure for x86_64 architecture, used for
 * task switching and interrupt handling.
 * 
 * Requirements: 7.1, 12.2
 */

#ifndef _ARCH_X86_64_CONTEXT64_H_
#define _ARCH_X86_64_CONTEXT64_H_

#include <types.h>

/* ============================================================================
 * x86_64 CPU Context Structure
 * ========================================================================== */

/**
 * @brief x86_64 CPU context structure
 * 
 * This structure holds all CPU registers that need to be saved/restored
 * during context switches. The layout matches the assembly code in
 * context64.asm for efficient save/restore operations.
 * 
 * Register offsets (for assembly reference):
 *   r15:      0
 *   r14:      8
 *   r13:     16
 *   r12:     24
 *   r11:     32
 *   r10:     40
 *   r9:      48
 *   r8:      56
 *   rbp:     64
 *   rdi:     72
 *   rsi:     80
 *   rdx:     88
 *   rcx:     96
 *   rbx:    104
 *   rax:    112
 *   rip:    120
 *   cs:     128
 *   rflags: 136
 *   rsp:    144
 *   ss:     152
 *   cr3:    160
 */
typedef struct x86_64_context {
    /* General purpose registers (offset 0-119) */
    uint64_t r15;                /* offset 0 */
    uint64_t r14;                /* offset 8 */
    uint64_t r13;                /* offset 16 */
    uint64_t r12;                /* offset 24 */
    uint64_t r11;                /* offset 32 */
    uint64_t r10;                /* offset 40 */
    uint64_t r9;                 /* offset 48 */
    uint64_t r8;                 /* offset 56 */
    uint64_t rbp;                /* offset 64 */
    uint64_t rdi;                /* offset 72 */
    uint64_t rsi;                /* offset 80 */
    uint64_t rdx;                /* offset 88 */
    uint64_t rcx;                /* offset 96 */
    uint64_t rbx;                /* offset 104 */
    uint64_t rax;                /* offset 112 */
    
    /* Instruction pointer (offset 120-127) */
    uint64_t rip;                /* offset 120 */
    
    /* Code segment (offset 128-135) */
    uint64_t cs;                 /* offset 128 */
    
    /* Flags register (offset 136-143) */
    uint64_t rflags;             /* offset 136 */
    
    /* Stack pointer (offset 144-151) */
    uint64_t rsp;                /* offset 144 */
    
    /* Stack segment (offset 152-159) */
    uint64_t ss;                 /* offset 152 */
    
    /* Page table base register (offset 160-167) */
    uint64_t cr3;                /* offset 160 */
} __attribute__((packed)) x86_64_context_t;


/* ============================================================================
 * Context Structure Offsets (for assembly code)
 * ========================================================================== */

#define X86_64_CTX_R15      0
#define X86_64_CTX_R14      8
#define X86_64_CTX_R13      16
#define X86_64_CTX_R12      24
#define X86_64_CTX_R11      32
#define X86_64_CTX_R10      40
#define X86_64_CTX_R9       48
#define X86_64_CTX_R8       56
#define X86_64_CTX_RBP      64
#define X86_64_CTX_RDI      72
#define X86_64_CTX_RSI      80
#define X86_64_CTX_RDX      88
#define X86_64_CTX_RCX      96
#define X86_64_CTX_RBX      104
#define X86_64_CTX_RAX      112
#define X86_64_CTX_RIP      120
#define X86_64_CTX_CS       128
#define X86_64_CTX_RFLAGS   136
#define X86_64_CTX_RSP      144
#define X86_64_CTX_SS       152
#define X86_64_CTX_CR3      160

#define X86_64_CTX_SIZE     168

/* ============================================================================
 * Segment Selectors
 * ========================================================================== */

/** Kernel code segment selector */
#define X86_64_KERNEL_CS    0x08

/** Kernel data segment selector */
#define X86_64_KERNEL_DS    0x10

/** User code segment selector (with RPL=3) - GDT index 4 */
#define X86_64_USER_CS      0x23

/** User data segment selector (with RPL=3) - GDT index 3 */
#define X86_64_USER_DS      0x1B

/* ============================================================================
 * RFLAGS Bits
 * ========================================================================== */

/** Interrupt enable flag */
#define X86_64_RFLAGS_IF    (1ULL << 9)

/** Default RFLAGS value (interrupts enabled, reserved bit 1 set) */
#define X86_64_RFLAGS_DEFAULT 0x202ULL

/* ============================================================================
 * HAL Context Type Alias
 * ========================================================================== */

/**
 * @brief HAL context type for x86_64
 * 
 * This typedef allows the HAL interface to use a generic hal_context_t
 * that maps to the architecture-specific x86_64_context_t.
 */
typedef x86_64_context_t hal_context;

#endif /* _ARCH_X86_64_CONTEXT64_H_ */
