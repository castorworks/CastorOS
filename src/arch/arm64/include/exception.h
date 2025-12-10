/**
 * @file exception.h
 * @brief ARM64 Exception Handling Definitions
 * 
 * Defines structures and constants for ARM64 exception handling.
 * 
 * Requirements: 4.5, 6.2
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */

#ifndef _ARCH_ARM64_EXCEPTION_H_
#define _ARCH_ARM64_EXCEPTION_H_

#include <types.h>

/* ============================================================================
 * Exception Types
 * ========================================================================== */

/** Exception type constants (matches vectors.S) */
#define EXCEPTION_SYNC      0   /**< Synchronous exception */
#define EXCEPTION_IRQ       1   /**< IRQ interrupt */
#define EXCEPTION_FIQ       2   /**< FIQ interrupt */
#define EXCEPTION_SERROR    3   /**< SError (System Error) */

/** Exception source constants (matches vectors.S) */
#define EXCEPTION_FROM_EL1_SP0  0   /**< Current EL with SP0 */
#define EXCEPTION_FROM_EL1_SPX  1   /**< Current EL with SPx */
#define EXCEPTION_FROM_EL0_64   2   /**< Lower EL using AArch64 */
#define EXCEPTION_FROM_EL0_32   3   /**< Lower EL using AArch32 */

/* ============================================================================
 * Exception Syndrome Register (ESR_EL1) Definitions
 * ========================================================================== */

/** ESR_EL1 Exception Class (EC) field - bits [31:26] */
#define ESR_EC_SHIFT        26
#define ESR_EC_MASK         (0x3F << ESR_EC_SHIFT)

/** Exception Class values */
#define ESR_EC_UNKNOWN      0x00    /**< Unknown reason */
#define ESR_EC_WFI_WFE      0x01    /**< WFI/WFE trapped */
#define ESR_EC_CP15_MCR     0x03    /**< MCR/MRC CP15 trapped */
#define ESR_EC_CP15_MCRR    0x04    /**< MCRR/MRRC CP15 trapped */
#define ESR_EC_CP14_MCR     0x05    /**< MCR/MRC CP14 trapped */
#define ESR_EC_CP14_LDC     0x06    /**< LDC/STC CP14 trapped */
#define ESR_EC_FP_ASIMD     0x07    /**< FP/ASIMD access trapped */
#define ESR_EC_CP10_MCR     0x08    /**< MCR/MRC CP10 trapped */
#define ESR_EC_PAC          0x09    /**< PAC trapped */
#define ESR_EC_CP14_MRRC    0x0C    /**< MRRC CP14 trapped */
#define ESR_EC_BTI          0x0D    /**< BTI exception */
#define ESR_EC_ILLEGAL      0x0E    /**< Illegal execution state */
#define ESR_EC_SVC32        0x11    /**< SVC from AArch32 */
#define ESR_EC_HVC32        0x12    /**< HVC from AArch32 */
#define ESR_EC_SMC32        0x13    /**< SMC from AArch32 */
#define ESR_EC_SVC64        0x15    /**< SVC from AArch64 */
#define ESR_EC_HVC64        0x16    /**< HVC from AArch64 */
#define ESR_EC_SMC64        0x17    /**< SMC from AArch64 */
#define ESR_EC_SYS64        0x18    /**< MSR/MRS/SYS trapped */
#define ESR_EC_SVE          0x19    /**< SVE access trapped */
#define ESR_EC_ERET         0x1A    /**< ERET trapped */
#define ESR_EC_FPAC         0x1C    /**< FPAC exception */
#define ESR_EC_SME          0x1D    /**< SME access trapped */
#define ESR_EC_IABT_LOW     0x20    /**< Instruction abort from lower EL */
#define ESR_EC_IABT_CUR     0x21    /**< Instruction abort from current EL */
#define ESR_EC_PC_ALIGN     0x22    /**< PC alignment fault */
#define ESR_EC_DABT_LOW     0x24    /**< Data abort from lower EL */
#define ESR_EC_DABT_CUR     0x25    /**< Data abort from current EL */
#define ESR_EC_SP_ALIGN     0x26    /**< SP alignment fault */
#define ESR_EC_FP32         0x28    /**< FP exception from AArch32 */
#define ESR_EC_FP64         0x2C    /**< FP exception from AArch64 */
#define ESR_EC_SERROR       0x2F    /**< SError interrupt */
#define ESR_EC_BKPT_LOW     0x30    /**< Breakpoint from lower EL */
#define ESR_EC_BKPT_CUR     0x31    /**< Breakpoint from current EL */
#define ESR_EC_STEP_LOW     0x32    /**< Software step from lower EL */
#define ESR_EC_STEP_CUR     0x33    /**< Software step from current EL */
#define ESR_EC_WATCH_LOW    0x34    /**< Watchpoint from lower EL */
#define ESR_EC_WATCH_CUR    0x35    /**< Watchpoint from current EL */
#define ESR_EC_BKPT32       0x38    /**< BKPT from AArch32 */
#define ESR_EC_BRK64        0x3C    /**< BRK from AArch64 */

/** ESR_EL1 Instruction Length (IL) field - bit 25 */
#define ESR_IL_SHIFT        25
#define ESR_IL_MASK         (1 << ESR_IL_SHIFT)

/** ESR_EL1 Instruction Specific Syndrome (ISS) field - bits [24:0] */
#define ESR_ISS_MASK        0x01FFFFFF

/** Data Abort ISS fields */
#define ESR_ISS_DFSC_MASK   0x3F    /**< Data Fault Status Code */
#define ESR_ISS_WNR         (1 << 6)    /**< Write not Read */
#define ESR_ISS_CM          (1 << 8)    /**< Cache maintenance */
#define ESR_ISS_EA          (1 << 9)    /**< External abort */
#define ESR_ISS_FNV         (1 << 10)   /**< FAR not valid */
#define ESR_ISS_SET_MASK    (3 << 11)   /**< Synchronous Error Type */
#define ESR_ISS_VNCR        (1 << 13)   /**< VNCR */
#define ESR_ISS_AR          (1 << 14)   /**< Acquire/Release */
#define ESR_ISS_SF          (1 << 15)   /**< Sixty-Four bit register */
#define ESR_ISS_SRT_MASK    (0x1F << 16) /**< Syndrome Register Transfer */
#define ESR_ISS_SSE         (1 << 21)   /**< Syndrome Sign Extend */
#define ESR_ISS_SAS_MASK    (3 << 22)   /**< Syndrome Access Size */
#define ESR_ISS_ISV         (1 << 24)   /**< Instruction Syndrome Valid */

/** Fault Status Codes (DFSC/IFSC) */
#define FSC_ADDR_L0         0x00    /**< Address size fault, level 0 */
#define FSC_ADDR_L1         0x01    /**< Address size fault, level 1 */
#define FSC_ADDR_L2         0x02    /**< Address size fault, level 2 */
#define FSC_ADDR_L3         0x03    /**< Address size fault, level 3 */
#define FSC_TRANS_L0        0x04    /**< Translation fault, level 0 */
#define FSC_TRANS_L1        0x05    /**< Translation fault, level 1 */
#define FSC_TRANS_L2        0x06    /**< Translation fault, level 2 */
#define FSC_TRANS_L3        0x07    /**< Translation fault, level 3 */
#define FSC_ACCESS_L1       0x09    /**< Access flag fault, level 1 */
#define FSC_ACCESS_L2       0x0A    /**< Access flag fault, level 2 */
#define FSC_ACCESS_L3       0x0B    /**< Access flag fault, level 3 */
#define FSC_PERM_L1         0x0D    /**< Permission fault, level 1 */
#define FSC_PERM_L2         0x0E    /**< Permission fault, level 2 */
#define FSC_PERM_L3         0x0F    /**< Permission fault, level 3 */
#define FSC_SYNC_EXT        0x10    /**< Synchronous external abort */
#define FSC_SYNC_TAG        0x11    /**< Synchronous tag check fault */
#define FSC_SYNC_EXT_L0     0x14    /**< Sync external abort, level 0 */
#define FSC_SYNC_EXT_L1     0x15    /**< Sync external abort, level 1 */
#define FSC_SYNC_EXT_L2     0x16    /**< Sync external abort, level 2 */
#define FSC_SYNC_EXT_L3     0x17    /**< Sync external abort, level 3 */
#define FSC_SYNC_PARITY     0x18    /**< Synchronous parity error */
#define FSC_SYNC_PARITY_L0  0x1C    /**< Sync parity error, level 0 */
#define FSC_SYNC_PARITY_L1  0x1D    /**< Sync parity error, level 1 */
#define FSC_SYNC_PARITY_L2  0x1E    /**< Sync parity error, level 2 */
#define FSC_SYNC_PARITY_L3  0x1F    /**< Sync parity error, level 3 */
#define FSC_ALIGN           0x21    /**< Alignment fault */
#define FSC_TLB_CONFLICT    0x30    /**< TLB conflict abort */
#define FSC_ATOMIC          0x31    /**< Unsupported atomic hardware update */
#define FSC_IMPL_DEF        0x34    /**< Implementation defined */

/* ============================================================================
 * Register Frame Structure
 * ========================================================================== */

/**
 * @brief ARM64 exception register frame
 * 
 * This structure matches the stack frame created by the kernel_entry macro
 * in vectors.S. It contains all registers saved during exception entry.
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
typedef struct arm64_regs {
    uint64_t x[31];     /**< General-purpose registers X0-X30 */
    uint64_t sp_el0;    /**< User stack pointer (SP_EL0) */
    uint64_t elr;       /**< Exception Link Register (return address) */
    uint64_t spsr;      /**< Saved Program Status Register */
} __attribute__((packed)) arm64_regs_t;

/* Verify structure size matches assembly frame size */
_Static_assert(sizeof(arm64_regs_t) == 272, "arm64_regs_t size mismatch with FRAME_SIZE");

/* ============================================================================
 * Function Declarations
 * ========================================================================== */

/**
 * @brief Install exception vector table
 * 
 * Sets VBAR_EL1 to point to the exception vector table.
 * Defined in vectors.S.
 */
void arm64_install_vectors(void);

/**
 * @brief Get ESR_EL1 value
 * @return Exception Syndrome Register value
 */
uint64_t arm64_get_esr(void);

/**
 * @brief Get FAR_EL1 value
 * @return Fault Address Register value
 */
uint64_t arm64_get_far(void);

/**
 * @brief Get ELR_EL1 value
 * @return Exception Link Register value
 */
uint64_t arm64_get_elr(void);

/**
 * @brief Get SPSR_EL1 value
 * @return Saved Program Status Register value
 */
uint64_t arm64_get_spsr(void);

/**
 * @brief Main exception handler (called from vectors.S)
 * 
 * @param regs Pointer to saved register frame
 * @param type Exception type (EXCEPTION_SYNC, EXCEPTION_IRQ, etc.)
 * @param source Exception source (EXCEPTION_FROM_EL1_SPX, etc.)
 */
void arm64_exception_handler(arm64_regs_t *regs, uint32_t type, uint32_t source);

/**
 * @brief Initialize ARM64 exception handling
 * 
 * Installs the exception vector table and sets up exception handlers.
 */
void arm64_exception_init(void);

/**
 * @brief Get exception class name string
 * @param ec Exception class value
 * @return Human-readable exception class name
 */
const char *arm64_exception_class_name(uint32_t ec);

/**
 * @brief Get fault status code name string
 * @param fsc Fault status code value
 * @return Human-readable fault status code name
 */
const char *arm64_fault_status_name(uint32_t fsc);

#endif /* _ARCH_ARM64_EXCEPTION_H_ */
