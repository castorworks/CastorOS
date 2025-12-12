// ============================================================================
// arch_types_test.c - Architecture Type Size Property Tests
// ============================================================================
//
// Property-based tests for verifying architecture-specific data type sizes.
//
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// This test verifies that pointer and size_t types match the architecture's
// native word size (32-bit on i686, 64-bit on x86_64 and ARM64).
// ============================================================================

#include <tests/ktest.h>
#include <tests/arch/arch_types_test.h>
#include <types.h>

// Include architecture-specific types
#if defined(ARCH_I686)
    #include <arch/i686/arch_types.h>
#elif defined(ARCH_X86_64)
    #include <arch/x86_64/arch_types.h>
#elif defined(ARCH_ARM64)
    #include <arch/arm64/arch_types.h>
#endif

// ============================================================================
// Property Test: Pointer Size Matches Architecture Word Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// For any pointer type in the user library, the size SHALL match the
// architecture's native word size (32-bit on i686, 64-bit on x86_64 and ARM64).
// ============================================================================

TEST_CASE(test_pointer_size_matches_arch_word_size) {
    void *ptr = NULL;
    size_t pointer_size = sizeof(ptr);
    
#if defined(ARCH_I686)
    // i686: 32-bit architecture, pointers should be 4 bytes
    ASSERT_EQ_UINT(pointer_size, 4);
    ASSERT_EQ_UINT(ARCH_BITS, 32);
    ASSERT_EQ_UINT(ARCH_IS_64BIT, 0);
#elif defined(ARCH_X86_64)
    // x86_64: 64-bit architecture, pointers should be 8 bytes
    ASSERT_EQ_UINT(pointer_size, 8);
    ASSERT_EQ_UINT(ARCH_BITS, 64);
    ASSERT_EQ_UINT(ARCH_IS_64BIT, 1);
#elif defined(ARCH_ARM64)
    // ARM64: 64-bit architecture, pointers should be 8 bytes
    ASSERT_EQ_UINT(pointer_size, 8);
    ASSERT_EQ_UINT(ARCH_BITS, 64);
    ASSERT_EQ_UINT(ARCH_IS_64BIT, 1);
#else
    TEST_FAIL("Unknown architecture - cannot verify pointer size");
#endif
}

// ============================================================================
// Property Test: uintptr_t Size Matches Pointer Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// For any uintptr_t type, the size SHALL match the pointer size, ensuring
// that pointers can be safely cast to uintptr_t and back.
// ============================================================================

TEST_CASE(test_uintptr_size_matches_pointer_size) {
    void *ptr = NULL;
    uintptr_t uptr = 0;
    
    // uintptr_t must be able to hold any pointer value
    ASSERT_EQ_UINT(sizeof(uptr), sizeof(ptr));
    
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(sizeof(uintptr_t), 4);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_UINT(sizeof(uintptr_t), 8);
#endif
}

// ============================================================================
// Property Test: intptr_t Size Matches Pointer Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// For any intptr_t type, the size SHALL match the pointer size.
// ============================================================================

TEST_CASE(test_intptr_size_matches_pointer_size) {
    void *ptr = NULL;
    intptr_t iptr = 0;
    
    // intptr_t must be able to hold any pointer value
    ASSERT_EQ_UINT(sizeof(iptr), sizeof(ptr));
    
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(sizeof(intptr_t), 4);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_UINT(sizeof(intptr_t), 8);
#endif
}

// ============================================================================
// Property Test: arch_size_t Size Matches Architecture Word Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// For any arch_size_t type, the size SHALL match the architecture's native
// word size.
// ============================================================================

TEST_CASE(test_arch_size_t_matches_word_size) {
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(sizeof(arch_size_t), 4);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_UINT(sizeof(arch_size_t), 8);
#endif
}

// ============================================================================
// Property Test: arch_ssize_t Size Matches Architecture Word Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// For any arch_ssize_t type, the size SHALL match the architecture's native
// word size.
// ============================================================================

TEST_CASE(test_arch_ssize_t_matches_word_size) {
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(sizeof(arch_ssize_t), 4);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_UINT(sizeof(arch_ssize_t), 8);
#endif
}

// ============================================================================
// Property Test: GPR_SIZE Matches Architecture Word Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// The general purpose register size constant SHALL match the architecture's
// native word size.
// ============================================================================

TEST_CASE(test_gpr_size_matches_word_size) {
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(GPR_SIZE, 4);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_UINT(GPR_SIZE, 8);
#endif
}

// ============================================================================
// Property Test: Fixed-Width Integer Types Have Correct Sizes
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// Fixed-width integer types SHALL have the correct sizes regardless of
// architecture.
// ============================================================================

TEST_CASE(test_fixed_width_integer_sizes) {
    // These should be the same on all architectures
    ASSERT_EQ_UINT(sizeof(uint8_t), 1);
    ASSERT_EQ_UINT(sizeof(uint16_t), 2);
    ASSERT_EQ_UINT(sizeof(uint32_t), 4);
    ASSERT_EQ_UINT(sizeof(uint64_t), 8);
    
    ASSERT_EQ_UINT(sizeof(int8_t), 1);
    ASSERT_EQ_UINT(sizeof(int16_t), 2);
    ASSERT_EQ_UINT(sizeof(int32_t), 4);
    ASSERT_EQ_UINT(sizeof(int64_t), 8);
}

// ============================================================================
// Property Test: PAGE_SIZE Constant Is Correct
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// PAGE_SIZE SHALL be 4096 bytes for all currently supported architectures.
// ============================================================================

TEST_CASE(test_page_size_constant) {
    // All supported architectures use 4KB pages as the standard page size
    ASSERT_EQ_UINT(PAGE_SIZE, 4096);
    ASSERT_EQ_UINT(PAGE_SHIFT, 12);
}

// ============================================================================
// Property Test: PAGE_TABLE_LEVELS Is Architecture Appropriate
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// PAGE_TABLE_LEVELS SHALL be 2 for i686 and 4 for x86_64/ARM64.
// ============================================================================

TEST_CASE(test_page_table_levels) {
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(PAGE_TABLE_LEVELS, 2);
    ASSERT_EQ_UINT(PAGE_TABLE_ENTRIES, 1024);
    ASSERT_EQ_UINT(PAGE_TABLE_ENTRY_SIZE, 4);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_UINT(PAGE_TABLE_LEVELS, 4);
    ASSERT_EQ_UINT(PAGE_TABLE_ENTRIES, 512);
    ASSERT_EQ_UINT(PAGE_TABLE_ENTRY_SIZE, 8);
#endif
}

// ============================================================================
// Property Test: hal_context Structure Size Is Reasonable
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// The hal_context structure SHALL have a size appropriate for the architecture.
// ============================================================================

TEST_CASE(test_hal_context_size) {
    size_t ctx_size = sizeof(struct hal_context);
    
    // Context must be large enough to hold all registers
    ASSERT_TRUE(ctx_size > 0);
    
#if defined(ARCH_I686)
    // i686 context: segment regs (4*4) + GPRs (8*4) + int frame (2*4) + 
    // CPU pushed (3*4) + user mode (2*4) = 76 bytes minimum
    ASSERT_TRUE(ctx_size >= 76);
#elif defined(ARCH_X86_64)
    // x86_64 context: GPRs (15*8) + int info (2*8) + CPU pushed (5*8) = 176 bytes minimum
    ASSERT_TRUE(ctx_size >= 176);
#elif defined(ARCH_ARM64)
    // ARM64 context: X0-X30 (31*8) + SP + PC + PSTATE + TTBR0 + ESR + FAR = 296 bytes minimum
    ASSERT_TRUE(ctx_size >= 296);
#endif
}

// ============================================================================
// Property Test: Pointer Arithmetic Works Correctly
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// Pointer arithmetic SHALL work correctly with the architecture's word size.
// ============================================================================

TEST_CASE(test_pointer_arithmetic) {
    uint8_t array[16];
    uint8_t *ptr1 = &array[0];
    uint8_t *ptr2 = &array[8];
    
    // Pointer difference should work correctly
    ASSERT_EQ((uintptr_t)(ptr2 - ptr1), 8);
    
    // Casting to uintptr_t and back should preserve the pointer
    uintptr_t addr = (uintptr_t)ptr1;
    uint8_t *ptr3 = (uint8_t *)addr;
    ASSERT_EQ_PTR(ptr1, ptr3);
}

// ============================================================================
// Test Suite Definition
// ============================================================================

TEST_SUITE(arch_types_property_tests) {
    RUN_TEST(test_pointer_size_matches_arch_word_size);
    RUN_TEST(test_uintptr_size_matches_pointer_size);
    RUN_TEST(test_intptr_size_matches_pointer_size);
    RUN_TEST(test_arch_size_t_matches_word_size);
    RUN_TEST(test_arch_ssize_t_matches_word_size);
    RUN_TEST(test_gpr_size_matches_word_size);
    RUN_TEST(test_fixed_width_integer_sizes);
    RUN_TEST(test_page_size_constant);
    RUN_TEST(test_page_table_levels);
    RUN_TEST(test_hal_context_size);
    RUN_TEST(test_pointer_arithmetic);
}

// ============================================================================
// Run All Tests
// ============================================================================

void run_arch_types_tests(void) {
    // Initialize test framework
    unittest_init();
    
    // Run all test suites
    RUN_SUITE(arch_types_property_tests);
    
    // Print test summary
    unittest_print_summary();
}
