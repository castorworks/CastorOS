/**
 * @file arm64_exception_test.h
 * @brief ARM64 Exception Register Preservation Tests Header
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */

#ifndef _TESTS_ARM64_EXCEPTION_TEST_H_
#define _TESTS_ARM64_EXCEPTION_TEST_H_

/**
 * @brief Run ARM64 exception register preservation tests
 * 
 * Tests that the arm64_regs_t structure layout matches the assembly
 * stub's register save/restore order.
 */
void run_arm64_exception_tests(void);

#endif /* _TESTS_ARM64_EXCEPTION_TEST_H_ */
