/**
 * @file arm64_mmu_test.h
 * @brief ARM64 MMU 属性测试头文件
 * 
 * **Feature: multi-arch-support**
 * **Property 4: VMM Kernel Mapping Range Correctness (ARM64)**
 * **Validates: Requirements 5.3**
 */

#ifndef _TESTS_ARM64_MMU_TEST_H_
#define _TESTS_ARM64_MMU_TEST_H_

/**
 * @brief Run all ARM64 MMU property tests
 * 
 * Tests include:
 * - Property 4: VMM Kernel Mapping Range Correctness (ARM64)
 */
void run_arm64_mmu_tests(void);

#endif /* _TESTS_ARM64_MMU_TEST_H_ */
