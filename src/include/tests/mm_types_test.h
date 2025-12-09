// ============================================================================
// mm_types_test.h - 内存管理类型单元测试
// ============================================================================
// 
// Property-Based Tests for mm_types.h
// **Feature: mm-refactor**
// **Validates: Requirements 1.1, 1.2, 1.5**
// ============================================================================

#ifndef _TESTS_MM_TYPES_TEST_H_
#define _TESTS_MM_TYPES_TEST_H_

/**
 * @brief 运行所有内存管理类型测试
 * 
 * 包含以下属性测试：
 * - Property 1: Physical Address Type Size (paddr_t = 8 bytes)
 * - Property 2: Virtual Address Type Size (vaddr_t = sizeof(void*))
 * - Property 3: PFN Conversion Round-Trip
 */
void run_mm_types_tests(void);

#endif // _TESTS_MM_TYPES_TEST_H_
