// ============================================================================
// pci_test.h - PCI 驱动测试模块头文件
// ============================================================================
//
// PCI 总线驱动测试，验证配置空间读写操作
//
// **Feature: test-refactor**
// **Validates: Requirements 6.2**
//
// 测试覆盖:
//   - PCI 配置空间读写操作
//   - PCI 设备枚举和查找
//   - BAR 地址提取和类型检测
//   - PCI 命令寄存器操作
// ============================================================================

#ifndef _TESTS_PCI_TEST_H_
#define _TESTS_PCI_TEST_H_

/**
 * @brief 运行所有 PCI 驱动测试
 * 
 * 测试 PCI 配置空间读写、设备枚举和 BAR 操作
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 6.2**
 */
void run_pci_tests(void);

#endif // _TESTS_PCI_TEST_H_
