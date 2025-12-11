// ============================================================================
// heap_test.h - 堆内存管理器单元测试
// ============================================================================
//
// 模块名称: heap
// 子系统: mm (内存管理)
// 描述: 测试 Heap (Dynamic Memory Allocator) 的功能
//
// 功能覆盖:
//   - 内存分配 (kmalloc)
//   - 内存释放 (kfree)
//   - 重新分配 (krealloc)
//   - 分配并清零 (kcalloc)
//   - 边界条件和错误处理
//   - 内存合并 (coalescing)
//   - 压力测试
//
// **Feature: test-refactor**
// **Validates: Requirements 3.3, 10.1, 11.1**
// ============================================================================

#ifndef _TESTS_HEAP_TEST_H_
#define _TESTS_HEAP_TEST_H_

/**
 * @brief 运行所有 Heap 单元测试
 *
 * 执行以下测试套件：
 *   1. heap_alloc_tests - 内存分配测试
 *   2. heap_free_tests - 内存释放测试
 *   3. heap_realloc_tests - 重新分配测试
 *   4. heap_calloc_tests - 分配并清零测试
 *   5. heap_boundary_tests - 边界条件测试
 *   6. heap_coalesce_tests - 内存合并测试
 *   7. heap_comprehensive_tests - 综合测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 11.1**
 */
void run_heap_tests(void);

#endif // _TESTS_HEAP_TEST_H_
