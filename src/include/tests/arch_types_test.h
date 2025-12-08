// ============================================================================
// arch_types_test.h - Architecture Type Size Property Tests
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

#ifndef _TESTS_ARCH_TYPES_TEST_H_
#define _TESTS_ARCH_TYPES_TEST_H_

/**
 * Run all architecture type size property tests
 */
void run_arch_types_tests(void);

#endif // _TESTS_ARCH_TYPES_TEST_H_
