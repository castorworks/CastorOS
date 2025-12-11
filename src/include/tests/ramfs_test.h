// ============================================================================
// ramfs_test.h - Ramfs 单元测试头文件
// ============================================================================
//
// 模块名称: ramfs
// 子系统: fs (文件系统)
// 描述: 测试 Ramfs (RAM-based File System) 的功能
//
// **Feature: test-refactor**
// **Validates: Requirements 4.4**
// ============================================================================

#ifndef _TESTS_RAMFS_TEST_H_
#define _TESTS_RAMFS_TEST_H_

/**
 * @brief 运行所有 Ramfs 测试
 *
 * 测试覆盖:
 *   - 文件创建和删除
 *   - 内容持久化（写入后读取）
 *   - 目录创建和删除
 *   - 文件重命名
 *   - 边界条件处理
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.4**
 */
void run_ramfs_tests(void);

#endif // _TESTS_RAMFS_TEST_H_
