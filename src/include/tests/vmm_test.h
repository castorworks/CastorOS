// ============================================================================
// vmm_test.h - 虚拟内存管理器单元测试
// ============================================================================
//
// 模块名称: vmm
// 子系统: mm (内存管理)
// 描述: 测试 VMM (Virtual Memory Manager) 的功能
//
// 功能覆盖:
//   - 页面映射 (vmm_map_page, vmm_map_page_in_directory)
//   - 取消映射 (vmm_unmap_page, vmm_unmap_page_in_directory)
//   - 页目录操作 (vmm_create_page_directory, vmm_clone_page_directory)
//   - TLB 刷新 (vmm_flush_tlb)
//   - COW 引用计数
//   - MMIO 映射
//
// 依赖模块:
//   - pmm (物理内存管理器)
//
// **Feature: test-refactor**
// **Validates: Requirements 3.2, 7.2, 10.1, 11.1**
// ============================================================================

#ifndef _TESTS_VMM_TEST_H_
#define _TESTS_VMM_TEST_H_

/**
 * @brief 运行所有 VMM 单元测试
 * 
 * 执行以下测试套件：
 *   1. vmm_map_tests - 页面映射测试
 *   2. vmm_unmap_tests - 取消映射测试
 *   3. vmm_tlb_tests - TLB 刷新测试
 *   4. vmm_directory_tests - 页目录操作测试
 *   5. vmm_cow_tests - COW 引用计数测试
 *   6. vmm_comprehensive_tests - 综合测试
 *   7. vmm_property_tests - 属性测试 (PBT)
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 11.1**
 */
void run_vmm_tests(void);

#endif // _TESTS_VMM_TEST_H_
