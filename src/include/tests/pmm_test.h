// ============================================================================
// pmm_test.h - 物理内存管理器单元测试
// ============================================================================
//
// 模块名称: pmm
// 子系统: mm (内存管理)
// 描述: 测试 PMM (Physical Memory Manager) 的功能
//
// 功能覆盖:
//   - 页帧分配 (pmm_alloc_frame)
//   - 页帧释放 (pmm_free_frame)
//   - 信息查询 (pmm_get_info)
//   - 引用计数 (pmm_frame_ref_inc, pmm_frame_ref_dec)
//   - 压力测试
//
// **Feature: test-refactor**
// **Validates: Requirements 3.1, 10.1, 11.1**
// ============================================================================

#ifndef _TESTS_PMM_TEST_H_
#define _TESTS_PMM_TEST_H_

/**
 * @brief 运行所有 PMM 单元测试
 * 
 * 执行以下测试套件：
 *   1. pmm_alloc_tests - 页帧分配测试
 *   2. pmm_free_tests - 页帧释放测试
 *   3. pmm_info_tests - 信息查询测试
 *   4. pmm_stress_tests - 压力测试
 *   5. pmm_property_tests - 分配属性测试 (PBT)
 *   6. pmm_refcount_property_tests - 引用计数属性测试 (PBT)
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 11.1**
 */
void run_pmm_tests(void);

#endif // _TESTS_PMM_TEST_H_
