/**
 * @file paging64_test.h
 * @brief x86_64 分页属性测试头文件
 * 
 * **Feature: multi-arch-support**
 * **Property 4: VMM Kernel Mapping Range Correctness (x86_64)**
 * **Property 5: VMM Page Fault Interpretation (x86_64)**
 * **Validates: Requirements 5.3, 5.4**
 */

#ifndef _TESTS_PAGING64_TEST_H_
#define _TESTS_PAGING64_TEST_H_

/**
 * @brief Run all x86_64 paging property tests
 * 
 * Tests include:
 * - Property 4: VMM Kernel Mapping Range Correctness (x86_64)
 * - Property 5: VMM Page Fault Interpretation (x86_64)
 */
void run_paging64_tests(void);

#endif /* _TESTS_PAGING64_TEST_H_ */
