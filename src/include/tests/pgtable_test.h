// ============================================================================
// pgtable_test.h - 页表抽象层单元测试
// ============================================================================
// 
// Property-Based Tests for pgtable.h
// **Feature: mm-refactor**
// **Validates: Requirements 3.1, 3.3, 3.4**
// ============================================================================

#ifndef _TESTS_PGTABLE_TEST_H_
#define _TESTS_PGTABLE_TEST_H_

/**
 * @brief 运行所有页表抽象层测试
 * 
 * 包含以下属性测试：
 * - Property 7: PTE Construction Round-Trip
 *   - MAKE_PTE 构造的页表项可以正确提取地址和标志
 */
void run_pgtable_tests(void);

#endif // _TESTS_PGTABLE_TEST_H_
