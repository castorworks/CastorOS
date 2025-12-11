// ============================================================================
// vfs_test.h - VFS 单元测试头文件
// ============================================================================
//
// 模块名称: vfs
// 子系统: fs (文件系统)
// 描述: 测试 VFS (Virtual File System) 的功能
//
// **Feature: test-refactor**
// **Validates: Requirements 4.1, 4.2**
// ============================================================================

#ifndef _TESTS_VFS_TEST_H_
#define _TESTS_VFS_TEST_H_

/**
 * @brief 运行所有 VFS 测试
 *
 * 测试覆盖:
 *   - 文件打开、关闭
 *   - 文件读写
 *   - 目录操作
 *   - 路径解析
 *   - 文件创建和删除
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.1, 4.2**
 */
void run_vfs_tests(void);

#endif // _TESTS_VFS_TEST_H_
