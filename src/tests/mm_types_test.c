// ============================================================================
// mm_types_test.c - 内存管理类型单元测试
// ============================================================================
// 
// Property-Based Tests for mm_types.h
// **Feature: mm-refactor**
// **Validates: Requirements 1.1, 1.2, 1.5**
// ============================================================================

#include <tests/ktest.h>
#include <tests/mm_types_test.h>
#include <mm/mm_types.h>
#include <lib/kprintf.h>

// ============================================================================
// Property 1: Physical Address Type Size
// **Feature: mm-refactor, Property 1: Physical Address Type Size**
// **Validates: Requirements 1.1**
// ============================================================================

/**
 * Property Test: paddr_t is always 64-bit
 * 
 * *For any* architecture, sizeof(paddr_t) SHALL equal 8 bytes (64-bit).
 * This ensures that physical addresses can represent the full address space
 * on 64-bit architectures while maintaining consistency across all platforms.
 */
TEST_CASE(test_pbt_paddr_type_size) {
    // Property: paddr_t must be exactly 8 bytes (64-bit) on all architectures
    ASSERT_EQ_U(sizeof(paddr_t), 8);
    
    // Verify pfn_t is also 64-bit (same underlying type)
    ASSERT_EQ_U(sizeof(pfn_t), 8);
}

/**
 * Property Test: paddr_t can represent full physical address range
 * 
 * *For any* valid physical address within PHYS_ADDR_MAX, paddr_t SHALL
 * be able to store and retrieve the value without truncation.
 */
TEST_CASE(test_pbt_paddr_range) {
    // Test that paddr_t can hold maximum physical address
    paddr_t max_addr = PHYS_ADDR_MAX;
    
    // On i686, max is 4GB-1
    // On x86_64, max is 2^52-1
    // On ARM64, max is 2^48-1
#if defined(ARCH_X86_64)
    ASSERT_TRUE(max_addr >= 0xFFFFFFFFFFFFFULL);  // At least 52 bits
#elif defined(ARCH_ARM64)
    ASSERT_TRUE(max_addr >= 0xFFFFFFFFFFFFULL);   // At least 48 bits
#else
    ASSERT_TRUE(max_addr >= 0xFFFFFFFFULL);       // At least 32 bits
#endif
    
    // Verify PADDR_INVALID is distinct from any valid address
    ASSERT_NE_U((uint32_t)(PADDR_INVALID & 0xFFFFFFFF), 0);
}

// ============================================================================
// Property 2: Virtual Address Type Size
// **Feature: mm-refactor, Property 2: Virtual Address Type Size**
// **Validates: Requirements 1.2**
// ============================================================================

/**
 * Property Test: vaddr_t matches pointer size
 * 
 * *For any* architecture, sizeof(vaddr_t) SHALL equal sizeof(void*).
 * This ensures that virtual addresses can be safely cast to/from pointers.
 */
TEST_CASE(test_pbt_vaddr_type_size) {
    // Property: vaddr_t must match pointer size
    ASSERT_EQ_U(sizeof(vaddr_t), sizeof(void*));
    
    // Also verify it matches uintptr_t (which it's typedef'd from)
    ASSERT_EQ_U(sizeof(vaddr_t), sizeof(uintptr_t));
}

/**
 * Property Test: vaddr_t size is architecture-appropriate
 * 
 * *For any* architecture, vaddr_t SHALL be:
 * - 4 bytes on 32-bit architectures (i686)
 * - 8 bytes on 64-bit architectures (x86_64, ARM64)
 */
TEST_CASE(test_pbt_vaddr_arch_size) {
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
    // 64-bit architectures: vaddr_t should be 8 bytes
    ASSERT_EQ_U(sizeof(vaddr_t), 8);
#else
    // 32-bit architectures (i686): vaddr_t should be 4 bytes
    ASSERT_EQ_U(sizeof(vaddr_t), 4);
#endif
}

/**
 * Property Test: vaddr_t can represent kernel virtual base
 * 
 * *For any* architecture, vaddr_t SHALL be able to represent
 * KERNEL_VIRTUAL_BASE without truncation.
 */
TEST_CASE(test_pbt_vaddr_kernel_base) {
    vaddr_t kernel_base = KERNEL_VIRTUAL_BASE;
    
    // Verify the value is preserved (no truncation)
#if defined(ARCH_X86_64)
    ASSERT_TRUE(kernel_base == 0xFFFF800000000000ULL);
#elif defined(ARCH_ARM64)
    ASSERT_TRUE(kernel_base == 0xFFFF000000000000ULL);
#else
    ASSERT_TRUE(kernel_base == 0x80000000UL);
#endif
}

// ============================================================================
// Property 3: PFN Conversion Round-Trip
// **Feature: mm-refactor, Property 3: PFN Conversion Round-Trip**
// **Validates: Requirements 1.5**
// ============================================================================

/**
 * Property Test: PFN conversion round-trip
 * 
 * *For any* valid page frame number pfn, PADDR_TO_PFN(PFN_TO_PADDR(pfn)) 
 * SHALL equal pfn.
 * 
 * This property ensures that converting a PFN to a physical address and back
 * preserves the original value, which is essential for correct page table
 * management.
 */
TEST_CASE(test_pbt_pfn_roundtrip) {
    // Test round-trip for various PFN values
    #define PBT_PFN_ITERATIONS 100
    
    // Test specific boundary values
    pfn_t test_pfns[] = {
        0,                      // First page
        1,                      // Second page
        0xFF,                   // 255
        0x100,                  // 256
        0xFFFF,                 // 64K pages
        0x10000,                // 64K+1 pages
        0xFFFFF,                // 1M pages (4GB boundary for i686)
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
        0x100000,               // 1M+1 pages
        0xFFFFFFFF,             // 4G pages (16TB)
        0x100000000ULL,         // 4G+1 pages
#endif
    };
    
    size_t num_test_pfns = sizeof(test_pfns) / sizeof(test_pfns[0]);
    
    for (size_t i = 0; i < num_test_pfns; i++) {
        pfn_t pfn = test_pfns[i];
        paddr_t paddr = PFN_TO_PADDR(pfn);
        pfn_t result = PADDR_TO_PFN(paddr);
        
        // Property: round-trip must preserve the original PFN
        ASSERT_TRUE(result == pfn);
    }
    
    // Test sequential PFNs
    for (pfn_t pfn = 0; pfn < PBT_PFN_ITERATIONS; pfn++) {
        paddr_t paddr = PFN_TO_PADDR(pfn);
        pfn_t result = PADDR_TO_PFN(paddr);
        ASSERT_TRUE(result == pfn);
    }
}

/**
 * Property Test: PFN to PADDR produces page-aligned addresses
 * 
 * *For any* valid page frame number pfn, PFN_TO_PADDR(pfn) SHALL be
 * page-aligned (divisible by PAGE_SIZE).
 */
TEST_CASE(test_pbt_pfn_to_paddr_aligned) {
    #define PBT_ALIGN_ITERATIONS 100
    
    for (pfn_t pfn = 0; pfn < PBT_ALIGN_ITERATIONS; pfn++) {
        paddr_t paddr = PFN_TO_PADDR(pfn);
        
        // Property: resulting address must be page-aligned
        ASSERT_TRUE(IS_PADDR_ALIGNED(paddr));
        ASSERT_EQ_U((uint32_t)(paddr & (PAGE_SIZE - 1)), 0);
    }
    
    // Test some larger PFNs
    pfn_t large_pfns[] = { 0x1000, 0x10000, 0x100000 };
    for (size_t i = 0; i < sizeof(large_pfns)/sizeof(large_pfns[0]); i++) {
        paddr_t paddr = PFN_TO_PADDR(large_pfns[i]);
        ASSERT_TRUE(IS_PADDR_ALIGNED(paddr));
    }
}

/**
 * Property Test: PADDR to PFN for page-aligned addresses
 * 
 * *For any* page-aligned physical address pa, PADDR_TO_PFN(pa) SHALL
 * produce a valid PFN that can be converted back to the same address.
 */
TEST_CASE(test_pbt_paddr_to_pfn_roundtrip) {
    // Test with page-aligned addresses
    paddr_t test_addrs[] = {
        0x0,                    // 0
        0x1000,                 // 4KB
        0x2000,                 // 8KB
        0x100000,               // 1MB
        0x1000000,              // 16MB
        0x10000000,             // 256MB
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
        0x100000000ULL,         // 4GB
        0x1000000000ULL,        // 64GB
#endif
    };
    
    size_t num_addrs = sizeof(test_addrs) / sizeof(test_addrs[0]);
    
    for (size_t i = 0; i < num_addrs; i++) {
        paddr_t paddr = test_addrs[i];
        pfn_t pfn = PADDR_TO_PFN(paddr);
        paddr_t result = PFN_TO_PADDR(pfn);
        
        // Property: round-trip must preserve the original address
        ASSERT_TRUE(result == paddr);
    }
}

/**
 * Property Test: Page alignment macros
 * 
 * *For any* physical address pa:
 * - PADDR_ALIGN_DOWN(pa) <= pa
 * - PADDR_ALIGN_UP(pa) >= pa
 * - Both results are page-aligned
 */
TEST_CASE(test_pbt_page_alignment_macros) {
    // Test various addresses including non-aligned ones
    paddr_t test_addrs[] = {
        0x0,
        0x1,
        0x123,
        0xFFF,
        0x1000,
        0x1001,
        0x1FFF,
        0x2000,
        0x12345678,
    };
    
    size_t num_addrs = sizeof(test_addrs) / sizeof(test_addrs[0]);
    
    for (size_t i = 0; i < num_addrs; i++) {
        paddr_t pa = test_addrs[i];
        paddr_t down = PADDR_ALIGN_DOWN(pa);
        paddr_t up = PADDR_ALIGN_UP(pa);
        
        // Property: ALIGN_DOWN <= original
        ASSERT_TRUE(down <= pa);
        
        // Property: ALIGN_UP >= original
        ASSERT_TRUE(up >= pa);
        
        // Property: both results are page-aligned
        ASSERT_TRUE(IS_PADDR_ALIGNED(down));
        ASSERT_TRUE(IS_PADDR_ALIGNED(up));
        
        // Property: if already aligned, both should equal original
        if (IS_PADDR_ALIGNED(pa)) {
            ASSERT_TRUE(down == pa);
            ASSERT_TRUE(up == pa);
        }
    }
}

// ============================================================================
// Test Suites
// ============================================================================

TEST_SUITE(mm_types_paddr_tests) {
    RUN_TEST(test_pbt_paddr_type_size);
    RUN_TEST(test_pbt_paddr_range);
}

TEST_SUITE(mm_types_vaddr_tests) {
    RUN_TEST(test_pbt_vaddr_type_size);
    RUN_TEST(test_pbt_vaddr_arch_size);
    RUN_TEST(test_pbt_vaddr_kernel_base);
}

TEST_SUITE(mm_types_pfn_tests) {
    RUN_TEST(test_pbt_pfn_roundtrip);
    RUN_TEST(test_pbt_pfn_to_paddr_aligned);
    RUN_TEST(test_pbt_paddr_to_pfn_roundtrip);
    RUN_TEST(test_pbt_page_alignment_macros);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_mm_types_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // Property 1: Physical Address Type Size
    // **Feature: mm-refactor, Property 1: Physical Address Type Size**
    // **Validates: Requirements 1.1**
    RUN_SUITE(mm_types_paddr_tests);
    
    // Property 2: Virtual Address Type Size
    // **Feature: mm-refactor, Property 2: Virtual Address Type Size**
    // **Validates: Requirements 1.2**
    RUN_SUITE(mm_types_vaddr_tests);
    
    // Property 3: PFN Conversion Round-Trip
    // **Feature: mm-refactor, Property 3: PFN Conversion Round-Trip**
    // **Validates: Requirements 1.5**
    RUN_SUITE(mm_types_pfn_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
