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

/* ============================================================================
 * Function Declarations
 * ========================================================================== */

/**
 * @brief Check if FSC indicates a translation fault (page not present)
 * @param fsc Fault Status Code
 * @return true if translation fault
 */
bool arm64_is_translation_fault(uint32_t fsc);

/**
 * @brief Check if FSC indicates a permission fault (page present but access denied)
 * @param fsc Fault Status Code
 * @return true if permission fault
 */
bool arm64_is_permission_fault(uint32_t fsc);

/**
 * @brief Check if FSC indicates an access flag fault
 * @param fsc Fault Status Code
 * @return true if access flag fault
 */
bool arm64_is_access_flag_fault(uint32_t fsc);

/**
 * @brief Check if FSC indicates an address size fault
 * @param fsc Fault Status Code
 * @return true if address size fault
 */
bool arm64_is_address_size_fault(uint32_t fsc);

/**
 * @brief Parse page fault information with provided ESR value
 * 
 * @param[out] info Pointer to structure to fill with fault information
 * @param esr ESR_EL1 value (already read by exception handler)
 */
void arm64_parse_fault_with_esr(hal_page_fault_info_t *info, uint64_t esr);

/**
 * @brief Check if a page fault is a COW (Copy-on-Write) fault
 * 
 * @param esr ESR_EL1 value
 * @return true if this is a COW fault
 */
bool arm64_is_cow_page_fault(uint64_t esr);

/**
 * @brief Get a human-readable description of the fault type
 * 
 * @param esr ESR_EL1 value
 * @return Description string
 */
const char* arm64_get_fault_description(uint64_t esr);

/**
 * @brief Get the page table level where the fault occurred
 * 
 * @param fsc Fault Status Code
 * @return Page table level (0-3), or -1 if not applicable
 */
int arm64_get_fault_level(uint32_t fsc);

#endif /* _ARCH_ARM64_FAULT_H_ */
