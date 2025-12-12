// ============================================================================
// vfs_test.c - VFS 单元测试
// ============================================================================
//
// 模块名称: vfs
// 子系统: fs (文件系统)
// 描述: 测试 VFS (Virtual File System) 的功能
//
// 功能覆盖:
//   - 文件打开、关闭 (vfs_open, vfs_close)
//   - 文件读写 (vfs_read, vfs_write)
//   - 目录操作 (vfs_readdir, vfs_finddir)
//   - 路径解析 (vfs_path_to_node)
//   - 文件创建和删除 (vfs_create, vfs_unlink)
//   - 目录创建 (vfs_mkdir)
//
// **Feature: test-refactor**
// **Validates: Requirements 4.1, 4.2**
// ============================================================================

#include <tests/ktest.h>
#include <tests/fs/vfs_test.h>
#include <tests/test_module.h>
#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * @brief 设置测试环境 - 初始化 ramfs 作为根文件系统
 *
 * 注意: 这个函数假设 VFS 和 ramfs 已经在内核启动时初始化
 * 如果根文件系统未设置，测试将跳过
 */
static bool vfs_test_setup(void) {
    fs_node_t *root = vfs_get_root();
    if (!root) {
        return false;
    }
    return true;
}

// ============================================================================
// 测试套件 1: vfs_basic_tests - 基本 VFS 操作测试
// ============================================================================
//
// 测试 VFS 的基本功能
// **Validates: Requirements 4.1** - VFS 打开文件返回有效文件描述符
// ============================================================================

/**
 * @brief 测试获取根文件系统
 *
 * 验证 vfs_get_root() 返回有效的根节点
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_get_root) {
    fs_node_t *root = vfs_get_root();
    ASSERT_NOT_NULL(root);
    ASSERT_EQ(root->type, FS_DIRECTORY);
}

/**
 * @brief 测试根目录路径解析
 *
 * 验证 vfs_path_to_node("/") 返回根节点
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_path_to_root) {
    fs_node_t *root = vfs_get_root();
    ASSERT_NOT_NULL(root);
    
    fs_node_t *node = vfs_path_to_node("/");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ_PTR(node, root);
}

/**
 * @brief 测试空路径解析
 *
 * 验证 vfs_path_to_node(NULL) 返回 NULL
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_path_null) {
    fs_node_t *node = vfs_path_to_node(NULL);
    ASSERT_NULL(node);
}

/**
 * @brief 测试不存在的路径
 *
 * 验证不存在的路径返回 NULL
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_path_not_found) {
    fs_node_t *node = vfs_path_to_node("/nonexistent_path_12345");
    ASSERT_NULL(node);
}

// ============================================================================
// 测试套件 2: vfs_file_tests - 文件操作测试
// ============================================================================
//
// 测试文件创建、读写、删除操作
// **Validates: Requirements 4.1, 4.2** - VFS 文件操作和数据完整性
// ============================================================================

/**
 * @brief 测试文件创建
 *
 * 验证 vfs_create() 能创建新文件
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_create_file) {
    if (!vfs_test_setup()) {
        return;  // 跳过测试
    }
    
    // 创建测试文件 (使用 8.3 兼容的短文件名以支持 FAT32)
    int result = vfs_create("/TCREAT.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件存在
    fs_node_t *node = vfs_path_to_node("/TCREAT.TMP");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_FILE);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/TCREAT.TMP");
}

/**
 * @brief 测试文件写入和读取
 *
 * 验证写入的数据可以正确读取回来
 * **Feature: test-refactor, Property 9: VFS Read-Write Round-Trip**
 * **Validates: Requirements 4.2**
 */
TEST_CASE(test_vfs_read_write) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/TRW.TMP");
    ASSERT_EQ(result, 0);
    
    // 获取文件节点
    fs_node_t *node = vfs_path_to_node("/TRW.TMP");
    ASSERT_NOT_NULL(node);
    
    // 写入测试数据
    const char *test_data = "Hello, VFS!";
    uint32_t data_len = strlen(test_data);
    uint32_t written = vfs_write(node, 0, data_len, (uint8_t *)test_data);
    ASSERT_EQ_U(written, data_len);
    
    // 读取数据
    char read_buffer[64];
    memset(read_buffer, 0, sizeof(read_buffer));
    uint32_t read_count = vfs_read(node, 0, data_len, (uint8_t *)read_buffer);
    ASSERT_EQ_U(read_count, data_len);
    
    // 验证数据完整性 (Round-Trip)
    ASSERT_STR_EQ(test_data, read_buffer);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/TRW.TMP");
}

/**
 * @brief 测试文件偏移读写
 *
 * 验证从不同偏移位置读写数据
 * _Requirements: 4.2_
 */
TEST_CASE(test_vfs_read_write_offset) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/TOFFSET.TMP");
    ASSERT_EQ(result, 0);
    
    fs_node_t *node = vfs_path_to_node("/TOFFSET.TMP");
    ASSERT_NOT_NULL(node);
    
    // 写入数据到偏移 0
    const char *data1 = "AAAA";
    vfs_write(node, 0, 4, (uint8_t *)data1);
    
    // 写入数据到偏移 4
    const char *data2 = "BBBB";
    vfs_write(node, 4, 4, (uint8_t *)data2);
    
    // 从偏移 0 读取
    char buffer[16];
    memset(buffer, 0, sizeof(buffer));
    uint32_t read_count = vfs_read(node, 0, 8, (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 8);
    ASSERT_STR_EQ(buffer, "AAAABBBB");
    
    // 从偏移 4 读取
    memset(buffer, 0, sizeof(buffer));
    read_count = vfs_read(node, 4, 4, (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 4);
    ASSERT_STR_EQ(buffer, "BBBB");
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/TOFFSET.TMP");
}

/**
 * @brief 测试文件删除
 *
 * 验证 vfs_unlink() 能删除文件
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_unlink_file) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/TUNLINK.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件存在
    fs_node_t *node = vfs_path_to_node("/TUNLINK.TMP");
    ASSERT_NOT_NULL(node);
    vfs_release_node(node);
    
    // 删除文件
    result = vfs_unlink("/TUNLINK.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件不存在
    node = vfs_path_to_node("/TUNLINK.TMP");
    ASSERT_NULL(node);
}

/**
 * @brief 测试读取空文件
 *
 * 验证读取空文件返回 0 字节
 * _Requirements: 4.2_
 */
TEST_CASE(test_vfs_read_empty_file) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建空文件 (8.3 兼容)
    int result = vfs_create("/TEMPTY.TMP");
    ASSERT_EQ(result, 0);
    
    fs_node_t *node = vfs_path_to_node("/TEMPTY.TMP");
    ASSERT_NOT_NULL(node);
    
    // 读取空文件
    char buffer[16];
    uint32_t read_count = vfs_read(node, 0, 16, (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 0);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/TEMPTY.TMP");
}

// ============================================================================
// 测试套件 3: vfs_dir_tests - 目录操作测试
// ============================================================================
//
// 测试目录创建、读取、查找操作
// **Validates: Requirements 4.1** - VFS 目录操作
// ============================================================================

/**
 * @brief 测试目录创建
 *
 * 验证 vfs_mkdir() 能创建新目录
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_mkdir) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建测试目录 (8.3 兼容)
    int result = vfs_mkdir("/TDIR", FS_PERM_READ | FS_PERM_WRITE);
    ASSERT_EQ(result, 0);
    
    // 验证目录存在
    fs_node_t *node = vfs_path_to_node("/TDIR");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_DIRECTORY);
    
    // 清理
    vfs_release_node(node);
    vfs_unlink("/TDIR");
}

/**
 * @brief 测试在子目录中创建文件
 *
 * 验证可以在子目录中创建文件
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_create_in_subdir) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建子目录 (8.3 兼容)
    int result = vfs_mkdir("/TSUBDIR", FS_PERM_READ | FS_PERM_WRITE);
    ASSERT_EQ(result, 0);
    
    // 在子目录中创建文件
    result = vfs_create("/TSUBDIR/SUBFILE.TMP");
    ASSERT_EQ(result, 0);
    
    // 验证文件存在
    fs_node_t *node = vfs_path_to_node("/TSUBDIR/SUBFILE.TMP");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_FILE);
    vfs_release_node(node);
    
    // 清理
    vfs_unlink("/TSUBDIR/SUBFILE.TMP");
    vfs_unlink("/TSUBDIR");
}

/**
 * @brief 测试目录查找
 *
 * 验证 vfs_finddir() 能在目录中查找文件
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_finddir) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建测试文件 (8.3 兼容)
    int result = vfs_create("/TFIND.TMP");
    ASSERT_EQ(result, 0);
    
    // 使用 finddir 查找
    fs_node_t *root = vfs_get_root();
    ASSERT_NOT_NULL(root);
    
    fs_node_t *found = vfs_finddir(root, "TFIND.TMP");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(found->type, FS_FILE);
    vfs_release_node(found);
    
    // 查找不存在的文件
    found = vfs_finddir(root, "NOEXIST.TMP");
    ASSERT_NULL(found);
    
    // 清理
    vfs_unlink("/TFIND.TMP");
}

/**
 * @brief 测试目录读取
 *
 * 验证 vfs_readdir() 能读取目录项
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_readdir) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建测试目录和文件 (8.3 兼容)
    vfs_mkdir("/TRDDIR", FS_PERM_READ | FS_PERM_WRITE);
    vfs_create("/TRDDIR/FILE1.TMP");
    vfs_create("/TRDDIR/FILE2.TMP");
    
    // 获取目录节点
    fs_node_t *dir = vfs_path_to_node("/TRDDIR");
    ASSERT_NOT_NULL(dir);
    
    // 读取目录项
    uint32_t count = 0;
    struct dirent *entry;
    while ((entry = vfs_readdir(dir, count)) != NULL) {
        count++;
        // 验证目录项有名称
        ASSERT_TRUE(entry->d_name[0] != '\0');
    }
    
    // 应该至少有 2 个文件
    ASSERT_TRUE(count >= 2);
    
    // 清理
    vfs_release_node(dir);
    vfs_unlink("/TRDDIR/FILE1.TMP");
    vfs_unlink("/TRDDIR/FILE2.TMP");
    vfs_unlink("/TRDDIR");
}

// ============================================================================
// 测试套件 4: vfs_edge_tests - 边界条件测试
// ============================================================================
//
// 测试 VFS 的边界条件和错误处理
// **Validates: Requirements 4.1** - VFS 错误处理
// ============================================================================

/**
 * @brief 测试 NULL 节点操作
 *
 * 验证对 NULL 节点的操作是安全的
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_null_node_operations) {
    // 这些操作不应该崩溃
    vfs_open(NULL, 0);
    vfs_close(NULL);
    
    uint32_t read_result = vfs_read(NULL, 0, 10, NULL);
    ASSERT_EQ_U(read_result, 0);
    
    uint32_t write_result = vfs_write(NULL, 0, 10, NULL);
    ASSERT_EQ_U(write_result, 0);
    
    struct dirent *entry = vfs_readdir(NULL, 0);
    ASSERT_NULL(entry);
    
    fs_node_t *found = vfs_finddir(NULL, "test");
    ASSERT_NULL(found);
}

/**
 * @brief 测试重复创建文件
 *
 * 验证创建已存在的文件返回错误
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_create_duplicate) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建文件 (8.3 兼容)
    int result = vfs_create("/TDUP.TMP");
    ASSERT_EQ(result, 0);
    
    // 尝试再次创建同名文件
    result = vfs_create("/TDUP.TMP");
    ASSERT_EQ(result, -1);  // 应该失败
    
    // 清理
    vfs_unlink("/TDUP.TMP");
}

/**
 * @brief 测试删除不存在的文件
 *
 * 验证删除不存在的文件返回错误
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_unlink_nonexistent) {
    int result = vfs_unlink("/nonexistent_file_to_delete");
    ASSERT_EQ(result, -1);
}

/**
 * @brief 测试删除非空目录
 *
 * 验证删除非空目录返回错误
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_unlink_nonempty_dir) {
    if (!vfs_test_setup()) {
        return;
    }
    
    // 创建目录和文件 (8.3 兼容)
    vfs_mkdir("/TNEMPTY", FS_PERM_READ | FS_PERM_WRITE);
    vfs_create("/TNEMPTY/FILE.TMP");
    
    // 尝试删除非空目录
    int result = vfs_unlink("/TNEMPTY");
    ASSERT_EQ(result, -1);  // 应该失败
    
    // 清理
    vfs_unlink("/TNEMPTY/FILE.TMP");
    vfs_unlink("/TNEMPTY");
}

/**
 * @brief 测试特殊目录条目 '.'
 *
 * 验证 '.' 返回当前目录
 * _Requirements: 4.1_
 */
TEST_CASE(test_vfs_dot_entry) {
    if (!vfs_test_setup()) {
        return;
    }
    
    fs_node_t *root = vfs_get_root();
    ASSERT_NOT_NULL(root);
    
    // '.' 应该返回当前目录
    fs_node_t *dot = vfs_finddir(root, ".");
    ASSERT_NOT_NULL(dot);
    ASSERT_EQ_PTR(dot, root);
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 基本 VFS 操作测试套件
 *
 * **Validates: Requirements 4.1**
 */
TEST_SUITE(vfs_basic_tests) {
    RUN_TEST(test_vfs_get_root);
    RUN_TEST(test_vfs_path_to_root);
    RUN_TEST(test_vfs_path_null);
    RUN_TEST(test_vfs_path_not_found);
}

/**
 * @brief 文件操作测试套件
 *
 * **Validates: Requirements 4.1, 4.2**
 */
TEST_SUITE(vfs_file_tests) {
    RUN_TEST(test_vfs_create_file);
    RUN_TEST(test_vfs_read_write);
    RUN_TEST(test_vfs_read_write_offset);
    RUN_TEST(test_vfs_unlink_file);
    RUN_TEST(test_vfs_read_empty_file);
}

/**
 * @brief 目录操作测试套件
 *
 * **Validates: Requirements 4.1**
 */
TEST_SUITE(vfs_dir_tests) {
    RUN_TEST(test_vfs_mkdir);
    RUN_TEST(test_vfs_create_in_subdir);
    RUN_TEST(test_vfs_finddir);
    RUN_TEST(test_vfs_readdir);
}

/**
 * @brief 边界条件测试套件
 *
 * **Validates: Requirements 4.1**
 */
TEST_SUITE(vfs_edge_tests) {
    RUN_TEST(test_vfs_null_node_operations);
    RUN_TEST(test_vfs_create_duplicate);
    RUN_TEST(test_vfs_unlink_nonexistent);
    RUN_TEST(test_vfs_unlink_nonempty_dir);
    RUN_TEST(test_vfs_dot_entry);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 VFS 测试
 *
 * 按功能组织的测试套件：
 *   1. vfs_basic_tests - 基本 VFS 操作测试
 *   2. vfs_file_tests - 文件操作测试
 *   3. vfs_dir_tests - 目录操作测试
 *   4. vfs_edge_tests - 边界条件测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.1, 4.2**
 */
void run_vfs_tests(void) {
    // 初始化测试框架
    unittest_init();

    // ========================================================================
    // 功能测试套件
    // ========================================================================

    // 套件 1: 基本 VFS 操作测试
    // _Requirements: 4.1_
    RUN_SUITE(vfs_basic_tests);

    // 套件 2: 文件操作测试
    // _Requirements: 4.1, 4.2_
    RUN_SUITE(vfs_file_tests);

    // 套件 3: 目录操作测试
    // _Requirements: 4.1_
    RUN_SUITE(vfs_dir_tests);

    // 套件 4: 边界条件测试
    // _Requirements: 4.1_
    RUN_SUITE(vfs_edge_tests);

    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief VFS 测试模块元数据
 *
 * 使用 TEST_MODULE_DESC 宏注册模块到测试框架
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.1, 4.2, 10.1, 10.2**
 */
TEST_MODULE_DESC(vfs, FS, run_vfs_tests,
    "Virtual File System tests - open, close, read, write, directory operations");
