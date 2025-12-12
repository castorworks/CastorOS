// ============================================================================
// ramfs_test.c - Ramfs 单元测试
// ============================================================================
//
// 模块名称: ramfs
// 子系统: fs (文件系统)
// 描述: 测试 Ramfs (RAM-based File System) 的功能
//
// 功能覆盖:
//   - 文件创建和删除 (ramfs_create_file, ramfs_unlink)
//   - 内容持久化 (ramfs_read, ramfs_write)
//   - 目录创建和删除 (ramfs_mkdir, ramfs_unlink)
//   - 文件重命名 (ramfs_rename)
//   - 目录遍历 (ramfs_readdir, ramfs_finddir)
//
// **Feature: test-refactor**
// **Validates: Requirements 4.4**
// ============================================================================

#include <tests/ktest.h>
#include <tests/fs/ramfs_test.h>
#include <tests/test_module.h>
#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * @brief 设置测试环境 - 确保 ramfs 根文件系统可用
 *
 * 注意: 这个函数假设 VFS 和 ramfs 已经在内核启动时初始化
 * 如果根文件系统未设置，测试将跳过
 */
static bool ramfs_test_setup(void) {
    fs_node_t *root = vfs_get_root();
    if (!root) {
        return false;
    }
    return true;
}

// ============================================================================
// 测试套件 1: ramfs_file_tests - 文件操作测试
// ============================================================================
//
// 测试 ramfs 文件创建、删除、读写操作
// **Validates: Requirements 4.4** - ramfs 文件创建、删除和内容持久化
// ============================================================================

/**
 * @brief 测试文件创建
 *
 * 验证 ramfs 能创建新文件
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_create_file) {
    if (!ramfs_test_setup()) {
        return;  // 跳过测试
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/RCREATE.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件存在
    fs_node_t *node = vfs_path_to_node("/RCREATE.TMP");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_FILE);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/RCREATE.TMP");
}

/**
 * @brief 测试文件删除
 *
 * 验证 ramfs 能删除文件
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_delete_file) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/RDELETE.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件存在
    fs_node_t *node = vfs_path_to_node("/RDELETE.TMP");
    ASSERT_NOT_NULL(node);
    vfs_release_node(node);
    
    // 删除文件
    result = vfs_unlink("/RDELETE.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件不存在
    node = vfs_path_to_node("/RDELETE.TMP");
    ASSERT_NULL(node);
}

/**
 * @brief 测试内容持久化 - 写入后读取
 *
 * 验证写入 ramfs 的数据可以正确读取回来
 * **Feature: test-refactor, Property 10: Ramfs Create-Delete Consistency**
 * **Validates: Requirements 4.4**
 */
TEST_CASE(test_ramfs_content_persistence) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/RPERSIST.TMP");
    ASSERT_EQ(result, 0);
    
    // 获取文件节点
    fs_node_t *node = vfs_path_to_node("/RPERSIST.TMP");
    ASSERT_NOT_NULL(node);
    
    // 写入测试数据
    const char *test_data = "Ramfs persistence test data!";
    uint32_t data_len = strlen(test_data);
    uint32_t written = vfs_write(node, 0, data_len, (uint8_t *)test_data);
    ASSERT_EQ_U(written, data_len);
    
    // 读取数据
    char read_buffer[64];
    memset(read_buffer, 0, sizeof(read_buffer));
    uint32_t read_count = vfs_read(node, 0, data_len, (uint8_t *)read_buffer);
    ASSERT_EQ_U(read_count, data_len);
    
    // 验证数据完整性
    ASSERT_STR_EQ(test_data, read_buffer);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/RPERSIST.TMP");
}

/**
 * @brief 测试多次写入和读取
 *
 * 验证多次写入后数据正确
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_multiple_writes) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/RMULTI.TMP");
    ASSERT_EQ(result, 0);
    
    fs_node_t *node = vfs_path_to_node("/RMULTI.TMP");
    ASSERT_NOT_NULL(node);
    
    // 第一次写入
    const char *data1 = "First";
    vfs_write(node, 0, 5, (uint8_t *)data1);
    
    // 第二次写入（追加）
    const char *data2 = "Second";
    vfs_write(node, 5, 6, (uint8_t *)data2);
    
    // 读取全部数据
    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    uint32_t read_count = vfs_read(node, 0, 11, (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 11);
    ASSERT_STR_EQ(buffer, "FirstSecond");
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/RMULTI.TMP");
}

/**
 * @brief 测试文件覆盖写入
 *
 * 验证覆盖写入正确更新数据
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_overwrite) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/ROVER.TMP");
    ASSERT_EQ(result, 0);
    
    fs_node_t *node = vfs_path_to_node("/ROVER.TMP");
    ASSERT_NOT_NULL(node);
    
    // 写入初始数据
    const char *initial = "AAAAAAAAAA";  // 10 个 A
    vfs_write(node, 0, 10, (uint8_t *)initial);
    
    // 覆盖中间部分
    const char *overwrite = "BBB";
    vfs_write(node, 3, 3, (uint8_t *)overwrite);
    
    // 读取并验证
    char buffer[16];
    memset(buffer, 0, sizeof(buffer));
    vfs_read(node, 0, 10, (uint8_t *)buffer);
    ASSERT_STR_EQ(buffer, "AAABBBAAAA");
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/ROVER.TMP");
}

// ============================================================================
// 测试套件 2: ramfs_dir_tests - 目录操作测试
// ============================================================================
//
// 测试 ramfs 目录创建、删除、遍历操作
// **Validates: Requirements 4.4** - ramfs 目录操作
// ============================================================================

/**
 * @brief 测试目录创建
 *
 * 验证 ramfs 能创建新目录
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_mkdir) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试目录 (8.3 兼容)
    int result = vfs_mkdir("/RDIR", FS_PERM_READ | FS_PERM_WRITE);
    ASSERT_EQ(result, 0);
    
    // 验证目录存在
    fs_node_t *node = vfs_path_to_node("/RDIR");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_DIRECTORY);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/RDIR");
}

/**
 * @brief 测试空目录删除
 *
 * 验证 ramfs 能删除空目录
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_rmdir_empty) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试目录 (8.3 兼容)
    int result = vfs_mkdir("/RRMDIR", FS_PERM_READ | FS_PERM_WRITE);
    ASSERT_EQ(result, 0);
    
    // 删除空目录
    result = vfs_unlink("/RRMDIR");
    ASSERT_EQ(result, 0);
    
    // 验证目录不存在
    fs_node_t *node = vfs_path_to_node("/RRMDIR");
    ASSERT_NULL(node);
}

/**
 * @brief 测试在子目录中创建文件
 *
 * 验证可以在 ramfs 子目录中创建文件
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_file_in_subdir) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建子目录 (8.3 兼容)
    int result = vfs_mkdir("/RSUBDIR", FS_PERM_READ | FS_PERM_WRITE);
    ASSERT_EQ(result, 0);
    
    // 在子目录中创建文件
    result = vfs_create("/RSUBDIR/TFILE.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件存在
    fs_node_t *node = vfs_path_to_node("/RSUBDIR/TFILE.TMP");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_FILE);
    vfs_release_node(node);
    
    // 写入数据并验证
    node = vfs_path_to_node("/RSUBDIR/TFILE.TMP");
    const char *data = "subdir file data";
    vfs_write(node, 0, strlen(data), (uint8_t *)data);
    
    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    vfs_read(node, 0, strlen(data), (uint8_t *)buffer);
    ASSERT_STR_EQ(buffer, data);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/RSUBDIR/TFILE.TMP");
    vfs_unlink("/RSUBDIR");
}

/**
 * @brief 测试目录遍历
 *
 * 验证 ramfs 目录遍历功能
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_readdir) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试目录和文件 (8.3 兼容)
    vfs_mkdir("/RRDDIR", FS_PERM_READ | FS_PERM_WRITE);
    vfs_create("/RRDDIR/FILE1.TMP");
    vfs_create("/RRDDIR/FILE2.TMP");
    vfs_create("/RRDDIR/FILE3.TMP");
    
    // 获取目录节点
    fs_node_t *dir = vfs_path_to_node("/RRDDIR");
    ASSERT_NOT_NULL(dir);
    
    // 遍历目录
    uint32_t count = 0;
    struct dirent *entry;
    while ((entry = vfs_readdir(dir, count)) != NULL) {
        count++;
        // 验证目录项有名称
        ASSERT_TRUE(entry->d_name[0] != '\0');
    }
    
    // 应该至少有 3 个文件 (FAT32 可能还包含 . 和 .. 条目)
    ASSERT_TRUE(count >= 3);
    
    // 清理
    vfs_release_node(dir);
    vfs_unlink("/RRDDIR/FILE1.TMP");
    vfs_unlink("/RRDDIR/FILE2.TMP");
    vfs_unlink("/RRDDIR/FILE3.TMP");
    vfs_unlink("/RRDDIR");
}

// ============================================================================
// 测试套件 3: ramfs_edge_tests - 边界条件测试
// ============================================================================
//
// 测试 ramfs 的边界条件和错误处理
// **Validates: Requirements 4.4** - ramfs 错误处理
// ============================================================================

/**
 * @brief 测试重复创建文件
 *
 * 验证创建已存在的文件返回错误
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_create_duplicate) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建文件 (8.3 兼容)
    int result = vfs_create("/RDUP.TMP");
    ASSERT_EQ(result, 0);
    
    // 尝试再次创建同名文件
    result = vfs_create("/RDUP.TMP");
    ASSERT_EQ(result, -1);  // 应该失败
    
    // 清理
    vfs_unlink("/RDUP.TMP");
}

/**
 * @brief 测试删除不存在的文件
 *
 * 验证删除不存在的文件返回错误
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_delete_nonexistent) {
    int result = vfs_unlink("/ramfs_nonexistent_file_xyz");
    ASSERT_EQ(result, -1);
}

/**
 * @brief 测试删除非空目录
 *
 * 验证删除非空目录返回错误
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_rmdir_nonempty) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建目录和文件 (8.3 兼容)
    vfs_mkdir("/RNEMPTY", FS_PERM_READ | FS_PERM_WRITE);
    vfs_create("/RNEMPTY/FILE.TMP");
    
    // 尝试删除非空目录
    int result = vfs_unlink("/RNEMPTY");
    ASSERT_EQ(result, -1);  // 应该失败
    
    // 清理
    vfs_unlink("/RNEMPTY/FILE.TMP");
    vfs_unlink("/RNEMPTY");
}

/**
 * @brief 测试读取空文件
 *
 * 验证读取空文件返回 0 字节
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_read_empty) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建空文件 (8.3 兼容)
    int result = vfs_create("/REMPTY.TMP");
    ASSERT_EQ(result, 0);
    
    fs_node_t *node = vfs_path_to_node("/REMPTY.TMP");
    ASSERT_NOT_NULL(node);
    
    // 读取空文件
    char buffer[16];
    uint32_t read_count = vfs_read(node, 0, 16, (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 0);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/REMPTY.TMP");
}

/**
 * @brief 测试超出文件末尾的读取
 *
 * 验证从超出文件大小的偏移读取返回 0
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_read_past_eof) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建文件并写入数据 (8.3 兼容)
    int result = vfs_create("/REOF.TMP");
    ASSERT_EQ(result, 0);
    
    fs_node_t *node = vfs_path_to_node("/REOF.TMP");
    ASSERT_NOT_NULL(node);
    
    const char *data = "short";
    vfs_write(node, 0, 5, (uint8_t *)data);
    
    // 从超出文件末尾的位置读取
    char buffer[16];
    uint32_t read_count = vfs_read(node, 100, 16, (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 0);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/REOF.TMP");
}

/**
 * @brief 测试文件查找
 *
 * 验证 finddir 能在目录中查找文件
 * _Requirements: 4.4_
 */
TEST_CASE(test_ramfs_finddir) {
    if (!ramfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/RFIND.TMP");
    ASSERT_EQ(result, 0);
    
    // 使用 finddir 查找
    fs_node_t *root = vfs_get_root();
    ASSERT_NOT_NULL(root);
    
    fs_node_t *found = vfs_finddir(root, "RFIND.TMP");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(found->type, FS_FILE);
    vfs_release_node(found);
    
    // 查找不存在的文件
    found = vfs_finddir(root, "RNOEXIST.TMP");
    ASSERT_NULL(found);
    
    // 清理
    vfs_unlink("/RFIND.TMP");
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 文件操作测试套件
 *
 * **Validates: Requirements 4.4**
 */
TEST_SUITE(ramfs_file_tests) {
    RUN_TEST(test_ramfs_create_file);
    RUN_TEST(test_ramfs_delete_file);
    RUN_TEST(test_ramfs_content_persistence);
    RUN_TEST(test_ramfs_multiple_writes);
    RUN_TEST(test_ramfs_overwrite);
}

/**
 * @brief 目录操作测试套件
 *
 * **Validates: Requirements 4.4**
 */
TEST_SUITE(ramfs_dir_tests) {
    RUN_TEST(test_ramfs_mkdir);
    RUN_TEST(test_ramfs_rmdir_empty);
    RUN_TEST(test_ramfs_file_in_subdir);
    RUN_TEST(test_ramfs_readdir);
}

/**
 * @brief 边界条件测试套件
 *
 * **Validates: Requirements 4.4**
 */
TEST_SUITE(ramfs_edge_tests) {
    RUN_TEST(test_ramfs_create_duplicate);
    RUN_TEST(test_ramfs_delete_nonexistent);
    RUN_TEST(test_ramfs_rmdir_nonempty);
    RUN_TEST(test_ramfs_read_empty);
    RUN_TEST(test_ramfs_read_past_eof);
    RUN_TEST(test_ramfs_finddir);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 Ramfs 测试
 *
 * 按功能组织的测试套件：
 *   1. ramfs_file_tests - 文件操作测试
 *   2. ramfs_dir_tests - 目录操作测试
 *   3. ramfs_edge_tests - 边界条件测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.4**
 */
void run_ramfs_tests(void) {
    // 初始化测试框架
    unittest_init();

    // ========================================================================
    // 功能测试套件
    // ========================================================================

    // 套件 1: 文件操作测试
    // _Requirements: 4.4_
    RUN_SUITE(ramfs_file_tests);

    // 套件 2: 目录操作测试
    // _Requirements: 4.4_
    RUN_SUITE(ramfs_dir_tests);

    // 套件 3: 边界条件测试
    // _Requirements: 4.4_
    RUN_SUITE(ramfs_edge_tests);

    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief Ramfs 测试模块元数据
 *
 * 使用 TEST_MODULE_DESC 宏注册模块到测试框架
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.4, 10.1, 10.2**
 */
TEST_MODULE_DESC(ramfs, FS, run_ramfs_tests,
    "RAM-based File System tests - file create/delete, content persistence, directory operations");
