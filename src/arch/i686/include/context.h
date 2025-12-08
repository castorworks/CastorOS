/**
 * @file context.h
 * @brief i686 Architecture-Specific Context Structure
 * 
 * Defines the CPU context structure for i686 architecture, used for
 * task switching and interrupt handling.
 * 
 * Requirements: 7.1, 12.2
 */

#ifndef _ARCH_I686_CONTEXT_H_
#define _ARCH_I686_CONTEXT_H_

#include <types.h>

/* ============================================================================
 * i686 CPU Context Structure
 * ========================================================================== */

/**
 * @brief i686 CPU context structure
 * 
 * This structure holds all CPU registers that need to be saved/restored
 * during context switches. The layout matches the assembly code in
 * task_asm.asm for efficient save/restore operations.
 * 
 * Register offsets (for assembly reference):
 *   gs:       0
 *   fs:       4
 *   es:       8
 *   ds:      12
 *   edi:     16
 *   esi:     20
 *   ebp:     24
 *   esp_dummy: 28 (not used, placeholder for PUSHA compatibility)
 *   ebx:     32
 *   edx:     36
 *   ecx:     40
 *   eax:     44
 *   eip:     48
 *   cs:      52
 *   eflags:  56
 *   esp:     60
 *   ss:      64
 *   cr3:     68
 */
typedef struct i686_context {
    /* Segment registers (offset 0-15) */
    uint16_t gs, _gs_padding;    /* offset 0 */
    uint16_t fs, _fs_padding;    /* offset 4 */
    uint16_t es, _es_padding;    /* offset 8 */
    uint16_t ds, _ds_padding;    /* offset 12 */
    
    /* General purpose registers (offset 16-47) */
    uint32_t edi;                /* offset 16 */
    uint32_t esi;                /* offset 20 */
    uint32_t ebp;                /* offset 24 */
    uint32_t esp_dummy;          /* offset 28 - placeholder for PUSHA */
    uint32_t ebx;                /* offset 32 */
    uint32_t edx;                /* offset 36 */
    uint32_t ecx;                /* offset 40 */
    uint32_t eax;                /* offset 44 */
    
    /* Instruction pointer and code segment (offset 48-55) */
    uint32_t eip;                /* offset 48 */
    uint16_t cs, _cs_padding;    /* offset 52 */
    
    /* Flags register (offset 56-59) */
    uint32_t eflags;             /* offset 56 */
    
    /* Stack pointer and stack segment (offset 60-67) */
    uint32_t esp;                /* offset 60 */
    uint16_t ss, _ss_padding;    /* offset 64 */
    
    /* Page directory base register (offset 68-71) */
    uint32_t cr3;                /* offset 68 */
} __attribute__((packed)) i686_context_t;

/* ============================================================================
 * Context Structure Offsets (for assembly code)
 * ========================================================================== */

#define I686_CTX_GS         0
#define I686_CTX_FS         4
#define I686_CTX_ES         8
#define I686_CTX_DS         12
#define I686_CTX_EDI        16
#define I686_CTX_ESI        20
#define I686_CTX_EBP        24
#define I686_CTX_ESP_DUMMY  28
#define I686_CTX_EBX        32
#define I686_CTX_EDX        36
#define I686_CTX_ECX        40
#define I686_CTX_EAX        44
#define I686_CTX_EIP        48
#define I686_CTX_CS         52
#define I686_CTX_EFLAGS     56
#define I686_CTX_ESP        60
#define I686_CTX_SS         64
#define I686_CTX_CR3        68

#define I686_CTX_SIZE       72

/* ============================================================================
 * Segment Selectors
 * ========================================================================== */

/** Kernel code segment selector */
#define I686_KERNEL_CS      0x08

/** Kernel data segment selector */
#define I686_KERNEL_DS      0x10

/** User code segment selector (with RPL=3) */
#define I686_USER_CS        0x1B

/** User data segment selector (with RPL=3) */
#define I686_USER_DS        0x23

/* ============================================================================
 * EFLAGS Bits
 * ========================================================================== */

/** Interrupt enable flag */
#define I686_EFLAGS_IF      (1 << 9)

/** Default EFLAGS value (interrupts enabled) */
#define I686_EFLAGS_DEFAULT 0x202

/* ============================================================================
 * HAL Context Type Alias
 * ========================================================================== */

/**
 * @brief HAL context type for i686
 * 
 * This typedef allows the HAL interface to use a generic hal_context_t
 * that maps to the architecture-specific i686_context_t.
 */
typedef i686_context_t hal_context;

#endif /* _ARCH_I686_CONTEXT_H_ */
