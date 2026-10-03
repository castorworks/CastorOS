/**
 * @file arm64_mmu_test.c
 * @brief ARM64 MMU 属性测试
 * 
 * 实现 ARM64 架构的 MMU 相关属性测试
 * 
 * **Feature: multi-arch-support**
 * **Property 4: VMM Kernel Mapping Range Correctness (ARM64)**
 * **Validates: Requirements 5.3**
 */

#include <tests/ktest.h>
#include <tests/arch/arm64/arm64_mmu_test.h>
#include <types.h>
#include <lib/kprintf.h>

#ifdef ARCH_ARM64

#include <hal/hal.h>
#include <mm/mm_types.h>

/* ARM64 specific constants */
#define KERNEL_VIRTUAL_BASE_ARM64   0xFFFF000000000000ULL
#define USER_SPACE_END_ARM64        0x0000FFFFFFFFFFFFULL

/* ============================================================================
 * ARM64 Address Helper Functions
 * ============================================================================ */

/**
 * @brief Get the ARM64 kernel virtual base address
 * @return Kernel virtual base (0xFFFF000000000000)
 */
static inline uint64_t arm64_get_kernel_virtual_base(void) {
    return KERNEL_VIRTUAL_BASE_ARM64;
}

/**
 * @brief Check if an address is a kernel address (high-half)
 * @param addr Address to check
 * @return true if kernel address
 * 
 * ARM64 uses TTBR1 for addresses with bit 55 set (0xFFFF...)
 */
static inline bool arm64_is_kernel_address(uint64_t addr) {
    return addr >= KERNEL_VIRTUAL_BASE_ARM64;
}

/**
 * @brief Check if an address is a user address (low-half)
 * @param addr Address to check
 * @return true if user address
 * 
 * ARM64 uses TTBR0 for addresses with bit 55 clear (0x0000...)
 */
static inline bool arm64_is_user_address(uint64_t addr) {
    return addr <= USER_SPACE_END_ARM64;
}


/**
 * @brief Get the number of page table levels for ARM64
 * @return 4 (ARM64 uses 4-level page tables with 4KB granule)
 */
static inline uint32_t arm64_get_page_table_levels(void) {
    return 4;
}

/**
 * @brief Get the standard page size for ARM64
 * @return 4096 (4KB)
 */
static inline uint32_t arm64_get_page_size(void) {
    return 4096;
}

/* ============================================================================
 * Property 4: VMM Kernel Mapping Range Correctness (ARM64)
 * 
 * *For any* kernel virtual address, the address SHALL fall within 
 * the architecture-appropriate higher-half range 
 * (≥0xFFFF000000000000 for ARM64).
 * 
 * **Validates: Requirements 5.3**
 * ============================================================================ */

/**
 * @brief Test that KERNEL_VIRTUAL_BASE is correct for ARM64
 */
TEST_CASE(pbt_arm64_kernel_base_address) {
    /* Property: KERNEL_VIRTUAL_BASE must be 0xFFFF000000000000 for ARM64 */
    uint64_t expected_base = KERNEL_VIRTUAL_BASE_ARM64;
    uint64_t actual_base = arm64_get_kernel_virtual_base();
    
    ASSERT_TRUE(actual_base == expected_base);
}

/**
 * @brief Test kernel address validation for ARM64
 * 
 * *For any* kernel address, it must be in the high-half address space
 * (bits 63:48 must be 0xFFFF for kernel addresses)
 */
TEST_CASE(pbt_arm64_kernel_addresses) {
    /* Test various kernel addresses */
    uint64_t test_addrs[] = {
        KERNEL_VIRTUAL_BASE_ARM64,                    /* Base address */
        KERNEL_VIRTUAL_BASE_ARM64 + 0x1000,           /* Base + 4KB */
        KERNEL_VIRTUAL_BASE_ARM64 + 0x100000,         /* Base + 1MB */
        KERNEL_VIRTUAL_BASE_ARM64 + 0x10000000,       /* Base + 256MB */
        KERNEL_VIRTUAL_BASE_ARM64 + 0x100000000ULL,   /* Base + 4GB */
        0xFFFF000000000000ULL,                        /* Exact base */
        0xFFFF000000001000ULL,                        /* Base + 4KB */
        0xFFFF0000FFFFFFFFULL,                        /* Base + 4GB - 1 */
        0xFFFFFFFF80000000ULL,                        /* High kernel address */
        0xFFFFFFFFFFFFFFFFULL,                        /* Maximum address */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: All kernel addresses must be >= KERNEL_VIRTUAL_BASE */
        bool is_kernel = arm64_is_kernel_address(addr);
        ASSERT_TRUE(is_kernel);
        
        /* Property: Kernel addresses must NOT be user addresses */
        bool is_user = arm64_is_user_address(addr);
        ASSERT_FALSE(is_user);
    }
}

/**
 * @brief Test user address validation for ARM64
 * 
 * *For any* user address, it must be in the low-half address space
 * (bits 63:48 must be 0x0000 for user addresses)
 */
TEST_CASE(pbt_arm64_user_addresses) {
    /* Test various user addresses */
    uint64_t test_addrs[] = {
        0x0000000000001000ULL,                        /* First valid user page */
        0x0000000000010000ULL,                        /* 64KB */
        0x0000000000100000ULL,                        /* 1MB */
        0x0000000001000000ULL,                        /* 16MB */
        0x0000000010000000ULL,                        /* 256MB */
        0x0000000100000000ULL,                        /* 4GB */
        0x0000001000000000ULL,                        /* 64GB */
        0x0000FFFFFFFFFFFFULL,                        /* Near end of user space */
        USER_SPACE_END_ARM64,                         /* End of user space */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: User addresses must be user addresses */
        bool is_user = arm64_is_user_address(addr);
        ASSERT_TRUE(is_user);
        
        /* Property: User addresses must NOT be kernel addresses */
        bool is_kernel = arm64_is_kernel_address(addr);
        ASSERT_FALSE(is_kernel);
    }
}


/**
 * @brief Test address space boundary
 * 
 * *For any* address at the boundary between user and kernel space,
 * it must be correctly classified
 */
TEST_CASE(pbt_arm64_address_space_boundary) {
    /* Test boundary addresses */
    
    /* Last user address */
    uint64_t last_user = USER_SPACE_END_ARM64;
    ASSERT_TRUE(arm64_is_user_address(last_user));
    ASSERT_FALSE(arm64_is_kernel_address(last_user));
    
    /* First kernel address */
    uint64_t first_kernel = KERNEL_VIRTUAL_BASE_ARM64;
    ASSERT_TRUE(arm64_is_kernel_address(first_kernel));
    ASSERT_FALSE(arm64_is_user_address(first_kernel));
    
    /* Address in the gap (should be neither valid user nor kernel) */
    /* ARM64 has a gap between 0x0000FFFFFFFFFFFF and 0xFFFF000000000000 */
    uint64_t gap_addr = 0x0001000000000000ULL;
    ASSERT_FALSE(arm64_is_user_address(gap_addr));
    ASSERT_FALSE(arm64_is_kernel_address(gap_addr));
}

/**
 * @brief Test page table level count
 * 
 * *For any* ARM64 system with 4KB granule, the page table SHALL use 4 levels
 */
TEST_CASE(pbt_arm64_page_table_levels) {
    /* Property: ARM64 must use 4-level page tables (with 4KB granule) */
    uint32_t levels = arm64_get_page_table_levels();
    ASSERT_EQ_U(levels, 4);
}

/**
 * @brief Test page size
 * 
 * *For any* ARM64 system with 4KB granule, the standard page size SHALL be 4KB
 */
TEST_CASE(pbt_arm64_page_size) {
    /* Property: Standard page size must be 4KB */
    uint32_t page_size = arm64_get_page_size();
    ASSERT_EQ_U(page_size, 4096);
}

/**
 * @brief Test KERNEL_VIRTUAL_BASE macro consistency
 * 
 * *For any* ARM64 build, the KERNEL_VIRTUAL_BASE macro must match
 * the expected ARM64 kernel base address
 */
TEST_CASE(pbt_arm64_kernel_base_macro) {
    /* Property: KERNEL_VIRTUAL_BASE macro must be correct for ARM64 */
    ASSERT_TRUE(KERNEL_VIRTUAL_BASE == KERNEL_VIRTUAL_BASE_ARM64);
}

/**
 * @brief Test address translation macros
 * 
 * *For any* physical address in the direct-mapped region,
 * PADDR_TO_KVADDR and KVADDR_TO_PADDR must be inverses
 */
TEST_CASE(pbt_arm64_address_translation_roundtrip) {
    /* Test various physical addresses */
    uint64_t test_paddrs[] = {
        0x0000000000000000ULL,                        /* Physical 0 */
        0x0000000000001000ULL,                        /* 4KB */
        0x0000000000100000ULL,                        /* 1MB */
        0x0000000010000000ULL,                        /* 256MB */
        0x0000000100000000ULL,                        /* 4GB */
    };
    
    for (uint32_t i = 0; i < sizeof(test_paddrs)/sizeof(test_paddrs[0]); i++) {
        paddr_t paddr = test_paddrs[i];
        
        /* Convert to kernel virtual address */
        vaddr_t vaddr = PADDR_TO_KVADDR(paddr);
        
        /* Property: Kernel virtual address must be in kernel space */
        ASSERT_TRUE(arm64_is_kernel_address(vaddr));
        
        /* Property: Round-trip must return original address */
        paddr_t roundtrip = KVADDR_TO_PADDR(vaddr);
        ASSERT_TRUE(roundtrip == paddr);
    }
}

/* ============================================================================
 * Test Suite Definition
 * ============================================================================ */

TEST_SUITE(arm64_mmu_kernel_range_tests) {
    RUN_TEST(pbt_arm64_kernel_base_address);
    RUN_TEST(pbt_arm64_kernel_addresses);
    RUN_TEST(pbt_arm64_user_addresses);
    RUN_TEST(pbt_arm64_address_space_boundary);
    RUN_TEST(pbt_arm64_page_table_levels);
    RUN_TEST(pbt_arm64_page_size);
    RUN_TEST(pbt_arm64_kernel_base_macro);
    RUN_TEST(pbt_arm64_address_translation_roundtrip);
}

/**
 * @brief Run all ARM64 MMU property tests
 */
void run_arm64_mmu_tests(void) {
    unittest_init();
    
    /* Property 4: VMM Kernel Mapping Range Correctness (ARM64) */
    /* **Validates: Requirements 5.3** */
    RUN_SUITE(arm64_mmu_kernel_range_tests);
    
    unittest_print_summary();
}

#else /* !ARCH_ARM64 */

/**
 * @brief Stub for non-ARM64 architectures
 */
void run_arm64_mmu_tests(void) {
    /* ARM64 MMU tests only run on ARM64 architecture */
}

#endif /* ARCH_ARM64 */
