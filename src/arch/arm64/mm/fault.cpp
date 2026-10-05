/**
 * @file fault.c
 * @brief ARM64 Page Fault Handling Implementation
 * 
 * Implements page fault parsing and handling for ARM64 architecture.
 * Parses ESR_EL1 (Exception Syndrome Register) and FAR_EL1 (Fault Address Register)
 * to fill hal_page_fault_info_t structure.
 * 
 * Requirements: 5.4, mm-refactor 6.4
 * 
 * **Feature: multi-arch-support, Property 5: VMM Page Fault Interpretation (ARM64)**
 * **Validates: Requirements 5.4**
 */

#include <types.h>
#include <hal/hal.h>
#include <mm/mm_types.h>
#include <lib/klog.h>

/* ============================================================================
 * ARM64 System Register Access
 * ========================================================================== */

/**
 * @brief Read FAR_EL1 (Fault Address Register)
 * @return Fault address value
 */
static inline uint64_t arm64_read_far_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, far_el1" : "=r"(val));
    return val;
}

/**
 * @brief Read ESR_EL1 (Exception Syndrome Register)
 * @return Exception syndrome value
 */
static inline uint64_t arm64_read_esr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(val));
    return val;
}

/* ============================================================================
 * ESR_EL1 Field Definitions
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

/** Data/Instruction Fault Status Code (DFSC/IFSC) - bits [5:0] of ISS */
#define ARM64_ISS_FSC_MASK          0x3F

/** Write not Read (WnR) bit - bit 6 of ISS for data aborts */
#define ARM64_ISS_WNR               (1ULL << 6)

/** FAR not Valid (FnV) bit - bit 10 of ISS */
#define ARM64_ISS_FNV               (1ULL << 10)

/* ============================================================================
 * Fault Status Code (FSC) Definitions
 * 
 * FSC values indicate the type of fault that occurred.
 * ========================================================================== */

/** Address Size Faults (page not present at translation level) */
#define ARM64_FSC_ADDR_SIZE_L0      0x00    /**< Address size fault, level 0 */
#define ARM64_FSC_ADDR_SIZE_L1      0x01    /**< Address size fault, level 1 */
#define ARM64_FSC_ADDR_SIZE_L2      0x02    /**< Address size fault, level 2 */
#define ARM64_FSC_ADDR_SIZE_L3      0x03    /**< Address size fault, level 3 */

/** Translation Faults (page not present) */
#define ARM64_FSC_TRANS_L0          0x04    /**< Translation fault, level 0 */
#define ARM64_FSC_TRANS_L1          0x05    /**< Translation fault, level 1 */
#define ARM64_FSC_TRANS_L2          0x06    /**< Translation fault, level 2 */
#define ARM64_FSC_TRANS_L3          0x07    /**< Translation fault, level 3 */

/** Access Flag Faults (page present but access flag not set) */
#define ARM64_FSC_ACCESS_L1         0x09    /**< Access flag fault, level 1 */
#define ARM64_FSC_ACCESS_L2         0x0A    /**< Access flag fault, level 2 */
#define ARM64_FSC_ACCESS_L3         0x0B    /**< Access flag fault, level 3 */

/** Permission Faults (page present but permission denied) */
#define ARM64_FSC_PERM_L1           0x0D    /**< Permission fault, level 1 */
#define ARM64_FSC_PERM_L2           0x0E    /**< Permission fault, level 2 */
#define ARM64_FSC_PERM_L3           0x0F    /**< Permission fault, level 3 */

/** Synchronous External Aborts */
#define ARM64_FSC_SYNC_EXT          0x10    /**< Synchronous external abort */
#define ARM64_FSC_SYNC_EXT_L0       0x14    /**< Sync external abort, level 0 */
#define ARM64_FSC_SYNC_EXT_L1       0x15    /**< Sync external abort, level 1 */
#define ARM64_FSC_SYNC_EXT_L2       0x16    /**< Sync external abort, level 2 */
#define ARM64_FSC_SYNC_EXT_L3       0x17    /**< Sync external abort, level 3 */

/** Other Faults */
#define ARM64_FSC_ALIGNMENT         0x21    /**< Alignment fault */
#define ARM64_FSC_TLB_CONFLICT      0x30    /**< TLB conflict abort */

/* ============================================================================
 * Fault Classification Helper Functions
 * ========================================================================== */

/**
 * @brief Check if exception class is an instruction abort
 * @param ec Exception Class from ESR_EL1
 * @return true if instruction abort
 */
static inline bool is_instruction_abort(uint32_t ec) {
    return (ec == ARM64_EC_IABT_LOW || ec == ARM64_EC_IABT_CUR);
}

/**
 * @brief Check if exception originated from user mode (EL0)
 * @param ec Exception Class from ESR_EL1
 * @return true if from user mode
 */
static inline bool is_from_user_mode(uint32_t ec) {
    return (ec == ARM64_EC_DABT_LOW || ec == ARM64_EC_IABT_LOW);
}

