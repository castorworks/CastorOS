/**
 * @file context.h
 * @brief ARM64 Architecture-Specific Context Structure
 * 
 * Defines the CPU context structure for ARM64 architecture, used for
 * task switching and interrupt handling.
 * 
 * Requirements: 7.2, 12.2
 */

#ifndef _ARCH_ARM64_CONTEXT_H_
#define _ARCH_ARM64_CONTEXT_H_

#include <types.h>

/* ============================================================================
 * ARM64 CPU Context Structure
 * ========================================================================== */

/**
 * @brief ARM64 CPU context structure
 * 
 * This structure holds all CPU registers that need to be saved/restored
 * during context switches. The layout matches the assembly code in
 * context.S for efficient save/restore operations.
 * 
 * Register offsets (for assembly reference):
 *   x0-x30:   0-240   (31 registers × 8 bytes)
 *   sp:      248
 *   pc:      256      (ELR_EL1 - Exception Link Register)
 *   pstate:  264      (SPSR_EL1 - Saved Program Status Register)
 *   ttbr0:   272      (TTBR0_EL1 - User page table base)
 * 
 * Total size: 280 bytes
 */
typedef struct arm64_context {
    /* General purpose registers X0-X30 (offset 0-247) */
    uint64_t x[31];              /* offset 0-247: X0-X30 */
    
    /* Stack pointer (offset 248-255) */
    uint64_t sp;                 /* offset 248 */
    
    /* Program counter - stored in ELR_EL1 (offset 256-263) */
    uint64_t pc;                 /* offset 256 */
    
    /* Processor state - stored in SPSR_EL1 (offset 264-271) */
    uint64_t pstate;             /* offset 264 */
    
    /* User page table base register (offset 272-279) */
    uint64_t ttbr0;              /* offset 272 */
} __attribute__((packed, aligned(16))) arm64_context_t;

/* ============================================================================
 * Context Structure Offsets (for assembly code)
 * ========================================================================== */

#define ARM64_CTX_X0        0
#define ARM64_CTX_X1        8
#define ARM64_CTX_X2        16
#define ARM64_CTX_X3        24
#define ARM64_CTX_X4        32
#define ARM64_CTX_X5        40
#define ARM64_CTX_X6        48
#define ARM64_CTX_X7        56
#define ARM64_CTX_X8        64
#define ARM64_CTX_X9        72
#define ARM64_CTX_X10       80
#define ARM64_CTX_X11       88
#define ARM64_CTX_X12       96
#define ARM64_CTX_X13       104
#define ARM64_CTX_X14       112
#define ARM64_CTX_X15       120
#define ARM64_CTX_X16       128
#define ARM64_CTX_X17       136
#define ARM64_CTX_X18       144
#define ARM64_CTX_X19       152
#define ARM64_CTX_X20       160
#define ARM64_CTX_X21       168
#define ARM64_CTX_X22       176
#define ARM64_CTX_X23       184
#define ARM64_CTX_X24       192
#define ARM64_CTX_X25       200
#define ARM64_CTX_X26       208
#define ARM64_CTX_X27       216
#define ARM64_CTX_X28       224
#define ARM64_CTX_X29       232      /* Frame pointer (FP) */
#define ARM64_CTX_X30       240      /* Link register (LR) */
#define ARM64_CTX_SP        248
#define ARM64_CTX_PC        256
#define ARM64_CTX_PSTATE    264
#define ARM64_CTX_TTBR0     272

#define ARM64_CTX_SIZE      280

/* ============================================================================
 * PSTATE/SPSR Bits
 * ========================================================================== */

/** Negative condition flag */
#define ARM64_PSTATE_N      (1ULL << 31)

/** Zero condition flag */
#define ARM64_PSTATE_Z      (1ULL << 30)

/** Carry condition flag */
#define ARM64_PSTATE_C      (1ULL << 29)

/** Overflow condition flag */
#define ARM64_PSTATE_V      (1ULL << 28)

/** Debug mask */
#define ARM64_PSTATE_D      (1ULL << 9)

/** SError mask */
#define ARM64_PSTATE_A      (1ULL << 8)

/** IRQ mask */
#define ARM64_PSTATE_I      (1ULL << 7)

/** FIQ mask */
#define ARM64_PSTATE_F      (1ULL << 6)

/** Exception level and SP selection */
#define ARM64_PSTATE_EL0t   0x00    /* EL0 with SP_EL0 */
#define ARM64_PSTATE_EL1t   0x04    /* EL1 with SP_EL0 */
#define ARM64_PSTATE_EL1h   0x05    /* EL1 with SP_EL1 */

/** Default PSTATE for user mode (EL0, interrupts enabled) */
#define ARM64_PSTATE_USER_DEFAULT   ARM64_PSTATE_EL0t

/** Default PSTATE for kernel mode (EL1h, interrupts enabled) */
#define ARM64_PSTATE_KERNEL_DEFAULT ARM64_PSTATE_EL1h

/* ============================================================================
 * HAL Context Type Alias
 * ========================================================================== */

/**
 * @brief HAL context type for ARM64
 * 
 * This typedef allows the HAL interface to use a generic hal_context_t
 * that maps to the architecture-specific arm64_context_t.
 */
typedef arm64_context_t hal_context;

#endif /* _ARCH_ARM64_CONTEXT_H_ */
