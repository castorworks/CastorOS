/**
 * @file hal_test.h
 * @brief HAL (Hardware Abstraction Layer) Property Tests
 * 
 * Property-based tests for HAL initialization and I/O operations.
 * 
 * **Feature: multi-arch-support**
 * **Property 1: HAL Initialization Dispatch**
 * **Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 1.1, 9.1**
 */

#ifndef _TESTS_HAL_TEST_H_
#define _TESTS_HAL_TEST_H_

/**
 * @brief Run all HAL property tests
 * 
 * Tests include:
 *   - Property 1: HAL Initialization Dispatch
 *   - Property 14: MMIO Memory Barrier Correctness
 */
void run_hal_tests(void);

#endif /* _TESTS_HAL_TEST_H_ */
