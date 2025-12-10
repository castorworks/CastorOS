// ============================================================================
// syscall_error_test.h - System Call Error Consistency Property Tests
// ============================================================================
//
// **Feature: multi-arch-support, Property 13: System Call Error Consistency**
// **Validates: Requirements 8.4**
// ============================================================================

#ifndef _TESTS_SYSCALL_ERROR_TEST_H_
#define _TESTS_SYSCALL_ERROR_TEST_H_

/**
 * @brief Run all system call error consistency property tests
 * 
 * Tests Property 13: For any system call that fails, the return value
 * SHALL be a negative errno value that is consistent across all
 * supported architectures for the same error condition.
 */
void run_syscall_error_tests(void);

#endif // _TESTS_SYSCALL_ERROR_TEST_H_
