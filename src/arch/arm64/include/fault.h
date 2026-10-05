/**
 * @file fault.h
 * @brief ARM64 Page Fault Handling Definitions
 * 
 * Defines structures and functions for ARM64 page fault handling.
 * 
 * Requirements: 5.4, mm-refactor 6.4
 * 
 * **Feature: multi-arch-support, Property 5: VMM Page Fault Interpretation (ARM64)**
 * **Validates: Requirements 5.4**
 */

#ifndef _ARCH_ARM64_FAULT_H_
#define _ARCH_ARM64_FAULT_H_

#include <types.h>
#include <hal/hal.h>

/* ============================================================================
 * ESR_EL1 Field Definitions (for external use)
 * ========================================================================== */

/** Exception Class (EC) field - bits [31:26] */
#define ARM64_ESR_EC_SHIFT          26
#define ARM64_ESR_EC_MASK           (0x3FULL << ARM64_ESR_EC_SHIFT)

/** Exception Class values for page faults */
#define ARM64_EC_IABT_LOW           0x20    /**< Instruction Abort from lower EL (EL0) */
#define ARM64_EC_IABT_CUR           0x21    /**< Instruction Abort from current EL (EL1) */
#define ARM64_EC_DABT_LOW           0x24    /**< Data Abort from lower EL (EL0) */
#define ARM64_EC_DABT_CUR           0x25    /**< Data Abort from current EL (EL1) */

/** Instruction Specific Syndrome (ISS) field - bits [24:0] */
#define ARM64_ESR_ISS_MASK          0x01FFFFFFULL

/** Fault Status Code (FSC) - bits [5:0] of ISS */
#define ARM64_ISS_FSC_MASK          0x3F

/** Write not Read (WnR) bit - bit 6 of ISS */
#define ARM64_ISS_WNR               (1ULL << 6)

/** FAR not Valid (FnV) bit - bit 10 of ISS */
#define ARM64_ISS_FNV               (1ULL << 10)

/* ============================================================================
 * Fault Status Code (FSC) Definitions
 * ========================================================================== */

/** Translation Faults (page not present) */
#define ARM64_FSC_TRANS_L0          0x04    /**< Translation fault, level 0 */
#define ARM64_FSC_TRANS_L1          0x05    /**< Translation fault, level 1 */
#define ARM64_FSC_TRANS_L2          0x06    /**< Translation fault, level 2 */
#define ARM64_FSC_TRANS_L3          0x07    /**< Translation fault, level 3 */

/** Access Flag Faults */
#define ARM64_FSC_ACCESS_L1         0x09    /**< Access flag fault, level 1 */
#define ARM64_FSC_ACCESS_L2         0x0A    /**< Access flag fault, level 2 */
#define ARM64_FSC_ACCESS_L3         0x0B    /**< Access flag fault, level 3 */

/** Permission Faults (page present but permission denied) */
#define ARM64_FSC_PERM_L1           0x0D    /**< Permission fault, level 1 */
#define ARM64_FSC_PERM_L2           0x0E    /**< Permission fault, level 2 */
#define ARM64_FSC_PERM_L3           0x0F    /**< Permission fault, level 3 */

/** Other Faults */
#define ARM64_FSC_ALIGNMENT         0x21    /**< Alignment fault */

#endif /* _ARCH_ARM64_FAULT_H_ */
