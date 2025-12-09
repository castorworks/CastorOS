/**
 * @file paging64_test.c
 * @brief x86_64 分页属性测试
 * 
 * 实现 x86_64 架构的分页相关属性测试
 * 
 * **Feature: multi-arch-support**
 * **Property 4: VMM Kernel Mapping Range Correctness (x86_64)**
 * **Property 5: VMM Page Fault Interpretation (x86_64)**
 * **Validates: Requirements 5.3, 5.4**
 */

#include <tests/ktest.h>
#include <tests/paging64_test.h>
#include <types.h>
#include <lib/kprintf.h>

#ifdef ARCH_X86_64

#include "paging64.h"

/* x86_64 specific constants */
#define KERNEL_VIRTUAL_BASE_X64     0xFFFF800000000000ULL
#define USER_SPACE_END_X64          0x00007FFFFFFFFFFFULL
#define PHYS_ADDR_MAX_X64           0x0000FFFFFFFFFFFFULL

/* ============================================================================
 * Property 4: VMM Kernel Mapping Range Correctness (x86_64)
 * 
 * *For any* kernel virtual address, the address SHALL fall within 
 * the architecture-appropriate higher-half range 
 * (≥0xFFFF800000000000 for x86_64).
 * 
 * **Validates: Requirements 5.3**
 * ============================================================================ */

/**
 * @brief Test that KERNEL_VIRTUAL_BASE is correct for x86_64
 */
TEST_CASE(test_pbt_x86_64_kernel_base_address) {
    /* Property: KERNEL_VIRTUAL_BASE must be 0xFFFF800000000000 for x86_64 */
    uint64_t expected_base = KERNEL_VIRTUAL_BASE_X64;
    uint64_t actual_base = x86_64_get_kernel_virtual_base();
    
    ASSERT_TRUE(actual_base == expected_base);
}

/**
 * @brief Test canonical address validation for kernel addresses
 * 
 * *For any* kernel address, it must be a valid canonical address
 * (bits 63:48 must all be 1 for high-half addresses)
 */
TEST_CASE(test_pbt_x86_64_kernel_canonical_addresses) {
    #define PBT_ITERATIONS 20
    
    /* Test various kernel addresses */
    uint64_t test_addrs[] = {
        KERNEL_VIRTUAL_BASE_X64,                    /* Base address */
        KERNEL_VIRTUAL_BASE_X64 + 0x1000,           /* Base + 4KB */
        KERNEL_VIRTUAL_BASE_X64 + 0x100000,         /* Base + 1MB */
        KERNEL_VIRTUAL_BASE_X64 + 0x10000000,       /* Base + 256MB */
        KERNEL_VIRTUAL_BASE_X64 + 0x100000000ULL,   /* Base + 4GB */
        0xFFFF800000000000ULL,                      /* Exact base */
        0xFFFF800000001000ULL,                      /* Base + 4KB */
        0xFFFF8000FFFFFFFFULL,                      /* Base + 4GB - 1 */
        0xFFFFFFFF80000000ULL,                      /* High kernel address */
        0xFFFFFFFFFFFFFFFFULL,                      /* Maximum address */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: All kernel addresses must be canonical */
        bool is_canonical = x86_64_is_canonical_address(addr);
        ASSERT_TRUE(is_canonical);
        
        /* Property: All kernel addresses must be >= KERNEL_VIRTUAL_BASE */
        bool is_kernel = x86_64_is_kernel_address(addr);
        ASSERT_TRUE(is_kernel);
        
        /* Property: Kernel addresses must NOT be user addresses */
        bool is_user = x86_64_is_user_address(addr);
        ASSERT_FALSE(is_user);
    }
}

/**
 * @brief Test canonical address validation for user addresses
 * 
 * *For any* user address, it must be a valid canonical address
 * (bits 63:48 must all be 0 for low-half addresses)
 */
TEST_CASE(test_pbt_x86_64_user_canonical_addresses) {
    /* Test various user addresses */
    uint64_t test_addrs[] = {
        0x0000000000001000ULL,                      /* First valid user page */
        0x0000000000010000ULL,                      /* 64KB */
        0x0000000000100000ULL,                      /* 1MB */
        0x0000000001000000ULL,                      /* 16MB */
        0x0000000010000000ULL,                      /* 256MB */
        0x0000000100000000ULL,                      /* 4GB */
        0x0000001000000000ULL,                      /* 64GB */
        0x00007FFFFFFFF000ULL,                      /* Near end of user space */
        USER_SPACE_END_X64,                         /* End of user space */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: All user addresses must be canonical */
        bool is_canonical = x86_64_is_canonical_address(addr);
        ASSERT_TRUE(is_canonical);
        
        /* Property: User addresses must be user addresses */
        bool is_user = x86_64_is_user_address(addr);
        ASSERT_TRUE(is_user);
        
        /* Property: User addresses must NOT be kernel addresses */
        bool is_kernel = x86_64_is_kernel_address(addr);
        ASSERT_FALSE(is_kernel);
    }
}

/**
 * @brief Test non-canonical address detection
 * 
 * *For any* address in the canonical hole (0x0000800000000000 - 0xFFFF7FFFFFFFFFFF),
 * it must be detected as non-canonical
 */
TEST_CASE(test_pbt_x86_64_noncanonical_addresses) {
    /* Test addresses in the canonical hole */
    uint64_t test_addrs[] = {
        0x0000800000000000ULL,                      /* Start of hole */
        0x0000FFFFFFFFFFFFULL,                      /* Middle of hole */
        0x0001000000000000ULL,                      /* In hole */
        0x7FFFFFFFFFFFFFFFULL,                      /* In hole */
        0x8000000000000000ULL,                      /* In hole */
        0xFFFF7FFFFFFFFFFFULL,                      /* End of hole */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: Addresses in canonical hole must be non-canonical */
        bool is_canonical = x86_64_is_canonical_address(addr);
        ASSERT_FALSE(is_canonical);
    }
}

/**
 * @brief Test page table level count
 * 
 * *For any* x86_64 system, the page table SHALL use 4 levels
 */
TEST_CASE(test_pbt_x86_64_page_table_levels) {
    /* Property: x86_64 must use 4-level page tables */
    uint32_t levels = x86_64_get_page_table_levels();
    ASSERT_EQ_U(levels, 4);
}

/**
 * @brief Test page size
 * 
 * *For any* x86_64 system, the standard page size SHALL be 4KB
 */
TEST_CASE(test_pbt_x86_64_page_size) {
    /* Property: Standard page size must be 4KB */
    uint32_t page_size = x86_64_get_page_size();
    ASSERT_EQ_U(page_size, 4096);
}

/* ============================================================================
 * Property 5: VMM Page Fault Interpretation (x86_64)
 * 
 * *For any* page fault exception, the VMM SHALL correctly interpret 
 * the architecture-specific fault information (CR2 and error code on x86)
 * to determine the faulting address and fault type.
 * 
 * **Validates: Requirements 5.4**
 * ============================================================================ */

/**
 * @brief Test page fault error code parsing - present bit
 * 
 * *For any* page fault error code, the present bit (bit 0) SHALL be
 * correctly interpreted
 */
TEST_CASE(test_pbt_x86_64_page_fault_present_bit) {
    /* Test error codes with present bit = 0 (page not present) */
    uint64_t not_present_codes[] = {
        0x0,    /* Read from non-present page, kernel mode */
        0x2,    /* Write to non-present page, kernel mode */
        0x4,    /* Read from non-present page, user mode */
        0x6,    /* Write to non-present page, user mode */
        0x10,   /* Instruction fetch from non-present page */
    };
    
    for (uint32_t i = 0; i < sizeof(not_present_codes)/sizeof(not_present_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(not_present_codes[i]);
        ASSERT_FALSE(info.present);
    }
    
    /* Test error codes with present bit = 1 (protection violation) */
    uint64_t present_codes[] = {
        0x1,    /* Read protection violation, kernel mode */
        0x3,    /* Write protection violation, kernel mode */
        0x5,    /* Read protection violation, user mode */
        0x7,    /* Write protection violation, user mode */
        0x11,   /* Instruction fetch protection violation */
    };
    
    for (uint32_t i = 0; i < sizeof(present_codes)/sizeof(present_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(present_codes[i]);
        ASSERT_TRUE(info.present);
    }
}

/**
 * @brief Test page fault error code parsing - write bit
 * 
 * *For any* page fault error code, the write bit (bit 1) SHALL be
 * correctly interpreted
 */
TEST_CASE(test_pbt_x86_64_page_fault_write_bit) {
    /* Test error codes with write bit = 0 (read access) */
    uint64_t read_codes[] = {
        0x0,    /* Read from non-present page */
        0x1,    /* Read protection violation */
        0x4,    /* Read from non-present page, user mode */
        0x5,    /* Read protection violation, user mode */
    };
    
    for (uint32_t i = 0; i < sizeof(read_codes)/sizeof(read_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(read_codes[i]);
        ASSERT_FALSE(info.write);
    }
    
    /* Test error codes with write bit = 1 (write access) */
    uint64_t write_codes[] = {
        0x2,    /* Write to non-present page */
        0x3,    /* Write protection violation */
        0x6,    /* Write to non-present page, user mode */
        0x7,    /* Write protection violation, user mode */
    };
    
    for (uint32_t i = 0; i < sizeof(write_codes)/sizeof(write_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(write_codes[i]);
        ASSERT_TRUE(info.write);
    }
}

/**
 * @brief Test page fault error code parsing - user bit
 * 
 * *For any* page fault error code, the user bit (bit 2) SHALL be
 * correctly interpreted
 */
TEST_CASE(test_pbt_x86_64_page_fault_user_bit) {
    /* Test error codes with user bit = 0 (kernel mode) */
    uint64_t kernel_codes[] = {
        0x0,    /* Kernel read from non-present page */
        0x1,    /* Kernel read protection violation */
        0x2,    /* Kernel write to non-present page */
        0x3,    /* Kernel write protection violation */
    };
    
    for (uint32_t i = 0; i < sizeof(kernel_codes)/sizeof(kernel_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(kernel_codes[i]);
        ASSERT_FALSE(info.user);
    }
    
    /* Test error codes with user bit = 1 (user mode) */
    uint64_t user_codes[] = {
        0x4,    /* User read from non-present page */
        0x5,    /* User read protection violation */
        0x6,    /* User write to non-present page */
        0x7,    /* User write protection violation */
    };
    
    for (uint32_t i = 0; i < sizeof(user_codes)/sizeof(user_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(user_codes[i]);
        ASSERT_TRUE(info.user);
    }
}

/**
 * @brief Test COW fault detection
 * 
 * *For any* page fault with present=1 and write=1, it SHALL be
 * detected as a potential COW fault
 */
TEST_CASE(test_pbt_x86_64_cow_fault_detection) {
    /* COW faults have present=1 and write=1 (error code & 0x3 == 0x3) */
    uint64_t cow_codes[] = {
        0x3,    /* Kernel write protection violation */
        0x7,    /* User write protection violation */
        0x0B,   /* With reserved bit */
        0x13,   /* With instruction fetch bit */
    };
    
    for (uint32_t i = 0; i < sizeof(cow_codes)/sizeof(cow_codes[0]); i++) {
        bool is_cow = x86_64_is_cow_fault(cow_codes[i]);
        ASSERT_TRUE(is_cow);
    }
    
    /* Non-COW faults */
    uint64_t non_cow_codes[] = {
        0x0,    /* Page not present, read */
        0x1,    /* Read protection violation */
        0x2,    /* Page not present, write */
        0x4,    /* Page not present, user read */
        0x5,    /* User read protection violation */
        0x6,    /* Page not present, user write */
    };
    
    for (uint32_t i = 0; i < sizeof(non_cow_codes)/sizeof(non_cow_codes[0]); i++) {
        bool is_cow = x86_64_is_cow_fault(non_cow_codes[i]);
        ASSERT_FALSE(is_cow);
    }
}

/**
 * @brief Test page table entry validation
 * 
 * *For any* page table entry, the validation function SHALL correctly
 * identify valid and invalid entries
 */
TEST_CASE(test_pbt_x86_64_pte_validation) {
    /* Valid entries (present with page-aligned addresses) */
    pte64_t valid_entries[] = {
        0x0000000000001003ULL,   /* Present, writable, page at 0x1000 */
        0x0000000000002007ULL,   /* Present, writable, user, page at 0x2000 */
        0x00000000FFFFF003ULL,   /* Present, writable, high address */
        0x0000FFFFFFFF0003ULL,   /* Present, writable, very high address */
    };
    
    for (uint32_t i = 0; i < sizeof(valid_entries)/sizeof(valid_entries[0]); i++) {
        bool is_valid = x86_64_validate_pte_format(valid_entries[i]);
        ASSERT_TRUE(is_valid);
    }
    
    /* Non-present entries (always valid format-wise) */
    pte64_t non_present_entries[] = {
        0x0000000000000000ULL,   /* Empty entry */
        0x0000000000001000ULL,   /* Address but not present */
    };
    
    for (uint32_t i = 0; i < sizeof(non_present_entries)/sizeof(non_present_entries[0]); i++) {
        bool is_valid = x86_64_validate_pte_format(non_present_entries[i]);
        ASSERT_TRUE(is_valid);  /* Non-present entries are always "valid" */
    }
}

/* ============================================================================
 * Test Suites
 * ============================================================================ */

TEST_SUITE(paging64_kernel_range_tests) {
    RUN_TEST(test_pbt_x86_64_kernel_base_address);
    RUN_TEST(test_pbt_x86_64_kernel_canonical_addresses);
    RUN_TEST(test_pbt_x86_64_user_canonical_addresses);
    RUN_TEST(test_pbt_x86_64_noncanonical_addresses);
    RUN_TEST(test_pbt_x86_64_page_table_levels);
    RUN_TEST(test_pbt_x86_64_page_size);
}

TEST_SUITE(paging64_page_fault_tests) {
    RUN_TEST(test_pbt_x86_64_page_fault_present_bit);
    RUN_TEST(test_pbt_x86_64_page_fault_write_bit);
    RUN_TEST(test_pbt_x86_64_page_fault_user_bit);
    RUN_TEST(test_pbt_x86_64_cow_fault_detection);
    RUN_TEST(test_pbt_x86_64_pte_validation);
}

#endif /* ARCH_X86_64 */

/* ============================================================================
 * Run All Tests
 * ============================================================================ */

void run_paging64_tests(void) {
#ifdef ARCH_X86_64
    unittest_init();
    
    /* Property 4: VMM Kernel Mapping Range Correctness (x86_64) */
    /* **Validates: Requirements 5.3** */
    RUN_SUITE(paging64_kernel_range_tests);
    
    /* Property 5: VMM Page Fault Interpretation (x86_64) */
    /* **Validates: Requirements 5.4** */
    RUN_SUITE(paging64_page_fault_tests);
    
    unittest_print_summary();
#else
    kprintf("Paging64 tests skipped (not x86_64 architecture)\n");
#endif
}
