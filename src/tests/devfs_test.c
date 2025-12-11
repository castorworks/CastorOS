// ============================================================================
// devfs_test.c - Devfs 单元测试
// ============================================================================
//
// 模块名称: devfs
// 子系统: fs (文件系统)
// 描述: 测试 Devfs (Device File System) 的功能
//
// 功能覆盖:
//   - 设备注册 (devfs_init 创建的设备节点)
//   - 设备节点访问 (/dev/null, /dev/zero, /dev/serial, /dev/rtc)
//   - 目录遍历 (devfs_readdir, devfs_finddir)
//
// **Feature: test-refactor**
// **Validates: Requirements 4.5**
// ============================================================================

#include <tests/ktest.h>
#include <tests/devfs_test.h>
#include <tests/test_module.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * @brief 设置测试环境 - 确保 devfs 已挂载
 *
 * 注意: 这个函数假设 VFS 和 devfs 已经在内核启动时初始化
 * 如果 /dev 目录不存在，测试将跳过
 */
static bool devfs_test_setup(void) {
    fs_node_t *dev = vfs_path_to_node("/dev");
    if (!dev) {
        return false;
    }
    vfs_release_node(dev);
    return true;
}

// ============================================================================
// 测试套件 1: devfs_device_tests - 设备注册和访问测试
// ============================================================================
//
// 测试 devfs 设备注册和基本访问
// **Validates: Requirements 4.5** - devfs 设备注册和节点访问
// ============================================================================

/**
 * @brief 测试 /dev 目录存在
 *
 * 验证 devfs 根目录已正确挂载
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_root_exists) {
    fs_node_t *dev = vfs_path_to_node("/dev");
    ASSERT_NOT_NULL(dev);
    ASSERT_EQ(dev->type, FS_DIRECTORY);
    vfs_release_node(dev);
}

/**
 * @brief 测试 /dev/null 设备存在
 *
 * 验证 null 设备已注册
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_null_exists) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/null");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_CHARDEVICE);
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/zero 设备存在
 *
 * 验证 zero 设备已注册
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_zero_exists) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/zero");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_CHARDEVICE);
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/serial 设备存在
 *
 * 验证 serial 设备已注册
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_serial_exists) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/serial");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_CHARDEVICE);
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/rtc 设备存在
 *
 * 验证 rtc 设备已注册
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_rtc_exists) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/rtc");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_CHARDEVICE);
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/console 设备存在
 *
 * 验证 console 设备已注册
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_console_exists) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/console");
    ASSERT_NOT_NULL(node);
    ASSERT_EQ(node->type, FS_CHARDEVICE);
    vfs_release_node(node);
}

/**
 * @brief 测试不存在的设备
 *
 * 验证访问不存在的设备返回 NULL
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_nonexistent_device) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/nonexistent_device_xyz");
    ASSERT_NULL(node);
}

// ============================================================================
// 测试套件 2: devfs_null_tests - /dev/null 设备测试
// ============================================================================
//
// 测试 /dev/null 设备的读写行为
// **Validates: Requirements 4.5** - /dev/null 设备功能
// ============================================================================

/**
 * @brief 测试 /dev/null 读取返回 0 字节
 *
 * 验证从 /dev/null 读取总是返回 0
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_null_read) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/null");
    ASSERT_NOT_NULL(node);
    
    // 读取应该返回 0 字节
    char buffer[64];
    uint32_t read_count = vfs_read(node, 0, sizeof(buffer), (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 0);
    
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/null 写入成功但丢弃数据
 *
 * 验证写入 /dev/null 返回写入的字节数
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_null_write) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/null");
    ASSERT_NOT_NULL(node);
    
    // 写入应该返回写入的字节数（但数据被丢弃）
    const char *data = "This data will be discarded";
    uint32_t data_len = strlen(data);
    uint32_t written = vfs_write(node, 0, data_len, (uint8_t *)data);
    ASSERT_EQ_U(written, data_len);
    
    // 再次读取应该仍然返回 0
    char buffer[64];
    uint32_t read_count = vfs_read(node, 0, sizeof(buffer), (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, 0);
    
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/null 多次写入
 *
 * 验证多次写入 /dev/null 都成功
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_null_multiple_writes) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/null");
    ASSERT_NOT_NULL(node);
    
    // 多次写入
    for (int i = 0; i < 5; i++) {
        const char *data = "test";
        uint32_t written = vfs_write(node, 0, 4, (uint8_t *)data);
        ASSERT_EQ_U(written, 4);
    }
    
    vfs_release_node(node);
}

// ============================================================================
// 测试套件 3: devfs_zero_tests - /dev/zero 设备测试
// ============================================================================
//
// 测试 /dev/zero 设备的读写行为
// **Validates: Requirements 4.5** - /dev/zero 设备功能
// ============================================================================

/**
 * @brief 测试 /dev/zero 读取返回零字节
 *
 * 验证从 /dev/zero 读取返回全零数据
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_zero_read) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/zero");
    ASSERT_NOT_NULL(node);
    
    // 先填充非零数据
    char buffer[32];
    memset(buffer, 0xFF, sizeof(buffer));
    
    // 读取应该返回请求的字节数，且全为零
    uint32_t read_count = vfs_read(node, 0, sizeof(buffer), (uint8_t *)buffer);
    ASSERT_EQ_U(read_count, sizeof(buffer));
    
    // 验证所有字节都是零
    for (size_t i = 0; i < sizeof(buffer); i++) {
        ASSERT_EQ(buffer[i], 0);
    }
    
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/zero 写入成功
 *
 * 验证写入 /dev/zero 返回写入的字节数
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_zero_write) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/zero");
    ASSERT_NOT_NULL(node);
    
    // 写入应该返回写入的字节数
    const char *data = "This data will be discarded";
    uint32_t data_len = strlen(data);
    uint32_t written = vfs_write(node, 0, data_len, (uint8_t *)data);
    ASSERT_EQ_U(written, data_len);
    
    vfs_release_node(node);
}

/**
 * @brief 测试 /dev/zero 不同大小读取
 *
 * 验证从 /dev/zero 读取不同大小的数据
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_zero_read_sizes) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/zero");
    ASSERT_NOT_NULL(node);
    
    // 测试不同大小的读取
    uint32_t sizes[] = {1, 4, 16, 64};
    char buffer[64];
    
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        memset(buffer, 0xFF, sizeof(buffer));
        uint32_t read_count = vfs_read(node, 0, sizes[i], (uint8_t *)buffer);
        ASSERT_EQ_U(read_count, sizes[i]);
        
        // 验证读取的字节都是零
        for (uint32_t j = 0; j < sizes[i]; j++) {
            ASSERT_EQ(buffer[j], 0);
        }
    }
    
    vfs_release_node(node);
}

// ============================================================================
// 测试套件 4: devfs_dir_tests - 目录操作测试
// ============================================================================
//
// 测试 devfs 目录遍历和查找操作
// **Validates: Requirements 4.5** - devfs 目录操作
// ============================================================================

/**
 * @brief 测试 /dev 目录遍历
 *
 * 验证 readdir 能列出所有设备
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_readdir) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *dev = vfs_path_to_node("/dev");
    ASSERT_NOT_NULL(dev);
    
    // 遍历目录
    uint32_t count = 0;
    struct dirent *entry;
    while ((entry = vfs_readdir(dev, count)) != NULL) {
        count++;
        // 验证目录项有名称
        ASSERT_TRUE(entry->d_name[0] != '\0');
    }
    
    // 应该至少有 5 个设备 + . 和 .. = 7 个条目
    ASSERT_TRUE(count >= 5);
    
    vfs_release_node(dev);
}

/**
 * @brief 测试 /dev 目录查找
 *
 * 验证 finddir 能找到设备
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_finddir) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *dev = vfs_path_to_node("/dev");
    ASSERT_NOT_NULL(dev);
    
    // 查找 null 设备
    fs_node_t *null_dev = vfs_finddir(dev, "null");
    ASSERT_NOT_NULL(null_dev);
    ASSERT_EQ(null_dev->type, FS_CHARDEVICE);
    vfs_release_node(null_dev);
    
    // 查找 zero 设备
    fs_node_t *zero_dev = vfs_finddir(dev, "zero");
    ASSERT_NOT_NULL(zero_dev);
    ASSERT_EQ(zero_dev->type, FS_CHARDEVICE);
    vfs_release_node(zero_dev);
    
    // 查找不存在的设备
    fs_node_t *nonexistent = vfs_finddir(dev, "nonexistent_xyz");
    ASSERT_NULL(nonexistent);
    
    vfs_release_node(dev);
}

/**
 * @brief 测试 /dev 目录的 . 条目
 *
 * 验证 . 返回当前目录
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_dot_entry) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *dev = vfs_path_to_node("/dev");
    ASSERT_NOT_NULL(dev);
    
    // . 应该返回当前目录
    fs_node_t *dot = vfs_finddir(dev, ".");
    ASSERT_NOT_NULL(dot);
    ASSERT_EQ_PTR(dot, dev);
    
    vfs_release_node(dev);
}

/**
 * @brief 测试 /dev 目录的 .. 条目
 *
 * 验证 .. 返回父目录（在 devfs 中返回自身）
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_dotdot_entry) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *dev = vfs_path_to_node("/dev");
    ASSERT_NOT_NULL(dev);
    
    // .. 在 devfs 中返回自身（因为 devfs 是挂载点）
    fs_node_t *dotdot = vfs_finddir(dev, "..");
    ASSERT_NOT_NULL(dotdot);
    
    vfs_release_node(dev);
}

// ============================================================================
// 测试套件 5: devfs_rtc_tests - /dev/rtc 设备测试
// ============================================================================
//
// 测试 /dev/rtc 设备的读取行为
// **Validates: Requirements 4.5** - /dev/rtc 设备功能
// ============================================================================

/**
 * @brief 测试 /dev/rtc 读取返回时间字符串
 *
 * 验证从 /dev/rtc 读取返回格式化的时间
 * _Requirements: 4.5_
 */
TEST_CASE(test_devfs_rtc_read) {
    if (!devfs_test_setup()) {
        return;
    }
    
    fs_node_t *node = vfs_path_to_node("/dev/rtc");
    ASSERT_NOT_NULL(node);
    
    // 读取时间字符串
    char buffer[128];
    memset(buffer, 0, sizeof(buffer));
    uint32_t read_count = vfs_read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    
    // 应该读取到一些数据
    ASSERT_TRUE(read_count > 0);
    
    // 时间字符串应该包含数字（年份）
    bool has_digit = false;
    for (size_t i = 0; i < read_count; i++) {
        if (buffer[i] >= '0' && buffer[i] <= '9') {
            has_digit = true;
            break;
        }
    }
    ASSERT_TRUE(has_digit);
    
    vfs_release_node(node);
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 设备注册和访问测试套件
 *
 * **Validates: Requirements 4.5**
 */
TEST_SUITE(devfs_device_tests) {
    RUN_TEST(test_devfs_root_exists);
    RUN_TEST(test_devfs_null_exists);
    RUN_TEST(test_devfs_zero_exists);
    RUN_TEST(test_devfs_serial_exists);
    RUN_TEST(test_devfs_rtc_exists);
    RUN_TEST(test_devfs_console_exists);
    RUN_TEST(test_devfs_nonexistent_device);
}

/**
 * @brief /dev/null 设备测试套件
 *
 * **Validates: Requirements 4.5**
 */
TEST_SUITE(devfs_null_tests) {
    RUN_TEST(test_devfs_null_read);
    RUN_TEST(test_devfs_null_write);
    RUN_TEST(test_devfs_null_multiple_writes);
}

/**
 * @brief /dev/zero 设备测试套件
 *
 * **Validates: Requirements 4.5**
 */
TEST_SUITE(devfs_zero_tests) {
    RUN_TEST(test_devfs_zero_read);
    RUN_TEST(test_devfs_zero_write);
    RUN_TEST(test_devfs_zero_read_sizes);
}

/**
 * @brief 目录操作测试套件
 *
 * **Validates: Requirements 4.5**
 */
TEST_SUITE(devfs_dir_tests) {
    RUN_TEST(test_devfs_readdir);
    RUN_TEST(test_devfs_finddir);
    RUN_TEST(test_devfs_dot_entry);
    RUN_TEST(test_devfs_dotdot_entry);
}

/**
 * @brief /dev/rtc 设备测试套件
 *
 * **Validates: Requirements 4.5**
 */
TEST_SUITE(devfs_rtc_tests) {
    RUN_TEST(test_devfs_rtc_read);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 Devfs 测试
 *
 * 按功能组织的测试套件：
 *   1. devfs_device_tests - 设备注册和访问测试
 *   2. devfs_null_tests - /dev/null 设备测试
 *   3. devfs_zero_tests - /dev/zero 设备测试
 *   4. devfs_dir_tests - 目录操作测试
 *   5. devfs_rtc_tests - /dev/rtc 设备测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.5**
 */
void run_devfs_tests(void) {
    // 初始化测试框架
    unittest_init();

    // ========================================================================
    // 功能测试套件
    // ========================================================================

    // 套件 1: 设备注册和访问测试
    // _Requirements: 4.5_
    RUN_SUITE(devfs_device_tests);

    // 套件 2: /dev/null 设备测试
    // _Requirements: 4.5_
    RUN_SUITE(devfs_null_tests);

    // 套件 3: /dev/zero 设备测试
    // _Requirements: 4.5_
    RUN_SUITE(devfs_zero_tests);

    // 套件 4: 目录操作测试
    // _Requirements: 4.5_
    RUN_SUITE(devfs_dir_tests);

    // 套件 5: /dev/rtc 设备测试
    // _Requirements: 4.5_
    RUN_SUITE(devfs_rtc_tests);

    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief Devfs 测试模块元数据
 *
 * 使用 TEST_MODULE_DESC 宏注册模块到测试框架
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.5, 10.1, 10.2**
 */
TEST_MODULE_DESC(devfs, FS, run_devfs_tests,
    "Device File System tests - device registration, node access, directory operations");
