// ============================================================================
// usermode_test.h - User Mode Transition Property Tests Header
// ============================================================================
//
// **Feature: multi-arch-support, Property 11: User Mode Transition Correctness**
// **Validates: Requirements 7.4**
// ============================================================================

#ifndef _TESTS_USERMODE_TEST_H_
#define _TESTS_USERMODE_TEST_H_

/**
 * @brief Run all user mode transition property tests
 * 
 * This function runs the property tests that verify:
 * - Segment selectors have correct privilege levels
 * - IRETQ/IRET stack frame structure is correct
 * - RFLAGS has interrupts enabled for user mode
 * - User and kernel segments are properly separated
 */
void run_usermode_tests(void);

#endif // _TESTS_USERMODE_TEST_H_
