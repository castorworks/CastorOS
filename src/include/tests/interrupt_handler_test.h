/**
 * @file interrupt_handler_test.h
 * @brief Interrupt Handler Registration Tests Header
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */

#ifndef _TESTS_INTERRUPT_HANDLER_TEST_H_
#define _TESTS_INTERRUPT_HANDLER_TEST_H_

/**
 * @brief Run interrupt handler registration API consistency tests
 * 
 * Tests that the HAL interrupt registration API provides consistent
 * behavior across all supported architectures.
 */
void run_interrupt_handler_tests(void);

#endif /* _TESTS_INTERRUPT_HANDLER_TEST_H_ */
