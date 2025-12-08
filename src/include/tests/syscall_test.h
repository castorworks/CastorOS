// ============================================================================
// syscall_test.h - System Call Property Tests Header
// ============================================================================
//
// **Feature: multi-arch-support, Property 12: System Call Round-Trip Correctness**
// **Validates: Requirements 8.1, 8.2, 8.3**
//
// This header declares the system call property tests.
// ============================================================================

#ifndef _TESTS_SYSCALL_TEST_H_
#define _TESTS_SYSCALL_TEST_H_

/**
 * run_syscall_tests - Run all system call property tests
 *
 * This function runs property tests that verify:
 * - System call numbers are correctly dispatched
 * - Arguments are correctly passed to handlers
 * - Return values are correctly returned
 * - Error codes are consistent across architectures
 */
void run_syscall_tests(void);

#endif // _TESTS_SYSCALL_TEST_H_

