// ============================================================================
// fat32_test.h - FAT32 文件系统测试模块头文件
// ============================================================================
//
// 模块名称: fat32
// 子系统: fs (文件系统)
// 描述: FAT32 文件系统测试的公共接口
//
// **Feature: test-refactor**
// **Validates: Requirements 4.3, 11.2**
// ============================================================================

#ifndef _TESTS_FAT32_TEST_H_
#define _TESTS_FAT32_TEST_H_

/**
 * @brief 运行所有 FAT32 测试
 *
 * 执行以下测试套件：
 *   - fat32_dirent_tests: 目录项解析测试
 *   - fat32_shortname_tests: 短文件名测试
 *   - fat32_lfn_tests: 长文件名测试
 *   - fat32_edge_tests: 边界条件测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.3**
 */
void run_fat32_tests(void);

#endif // _TESTS_FAT32_TEST_H_
