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
 * @brief Check if FSC indicates a translation fault (page not present)
 * @param fsc Fault Status Code
 * @return true if translation fault
 * 
 * Translation faults occur when the page table walk fails to find
 * a valid mapping for the virtual address.
 */
bool arm64_is_translation_fault(uint32_t fsc) {
    return (fsc >= ARM64_FSC_TRANS_L0 && fsc <= ARM64_FSC_TRANS_L3);
}

/**
 * @brief Check if FSC indicates a permission fault (page present but access denied)
 * @param fsc Fault Status Code
 * @return true if permission fault
 * 
 * Permission faults occur when the page exists but the access type
 * (read/write/execute) is not permitted by the page table entry.
 */
bool arm64_is_permission_fault(uint32_t fsc) {
    return (fsc >= ARM64_FSC_PERM_L1 && fsc <= ARM64_FSC_PERM_L3);
}

/**
 * @brief Check if FSC indicates an access flag fault
 * @param fsc Fault Status Code
 * @return true if access flag fault
 * 
 * Access flag faults occur when the page exists but the Access Flag (AF)
 * bit is not set. This can be used for page aging/tracking.
 */
bool arm64_is_access_flag_fault(uint32_t fsc) {
    return (fsc >= ARM64_FSC_ACCESS_L1 && fsc <= ARM64_FSC_ACCESS_L3);
}

/**
 * @brief Check if FSC indicates an address size fault
 * @param fsc Fault Status Code
 * @return true if address size fault
 */
bool arm64_is_address_size_fault(uint32_t fsc) {
    /* ARM64_FSC_ADDR_SIZE_L0 is 0, so just check upper bound */
    return (fsc <= ARM64_FSC_ADDR_SIZE_L3);
}

/**
 * @brief Check if exception class is a data abort
 * @param ec Exception Class from ESR_EL1
 * @return true if data abort
 */
static inline bool is_data_abort(uint32_t ec) {
    return (ec == ARM64_EC_DABT_LOW || ec == ARM64_EC_DABT_CUR);
}

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

/* ============================================================================
 * HAL Page Fault Parsing Implementation
 * 
 * Note: hal_mmu_parse_fault() is implemented in mmu.c to keep all HAL MMU
 * functions together. This file provides additional helper functions for
 * page fault handling.
 * 
 * **Feature: multi-arch-support, Property 5: VMM Page Fault Interpretation (ARM64)**
 * **Validates: Requirements 5.4**
 * ========================================================================== */

/**
 * @brief Parse page fault information with provided ESR value
 * 
 * This variant is useful when the ESR value has already been saved
 * (e.g., by the exception handler) and we don't want to re-read it.
 * 
 * @param[out] info Pointer to structure to fill with fault information
 * @param esr ESR_EL1 value (already read by exception handler)
 * 
 * @see Requirements 5.4
 */
void arm64_parse_fault_with_esr(hal_page_fault_info_t *info, uint64_t esr) {
    if (info == NULL) {
        return;
    }
    
    /* Read fault address from FAR_EL1 */
    info->fault_addr = (vaddr_t)arm64_read_far_el1();
    info->raw_error = (uint32_t)esr;
    
    /* Extract fields */
    uint32_t ec = (esr & ARM64_ESR_EC_MASK) >> ARM64_ESR_EC_SHIFT;
    uint32_t iss = esr & ARM64_ESR_ISS_MASK;
    uint32_t fsc = iss & ARM64_ISS_FSC_MASK;
    
    bool data_abort = is_data_abort(ec);
    bool inst_abort = is_instruction_abort(ec);
    
    info->is_present = arm64_is_permission_fault(fsc) || 
                       arm64_is_access_flag_fault(fsc);
    info->is_write = data_abort && ((iss & ARM64_ISS_WNR) != 0);
    info->is_user = is_from_user_mode(ec);
    info->is_exec = inst_abort;
    info->is_reserved = false;
}

/**
 * @brief Check if a page fault is a COW (Copy-on-Write) fault
 * 
 * A COW fault occurs when:
 *   1. It's a data abort (not instruction fetch)
 *   2. It's a permission fault (page exists but write denied)
 *   3. The operation was a write
 * 
 * @param esr ESR_EL1 value
 * @return true if this is a COW fault
 */
bool arm64_is_cow_page_fault(uint64_t esr) {
    uint32_t ec = (esr & ARM64_ESR_EC_MASK) >> ARM64_ESR_EC_SHIFT;
    uint32_t iss = esr & ARM64_ESR_ISS_MASK;
    uint32_t fsc = iss & ARM64_ISS_FSC_MASK;
    
    /* Must be a data abort */
    if (!is_data_abort(ec)) {
        return false;
    }
    
    /* Must be a permission fault (page exists but write denied) */
    if (!arm64_is_permission_fault(fsc)) {
        return false;
    }
    
    /* Must be a write operation */
    return (iss & ARM64_ISS_WNR) != 0;
}

/**
 * @brief Get a human-readable description of the fault type
 * 
 * @param esr ESR_EL1 value
 * @return Description string
 */
const char* arm64_get_fault_description(uint64_t esr) {
    uint32_t ec = (esr & ARM64_ESR_EC_MASK) >> ARM64_ESR_EC_SHIFT;
    uint32_t iss = esr & ARM64_ESR_ISS_MASK;
    uint32_t fsc = iss & ARM64_ISS_FSC_MASK;
    bool is_write = (iss & ARM64_ISS_WNR) != 0;
    bool is_user = is_from_user_mode(ec);
    
    if (is_instruction_abort(ec)) {
        if (arm64_is_translation_fault(fsc)) {
            return is_user ? "User instruction fetch from unmapped page"
                           : "Kernel instruction fetch from unmapped page";
        } else if (arm64_is_permission_fault(fsc)) {
            return is_user ? "User instruction fetch permission denied"
                           : "Kernel instruction fetch permission denied";
        }
        return "Instruction abort";
    }
    
    if (is_data_abort(ec)) {
        if (arm64_is_translation_fault(fsc)) {
            if (is_write) {
                return is_user ? "User write to unmapped page"
                               : "Kernel write to unmapped page";
            } else {
                return is_user ? "User read from unmapped page"
                               : "Kernel read from unmapped page";
            }
        } else if (arm64_is_permission_fault(fsc)) {
            if (is_write) {
                return is_user ? "User write permission denied (possible COW)"
                               : "Kernel write permission denied";
            } else {
                return is_user ? "User read permission denied"
                               : "Kernel read permission denied";
            }
        } else if (arm64_is_access_flag_fault(fsc)) {
            return "Access flag fault";
        } else if (fsc == ARM64_FSC_ALIGNMENT) {
            return "Alignment fault";
        }
        return "Data abort";
    }
    
    return "Unknown fault";
}

/**
 * @brief Get the page table level where the fault occurred
 * 
 * @param fsc Fault Status Code
 * @return Page table level (0-3), or -1 if not applicable
 */
int arm64_get_fault_level(uint32_t fsc) {
    /* Translation faults */
    if (fsc >= ARM64_FSC_TRANS_L0 && fsc <= ARM64_FSC_TRANS_L3) {
        return fsc - ARM64_FSC_TRANS_L0;
    }
    
    /* Permission faults (levels 1-3 only) */
    if (fsc >= ARM64_FSC_PERM_L1 && fsc <= ARM64_FSC_PERM_L3) {
        return fsc - ARM64_FSC_PERM_L1 + 1;
    }
    
    /* Access flag faults (levels 1-3 only) */
    if (fsc >= ARM64_FSC_ACCESS_L1 && fsc <= ARM64_FSC_ACCESS_L3) {
        return fsc - ARM64_FSC_ACCESS_L1 + 1;
    }
    
    /* Address size faults (ARM64_FSC_ADDR_SIZE_L0 is 0) */
    if (fsc <= ARM64_FSC_ADDR_SIZE_L3) {
        return (int)fsc;  /* fsc - 0 = fsc */
    }
    
    return -1;  /* Not a level-specific fault */
}
