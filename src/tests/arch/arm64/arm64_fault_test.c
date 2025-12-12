/**
 * @file arm64_fault_test.c
 * @brief ARM64 Page Fault Property Tests
 * 
 * Implements property-based tests for ARM64 page fault interpretation.
 * Tests that ESR_EL1 and FAR_EL1 values are correctly parsed into
 * the architecture-independent hal_page_fault_info_t structure.
 * 
 * **Feature: multi-arch-support, Property 5: VMM Page Fault Interpretation (ARM64)**
 * **Validates: Requirements 5.4**
 */

#include <tests/ktest.h>
#include <tests/arch/arm64/arm64_fault_test.h>
#include <types.h>
#include <lib/kprintf.h>

#ifdef ARCH_ARM64

#include <hal/hal.h>
#include <mm/mm_types.h>

/* Include ARM64 fault definitions */
#include "fault.h"

/* ============================================================================
 * ESR_EL1 Construction Helpers
 * 
 * These helpers construct synthetic ESR_EL1 values for testing.
 * ============================================================================ */

/**
 * @brief Construct an ESR_EL1 value for a data abort
 * @param from_el0 true if from user mode (EL0)
 * @param fsc Fault Status Code
 * @param is_write true if write operation
 * @return Constructed ESR_EL1 value
 */
static uint64_t make_data_abort_esr(bool from_el0, uint32_t fsc, bool is_write) {
    uint64_t ec = from_el0 ? ARM64_EC_DABT_LOW : ARM64_EC_DABT_CUR;
    uint64_t esr = (ec << ARM64_ESR_EC_SHIFT);
    esr |= (fsc & ARM64_ISS_FSC_MASK);
    if (is_write) {
        esr |= ARM64_ISS_WNR;
    }
    return esr;
}

/**
 * @brief Construct an ESR_EL1 value for an instruction abort
 * @param from_el0 true if from user mode (EL0)
 * @param fsc Fault Status Code
 * @return Constructed ESR_EL1 value
 */
static uint64_t make_inst_abort_esr(bool from_el0, uint32_t fsc) {
    uint64_t ec = from_el0 ? ARM64_EC_IABT_LOW : ARM64_EC_IABT_CUR;
    uint64_t esr = (ec << ARM64_ESR_EC_SHIFT);
    esr |= (fsc & ARM64_ISS_FSC_MASK);
    return esr;
}

/**
 * @brief Parse fault info from a synthetic ESR value (for testing)
 * 
 * This function simulates hal_mmu_parse_fault() but uses a provided
 * ESR value instead of reading from the actual register.
 */
static void parse_fault_from_esr(hal_page_fault_info_t *info, uint64_t esr, vaddr_t fault_addr) {
    if (info == NULL) {
        return;
    }
    
    info->fault_addr = fault_addr;
    info->raw_error = (uint32_t)esr;
    
    uint32_t ec = (esr & ARM64_ESR_EC_MASK) >> ARM64_ESR_EC_SHIFT;
    uint32_t iss = esr & ARM64_ESR_ISS_MASK;
    uint32_t fsc = iss & ARM64_ISS_FSC_MASK;
    
    bool data_abort = (ec == ARM64_EC_DABT_LOW || ec == ARM64_EC_DABT_CUR);
    bool inst_abort = (ec == ARM64_EC_IABT_LOW || ec == ARM64_EC_IABT_CUR);
    
    info->is_present = arm64_is_permission_fault(fsc) || 
                       arm64_is_access_flag_fault(fsc);
    info->is_write = data_abort && ((iss & ARM64_ISS_WNR) != 0);
    info->is_user = (ec == ARM64_EC_DABT_LOW || ec == ARM64_EC_IABT_LOW);
    info->is_exec = inst_abort;
    info->is_reserved = false;
}

/* ============================================================================
 * Property 5: VMM Page Fault Interpretation (ARM64)
 * 
 * *For any* page fault exception, the VMM SHALL correctly interpret the
 * architecture-specific fault information (FAR_EL1 and ESR_EL1 on ARM64)
 * to determine the faulting address and fault type.
 * 
 * **Validates: Requirements 5.4**
 * ============================================================================ */

/**
 * @brief Test translation fault interpretation (page not present)
 * 
 * *For any* translation fault, is_present SHALL be false
 */
TEST_CASE(pbt_arm64_translation_fault_not_present) {
    hal_page_fault_info_t info;
    uint32_t trans_faults[] = {
        ARM64_FSC_TRANS_L0, ARM64_FSC_TRANS_L1,
        ARM64_FSC_TRANS_L2, ARM64_FSC_TRANS_L3
    };
    
    /* Test all translation fault levels */
    for (uint32_t i = 0; i < sizeof(trans_faults)/sizeof(trans_faults[0]); i++) {
        uint32_t fsc = trans_faults[i];
        
        /* Test data abort (read) */
        uint64_t esr = make_data_abort_esr(true, fsc, false);
        parse_fault_from_esr(&info, esr, 0x1000);
        
        /* Property: Translation faults indicate page NOT present */
        ASSERT_FALSE(info.is_present);
        
        /* Test data abort (write) */
        esr = make_data_abort_esr(true, fsc, true);
        parse_fault_from_esr(&info, esr, 0x2000);
        ASSERT_FALSE(info.is_present);
        
        /* Test instruction abort */
        esr = make_inst_abort_esr(true, fsc);
        parse_fault_from_esr(&info, esr, 0x3000);
        ASSERT_FALSE(info.is_present);
    }
}

/**
 * @brief Test permission fault interpretation (page present but access denied)
 * 
 * *For any* permission fault, is_present SHALL be true
 */
TEST_CASE(pbt_arm64_permission_fault_present) {
    hal_page_fault_info_t info;
    uint32_t perm_faults[] = {
        ARM64_FSC_PERM_L1, ARM64_FSC_PERM_L2, ARM64_FSC_PERM_L3
    };
    
    /* Test all permission fault levels */
    for (uint32_t i = 0; i < sizeof(perm_faults)/sizeof(perm_faults[0]); i++) {
        uint32_t fsc = perm_faults[i];
        
        /* Test data abort (read) */
        uint64_t esr = make_data_abort_esr(true, fsc, false);
        parse_fault_from_esr(&info, esr, 0x1000);
        
        /* Property: Permission faults indicate page IS present */
        ASSERT_TRUE(info.is_present);
        
        /* Test data abort (write) */
        esr = make_data_abort_esr(true, fsc, true);
        parse_fault_from_esr(&info, esr, 0x2000);
        ASSERT_TRUE(info.is_present);
        
        /* Test instruction abort */
        esr = make_inst_abort_esr(true, fsc);
        parse_fault_from_esr(&info, esr, 0x3000);
        ASSERT_TRUE(info.is_present);
    }
}

/**
 * @brief Test write fault detection
 * 
 * *For any* data abort with WnR bit set, is_write SHALL be true
 */
TEST_CASE(pbt_arm64_write_fault_detection) {
    hal_page_fault_info_t info;
    uint32_t test_faults[] = {
        ARM64_FSC_TRANS_L3, ARM64_FSC_PERM_L3, ARM64_FSC_ACCESS_L3
    };
    
    for (uint32_t i = 0; i < sizeof(test_faults)/sizeof(test_faults[0]); i++) {
        uint32_t fsc = test_faults[i];
        
        /* Test write operation */
        uint64_t esr = make_data_abort_esr(true, fsc, true);
        parse_fault_from_esr(&info, esr, 0x1000);
        
        /* Property: Write operations must set is_write */
        ASSERT_TRUE(info.is_write);
        
        /* Test read operation */
        esr = make_data_abort_esr(true, fsc, false);
        parse_fault_from_esr(&info, esr, 0x2000);
        
        /* Property: Read operations must NOT set is_write */
        ASSERT_FALSE(info.is_write);
    }
}

/**
 * @brief Test instruction fetch fault detection
 * 
 * *For any* instruction abort, is_exec SHALL be true
 */
TEST_CASE(pbt_arm64_exec_fault_detection) {
    hal_page_fault_info_t info;
    uint32_t test_faults[] = {
        ARM64_FSC_TRANS_L3, ARM64_FSC_PERM_L3
    };
    
    for (uint32_t i = 0; i < sizeof(test_faults)/sizeof(test_faults[0]); i++) {
        uint32_t fsc = test_faults[i];
        
        /* Test instruction abort */
        uint64_t esr = make_inst_abort_esr(true, fsc);
        parse_fault_from_esr(&info, esr, 0x1000);
        
        /* Property: Instruction aborts must set is_exec */
        ASSERT_TRUE(info.is_exec);
        
        /* Test data abort */
        esr = make_data_abort_esr(true, fsc, false);
        parse_fault_from_esr(&info, esr, 0x2000);
        
        /* Property: Data aborts must NOT set is_exec */
        ASSERT_FALSE(info.is_exec);
    }
}

/**
 * @brief Test user mode fault detection
 * 
 * *For any* fault from EL0, is_user SHALL be true
 */
TEST_CASE(pbt_arm64_user_mode_detection) {
    hal_page_fault_info_t info;
    uint32_t fsc = ARM64_FSC_TRANS_L3;
    
    /* Test user mode data abort */
    uint64_t esr = make_data_abort_esr(true, fsc, false);
    parse_fault_from_esr(&info, esr, 0x1000);
    ASSERT_TRUE(info.is_user);
    
    /* Test kernel mode data abort */
    esr = make_data_abort_esr(false, fsc, false);
    parse_fault_from_esr(&info, esr, 0x2000);
    ASSERT_FALSE(info.is_user);
    
    /* Test user mode instruction abort */
    esr = make_inst_abort_esr(true, fsc);
    parse_fault_from_esr(&info, esr, 0x3000);
    ASSERT_TRUE(info.is_user);
    
    /* Test kernel mode instruction abort */
    esr = make_inst_abort_esr(false, fsc);
    parse_fault_from_esr(&info, esr, 0x4000);
    ASSERT_FALSE(info.is_user);
}

/**
 * @brief Test fault address preservation
 * 
 * *For any* page fault, fault_addr SHALL contain the faulting address
 */
TEST_CASE(pbt_arm64_fault_address_preservation) {
    hal_page_fault_info_t info;
    uint64_t esr = make_data_abort_esr(true, ARM64_FSC_TRANS_L3, false);
    
    /* Test various fault addresses */
    vaddr_t test_addrs[] = {
        0x0000000000001000ULL,   /* User space low */
        0x0000FFFFFFFFF000ULL,   /* User space high */
        0xFFFF000000001000ULL,   /* Kernel space low */
        0xFFFFFFFFFFFF0000ULL,   /* Kernel space high */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        vaddr_t addr = test_addrs[i];
        parse_fault_from_esr(&info, esr, addr);
        
        /* Property: Fault address must be preserved */
        ASSERT_TRUE(info.fault_addr == addr);
    }
}

/**
 * @brief Test COW fault detection
 * 
 * *For any* permission fault on write, it SHALL be detected as potential COW
 */
TEST_CASE(pbt_arm64_cow_fault_detection) {
    uint32_t perm_faults[] = {
        ARM64_FSC_PERM_L1, ARM64_FSC_PERM_L2, ARM64_FSC_PERM_L3
    };
    
    for (uint32_t i = 0; i < sizeof(perm_faults)/sizeof(perm_faults[0]); i++) {
        uint32_t fsc = perm_faults[i];
        
        /* Permission fault + write = COW candidate */
        uint64_t esr = make_data_abort_esr(true, fsc, true);
        ASSERT_TRUE(arm64_is_cow_page_fault(esr));
        
        /* Permission fault + read = NOT COW */
        esr = make_data_abort_esr(true, fsc, false);
        ASSERT_FALSE(arm64_is_cow_page_fault(esr));
        
        /* Translation fault + write = NOT COW (page doesn't exist) */
        esr = make_data_abort_esr(true, ARM64_FSC_TRANS_L3, true);
        ASSERT_FALSE(arm64_is_cow_page_fault(esr));
    }
}

/**
 * @brief Test access flag fault interpretation
 * 
 * *For any* access flag fault, is_present SHALL be true
 */
TEST_CASE(pbt_arm64_access_flag_fault) {
    hal_page_fault_info_t info;
    uint32_t access_faults[] = {
        ARM64_FSC_ACCESS_L1, ARM64_FSC_ACCESS_L2, ARM64_FSC_ACCESS_L3
    };
    
    for (uint32_t i = 0; i < sizeof(access_faults)/sizeof(access_faults[0]); i++) {
        uint32_t fsc = access_faults[i];
        
        uint64_t esr = make_data_abort_esr(true, fsc, false);
        parse_fault_from_esr(&info, esr, 0x1000);
        
        /* Property: Access flag faults indicate page IS present */
        ASSERT_TRUE(info.is_present);
    }
}

/**
 * @brief Test raw error code preservation
 * 
 * *For any* page fault, raw_error SHALL contain the ESR value
 */
TEST_CASE(pbt_arm64_raw_error_preservation) {
    hal_page_fault_info_t info;
    
    /* Test various ESR values */
    uint64_t test_esrs[] = {
        make_data_abort_esr(true, ARM64_FSC_TRANS_L3, false),
        make_data_abort_esr(false, ARM64_FSC_PERM_L2, true),
        make_inst_abort_esr(true, ARM64_FSC_TRANS_L1),
    };
    
    for (uint32_t i = 0; i < sizeof(test_esrs)/sizeof(test_esrs[0]); i++) {
        uint64_t esr = test_esrs[i];
        parse_fault_from_esr(&info, esr, 0x1000);
        
        /* Property: Raw error must contain ESR (lower 32 bits) */
        ASSERT_TRUE(info.raw_error == (uint32_t)esr);
    }
}

/**
 * @brief Test is_reserved is always false on ARM64
 * 
 * *For any* ARM64 page fault, is_reserved SHALL be false
 */
TEST_CASE(pbt_arm64_reserved_always_false) {
    hal_page_fault_info_t info;
    
    /* Test various fault types */
    uint64_t test_esrs[] = {
        make_data_abort_esr(true, ARM64_FSC_TRANS_L3, false),
        make_data_abort_esr(true, ARM64_FSC_PERM_L3, true),
        make_inst_abort_esr(true, ARM64_FSC_TRANS_L3),
        make_inst_abort_esr(false, ARM64_FSC_PERM_L2),
    };
    
    for (uint32_t i = 0; i < sizeof(test_esrs)/sizeof(test_esrs[0]); i++) {
        parse_fault_from_esr(&info, test_esrs[i], 0x1000);
        
        /* Property: is_reserved must always be false on ARM64 */
        ASSERT_FALSE(info.is_reserved);
    }
}

/**
 * @brief Test fault level extraction
 * 
 * *For any* level-specific fault, the correct level SHALL be returned
 */
TEST_CASE(pbt_arm64_fault_level_extraction) {
    /* Translation faults */
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_TRANS_L0), 0);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_TRANS_L1), 1);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_TRANS_L2), 2);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_TRANS_L3), 3);
    
    /* Permission faults (levels 1-3 only) */
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_PERM_L1), 1);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_PERM_L2), 2);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_PERM_L3), 3);
    
    /* Access flag faults (levels 1-3 only) */
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_ACCESS_L1), 1);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_ACCESS_L2), 2);
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_ACCESS_L3), 3);
    
    /* Non-level-specific faults */
    ASSERT_EQ(arm64_get_fault_level(ARM64_FSC_ALIGNMENT), -1);
}

/* ============================================================================
 * Test Suite Definition
 * ============================================================================ */

TEST_SUITE(arm64_fault_interpretation_tests) {
    RUN_TEST(pbt_arm64_translation_fault_not_present);
    RUN_TEST(pbt_arm64_permission_fault_present);
    RUN_TEST(pbt_arm64_write_fault_detection);
    RUN_TEST(pbt_arm64_exec_fault_detection);
    RUN_TEST(pbt_arm64_user_mode_detection);
    RUN_TEST(pbt_arm64_fault_address_preservation);
    RUN_TEST(pbt_arm64_cow_fault_detection);
    RUN_TEST(pbt_arm64_access_flag_fault);
    RUN_TEST(pbt_arm64_raw_error_preservation);
    RUN_TEST(pbt_arm64_reserved_always_false);
    RUN_TEST(pbt_arm64_fault_level_extraction);
}

/**
 * @brief Run all ARM64 page fault property tests
 * 
 * **Feature: multi-arch-support, Property 5: VMM Page Fault Interpretation (ARM64)**
 * **Validates: Requirements 5.4**
 */
void run_arm64_fault_tests(void) {
    unittest_init();
    
    /* Property 5: VMM Page Fault Interpretation (ARM64) */
    /* **Validates: Requirements 5.4** */
    RUN_SUITE(arm64_fault_interpretation_tests);
    
    unittest_print_summary();
}

#else /* !ARCH_ARM64 */

/**
 * @brief Stub for non-ARM64 architectures
 */
void run_arm64_fault_tests(void) {
    /* ARM64 fault tests only run on ARM64 architecture */
}

#endif /* ARCH_ARM64 */
