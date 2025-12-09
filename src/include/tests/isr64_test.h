/**
 * @file isr64_test.h
 * @brief x86_64 ISR Register Preservation Tests
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)**
 * **Validates: Requirements 6.1**
 */

#ifndef _TESTS_ISR64_TEST_H_
#define _TESTS_ISR64_TEST_H_

/**
 * @brief Run x86_64 ISR register preservation tests
 * 
 * Tests that verify the registers64_t structure layout matches
 * the assembly stub's register save/restore order.
 */
void run_isr64_tests(void);

#endif /* _TESTS_ISR64_TEST_H_ */
