// ============================================================================
// pgtable_test.c - 页表抽象层单元测试
// ============================================================================
// 
// Property-Based Tests for pgtable.h
// **Feature: mm-refactor**
// **Validates: Requirements 3.1, 3.3, 3.4**
// ============================================================================

#include <tests/ktest.h>
#include <tests/pgtable_test.h>
#include <mm/pgtable.h>
#include <lib/kprintf.h>

// ============================================================================
// Property 7: PTE Construction Round-Trip
// **Feature: mm-refactor, Property 7: PTE Construction Round-Trip**
// **Validates: Requirements 3.3, 3.4**
// ============================================================================

/**
 * Property Test: PTE construction preserves address
 * 
 * *For any* valid physical address addr and flags f, 
 * PTE_ADDR(MAKE_PTE(addr, f)) SHALL equal addr & PTE_ADDR_MASK.
 * 
 * This property ensures that constructing a PTE and extracting the address
 * produces the original address (masked to valid bits).
 */
TEST_CASE(test_pbt_pte_addr_roundtrip) {
    // Test with various page-aligned addresses
    paddr_t test_addrs[] = {
        0x0,                    // Zero address
        0x1000,                 // 4KB
        0x2000,                 // 8KB
        0x100000,               // 1MB
        0x1000000,              // 16MB
        0x10000000,             // 256MB
        0xFFFFF000UL,           // Just below 4GB (i686 max)
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
        0x100000000ULL,         // 4GB
        0x1000000000ULL,        // 64GB
        0xFFFFFFFFF000ULL,      // Large address (within 52-bit)
#endif
    };
    
    size_t num_addrs = sizeof(test_addrs) / sizeof(test_addrs[0]);
    
    for (size_t i = 0; i < num_addrs; i++) {
        paddr_t addr = test_addrs[i];
        uint32_t flags = PTE_FLAG_PRESENT | PTE_FLAG_WRITE;
        
        pte_t pte = MAKE_PTE(addr, flags);
        paddr_t extracted = PTE_ADDR(pte);
        
        // Property: extracted address must equal original (masked)
        paddr_t expected = addr & PTE_ADDR_MASK;
        ASSERT_TRUE(extracted == expected);
    }
}

/**
 * Property Test: PTE construction preserves flags
 * 
 * *For any* valid physical address addr and flags f,
 * PTE_FLAGS(MAKE_PTE(addr, f)) SHALL contain f (masked to valid flag bits).
 * 
 * This property ensures that constructing a PTE and extracting the flags
 * produces the original flags.
 */
TEST_CASE(test_pbt_pte_flags_roundtrip) {
    paddr_t addr = 0x1000;  // Use a simple page-aligned address
    
    // Test various flag combinations
    uint32_t test_flags[] = {
        0,                                          // No flags
        PTE_FLAG_PRESENT,                           // Present only
        PTE_FLAG_PRESENT | PTE_FLAG_WRITE,          // Present + Write
        PTE_FLAG_PRESENT | PTE_FLAG_USER,           // Present + User
        PTE_FLAG_PRESENT | PTE_FLAG_WRITE | PTE_FLAG_USER,  // All basic
        PTE_FLAG_PRESENT | PTE_FLAG_PCD,            // Present + No-cache
        PTE_FLAG_PRESENT | PTE_FLAG_ACCESSED,       // Present + Accessed
        PTE_FLAG_PRESENT | PTE_FLAG_DIRTY,          // Present + Dirty
        PTE_FLAG_PRESENT | PTE_FLAG_COW,            // Present + COW
        PTE_FLAG_PRESENT | PTE_FLAG_WRITE | PTE_FLAG_USER | 
            PTE_FLAG_ACCESSED | PTE_FLAG_DIRTY,     // Multiple flags
    };
    
    size_t num_flags = sizeof(test_flags) / sizeof(test_flags[0]);
    
    for (size_t i = 0; i < num_flags; i++) {
        uint32_t flags = test_flags[i];
        
        pte_t pte = MAKE_PTE(addr, flags);
        uint32_t extracted = PTE_FLAGS(pte);
        
        // Property: extracted flags must contain original flags
        // (may have additional bits from address overlap on some architectures)
        uint32_t expected = flags & PTE_FLAGS_MASK;
        ASSERT_TRUE((extracted & expected) == expected);
    }
}

/**
 * Property Test: PTE construction with combined address and flags
 * 
 * *For any* valid physical address addr and flags f,
 * MAKE_PTE(addr, f) SHALL produce a PTE where both address and flags
 * can be independently extracted.
 */
TEST_CASE(test_pbt_pte_combined_roundtrip) {
    // Test combinations of addresses and flags
    paddr_t addrs[] = {
        0x1000,
        0x100000,
        0x10000000,
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
        0x100000000ULL,
#endif
    };
    
    uint32_t flags_list[] = {
        PTE_FLAG_PRESENT,
        PTE_FLAG_PRESENT | PTE_FLAG_WRITE,
        PTE_FLAG_PRESENT | PTE_FLAG_WRITE | PTE_FLAG_USER,
    };
    
    size_t num_addrs = sizeof(addrs) / sizeof(addrs[0]);
    size_t num_flags = sizeof(flags_list) / sizeof(flags_list[0]);
    
    for (size_t i = 0; i < num_addrs; i++) {
        for (size_t j = 0; j < num_flags; j++) {
            paddr_t addr = addrs[i];
            uint32_t flags = flags_list[j];
            
            pte_t pte = MAKE_PTE(addr, flags);
            
            // Extract both components
            paddr_t extracted_addr = PTE_ADDR(pte);
            uint32_t extracted_flags = PTE_FLAGS(pte);
            
            // Property: address must be preserved
            ASSERT_TRUE(extracted_addr == (addr & PTE_ADDR_MASK));
            
            // Property: flags must be preserved
            ASSERT_TRUE((extracted_flags & flags) == flags);
        }
    }
}

/**
 * Property Test: PTE flag checking macros
 * 
 * *For any* PTE with specific flags set, the corresponding
 * flag checking macros SHALL return true.
 */
TEST_CASE(test_pbt_pte_flag_macros) {
    paddr_t addr = 0x1000;
    
    // Test PTE_PRESENT macro
    {
        pte_t pte_present = MAKE_PTE(addr, PTE_FLAG_PRESENT);
        pte_t pte_not_present = MAKE_PTE(addr, 0);
        
        ASSERT_TRUE(PTE_PRESENT(pte_present));
        ASSERT_FALSE(PTE_PRESENT(pte_not_present));
    }
    
    // Test PTE_WRITABLE macro
    {
        pte_t pte_writable = MAKE_PTE(addr, PTE_FLAG_PRESENT | PTE_FLAG_WRITE);
        pte_t pte_readonly = MAKE_PTE(addr, PTE_FLAG_PRESENT);
        
        ASSERT_TRUE(PTE_WRITABLE(pte_writable));
        ASSERT_FALSE(PTE_WRITABLE(pte_readonly));
    }
    
    // Test PTE_USER macro
    {
        pte_t pte_user = MAKE_PTE(addr, PTE_FLAG_PRESENT | PTE_FLAG_USER);
        pte_t pte_kernel = MAKE_PTE(addr, PTE_FLAG_PRESENT);
        
        ASSERT_TRUE(PTE_USER(pte_user));
        ASSERT_FALSE(PTE_USER(pte_kernel));
    }
    
    // Test PTE_IS_COW macro
    {
        pte_t pte_cow = MAKE_PTE(addr, PTE_FLAG_PRESENT | PTE_FLAG_COW);
        pte_t pte_not_cow = MAKE_PTE(addr, PTE_FLAG_PRESENT);
        
        ASSERT_TRUE(PTE_IS_COW(pte_cow));
        ASSERT_FALSE(PTE_IS_COW(pte_not_cow));
    }
    
    // Test PTE_ACCESSED macro
    {
        pte_t pte_accessed = MAKE_PTE(addr, PTE_FLAG_PRESENT | PTE_FLAG_ACCESSED);
        pte_t pte_not_accessed = MAKE_PTE(addr, PTE_FLAG_PRESENT);
        
        ASSERT_TRUE(PTE_ACCESSED(pte_accessed));
        ASSERT_FALSE(PTE_ACCESSED(pte_not_accessed));
    }
    
    // Test PTE_DIRTY macro
    {
        pte_t pte_dirty = MAKE_PTE(addr, PTE_FLAG_PRESENT | PTE_FLAG_DIRTY);
        pte_t pte_clean = MAKE_PTE(addr, PTE_FLAG_PRESENT);
        
        ASSERT_TRUE(PTE_DIRTY(pte_dirty));
        ASSERT_FALSE(PTE_DIRTY(pte_clean));
    }
}

/**
 * Property Test: PTE type size is architecture-appropriate
 * 
 * *For any* architecture, pte_t SHALL be:
 * - 4 bytes on 32-bit architectures (i686)
 * - 8 bytes on 64-bit architectures (x86_64, ARM64)
 */
TEST_CASE(test_pbt_pte_type_size) {
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
    // 64-bit architectures: pte_t should be 8 bytes
    ASSERT_EQ_U(sizeof(pte_t), 8);
    ASSERT_EQ_U(sizeof(pde_t), 8);
#else
    // 32-bit architectures (i686): pte_t should be 4 bytes
    ASSERT_EQ_U(sizeof(pte_t), 4);
    ASSERT_EQ_U(sizeof(pde_t), 4);
#endif
}

/**
 * Property Test: Virtual address decomposition
 * 
 * *For any* virtual address, the index extraction macros SHALL
 * produce values within valid ranges.
 */
TEST_CASE(test_pbt_va_index_range) {
    vaddr_t test_addrs[] = {
        0x0,
        0x1000,
        0x12345678,
        0x80000000,             // Kernel base (i686)
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
        0x7FFFFFFFFFFF,         // User space max
        0xFFFF800000000000ULL,  // Kernel space start (x86_64)
#endif
    };
    
    size_t num_addrs = sizeof(test_addrs) / sizeof(test_addrs[0]);
    
    for (size_t i = 0; i < num_addrs; i++) {
        vaddr_t va = test_addrs[i];
        
#if defined(ARCH_X86_64)
        // x86_64: 9-bit indices (0-511)
        ASSERT_TRUE(VA_PML4_INDEX(va) < 512);
        ASSERT_TRUE(VA_PDPT_INDEX(va) < 512);
        ASSERT_TRUE(VA_PD_INDEX(va) < 512);
        ASSERT_TRUE(VA_PT_INDEX(va) < 512);
#elif defined(ARCH_ARM64)
        // ARM64: 9-bit indices (0-511)
        ASSERT_TRUE(VA_L0_INDEX(va) < 512);
        ASSERT_TRUE(VA_L1_INDEX(va) < 512);
        ASSERT_TRUE(VA_L2_INDEX(va) < 512);
        ASSERT_TRUE(VA_L3_INDEX(va) < 512);
#else
        // i686: 10-bit indices (0-1023)
        ASSERT_TRUE(VA_PD_INDEX(va) < 1024);
        ASSERT_TRUE(VA_PT_INDEX(va) < 1024);
#endif
        
        // Page offset should always be 0-4095
        ASSERT_TRUE(VA_PAGE_OFFSET(va) < PAGE_SIZE);
    }
}

/**
 * Property Test: Page offset extraction
 * 
 * *For any* virtual address, VA_PAGE_OFFSET SHALL extract
 * the lower 12 bits correctly.
 */
TEST_CASE(test_pbt_va_page_offset) {
    // Test specific offset values
    struct {
        vaddr_t addr;
        uint32_t expected_offset;
    } test_cases[] = {
        { 0x0,          0 },
        { 0x1,          1 },
        { 0xFFF,        0xFFF },
        { 0x1000,       0 },
        { 0x1001,       1 },
        { 0x1FFF,       0xFFF },
        { 0x12345678,   0x678 },
        { 0x80000000,   0 },
        { 0x80000ABC,   0xABC },
    };
    
    size_t num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (size_t i = 0; i < num_cases; i++) {
        vaddr_t va = test_cases[i].addr;
        uint32_t expected = test_cases[i].expected_offset;
        uint32_t actual = (uint32_t)VA_PAGE_OFFSET(va);
        
        ASSERT_EQ_U(actual, expected);
    }
}

// ============================================================================
// Test Suites
// ============================================================================

TEST_SUITE(pgtable_pte_tests) {
    RUN_TEST(test_pbt_pte_addr_roundtrip);
    RUN_TEST(test_pbt_pte_flags_roundtrip);
    RUN_TEST(test_pbt_pte_combined_roundtrip);
    RUN_TEST(test_pbt_pte_flag_macros);
    RUN_TEST(test_pbt_pte_type_size);
}

TEST_SUITE(pgtable_va_tests) {
    RUN_TEST(test_pbt_va_index_range);
    RUN_TEST(test_pbt_va_page_offset);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_pgtable_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // Property 7: PTE Construction Round-Trip
    // **Feature: mm-refactor, Property 7: PTE Construction Round-Trip**
    // **Validates: Requirements 3.3, 3.4**
    RUN_SUITE(pgtable_pte_tests);
    
    // Virtual address decomposition tests
    RUN_SUITE(pgtable_va_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
