// ============================================================================
// devfs_test.h - Devfs 测试模块头文件
// ============================================================================
//
// 模块名称: devfs
// 子系统: fs (文件系统)
// 描述: 测试 Devfs (Device File System) 的功能
//
// **Feature: test-refactor**
// **Validates: Requirements 4.5**
// ============================================================================

#ifndef _TESTS_DEVFS_TEST_H_
#define _TESTS_DEVFS_TEST_H_

/**
 * @brief 运行所有 Devfs 测试
 *
 * 按功能组织的测试套件：
 *   1. devfs_device_tests - 设备注册和访问测试
 *   2. devfs_null_tests - /dev/null 设备测试
 *   3. devfs_zero_tests - /dev/zero 设备测试
 *   4. devfs_dir_tests - 目录操作测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.5**
 */
void run_devfs_tests(void);

#endif // _TESTS_DEVFS_TEST_H_
