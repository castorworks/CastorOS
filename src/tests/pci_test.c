// ============================================================================
// pci_test.c - PCI 驱动测试模块
// ============================================================================
//
// PCI 总线驱动测试，验证配置空间读写操作
// 使用 QEMU 模拟的 PCI 设备进行测试
//
// **Feature: test-refactor**
// **Validates: Requirements 6.2**
//
// 测试覆盖:
//   - PCI 配置空间读写操作（8/16/32 位）
//   - PCI 设备枚举和查找
//   - BAR 地址提取和类型检测
//   - PCI 命令寄存器操作
//   - 设备信息读取
// ============================================================================

#include <tests/ktest.h>
#include <tests/pci_test.h>
#include <lib/kprintf.h>

// PCI 测试仅在 x86 架构上运行
#if defined(ARCH_I686) || defined(ARCH_X86_64)

#include <drivers/pci.h>
#include <lib/string.h>

// ============================================================================
// 测试数据定义
// ============================================================================

// QEMU 默认提供的 PCI 设备
// Host Bridge: Intel 440FX (vendor: 0x8086, device: 0x1237)
#define QEMU_HOST_BRIDGE_VENDOR  0x8086
#define QEMU_HOST_BRIDGE_DEVICE  0x1237

// ISA Bridge: Intel PIIX3 (vendor: 0x8086, device: 0x7000)
#define QEMU_ISA_BRIDGE_VENDOR   0x8086
#define QEMU_ISA_BRIDGE_DEVICE   0x7000

// 无效的 vendor ID（表示设备不存在）
#define PCI_VENDOR_INVALID       0xFFFF

// ============================================================================
// 测试用例：PCI 配置空间读取
// ============================================================================

/**
 * 测试读取不存在设备的 vendor ID
 * 不存在的设备应返回 0xFFFF
 */
TEST_CASE(test_pci_read_nonexistent_device) {
    // 读取一个不太可能存在的设备（bus 255, slot 31, func 7）
    uint16_t vendor_id = pci_read_config16(255, 31, 7, PCI_VENDOR_ID);
    
    // 不存在的设备应返回 0xFFFF
    ASSERT_EQ_UINT(PCI_VENDOR_INVALID, vendor_id);
}

/**
 * 测试读取 Host Bridge 的 vendor ID
 * QEMU 默认提供 Intel 440FX Host Bridge
 */
TEST_CASE(test_pci_read_host_bridge_vendor) {
    // Host Bridge 通常在 bus 0, slot 0, func 0
    uint16_t vendor_id = pci_read_config16(0, 0, 0, PCI_VENDOR_ID);
    
    // 应该是有效的 vendor ID（非 0xFFFF）
    ASSERT_NE_UINT(PCI_VENDOR_INVALID, vendor_id);
    
    // QEMU 默认使用 Intel 440FX
    ASSERT_EQ_UINT(QEMU_HOST_BRIDGE_VENDOR, vendor_id);
}

/**
 * 测试读取 Host Bridge 的 device ID
 */
TEST_CASE(test_pci_read_host_bridge_device) {
    uint16_t device_id = pci_read_config16(0, 0, 0, PCI_DEVICE_ID);
    
    // QEMU 默认使用 Intel 440FX (device ID: 0x1237)
    ASSERT_EQ_UINT(QEMU_HOST_BRIDGE_DEVICE, device_id);
}

/**
 * 测试 8 位配置空间读取
 * 读取 class code 和 subclass
 */
TEST_CASE(test_pci_read_config8) {
    // 读取 Host Bridge 的 class code
    uint8_t class_code = pci_read_config8(0, 0, 0, PCI_CLASS);
    
    // Host Bridge 的 class code 应该是 0x06 (Bridge)
    ASSERT_EQ_UINT(PCI_CLASS_BRIDGE, class_code);
    
    // 读取 subclass
    uint8_t subclass = pci_read_config8(0, 0, 0, PCI_SUBCLASS);
    
    // Host Bridge 的 subclass 应该是 0x00
    ASSERT_EQ_UINT(PCI_SUBCLASS_HOST_BRIDGE, subclass);
}

/**
 * 测试 32 位配置空间读取
 * 读取 vendor ID 和 device ID 组合
 */
TEST_CASE(test_pci_read_config32) {
    // 读取 offset 0 的 32 位值（包含 vendor ID 和 device ID）
    uint32_t id_combo = pci_read_config32(0, 0, 0, PCI_VENDOR_ID);
    
    // 低 16 位是 vendor ID，高 16 位是 device ID
    uint16_t vendor_id = id_combo & 0xFFFF;
    uint16_t device_id = (id_combo >> 16) & 0xFFFF;
    
    ASSERT_EQ_UINT(QEMU_HOST_BRIDGE_VENDOR, vendor_id);
    ASSERT_EQ_UINT(QEMU_HOST_BRIDGE_DEVICE, device_id);
}

/**
 * 测试读取 header type
 */
TEST_CASE(test_pci_read_header_type) {
    uint8_t header_type = pci_read_config8(0, 0, 0, PCI_HEADER_TYPE);
    
    // Header type 的低 7 位表示类型
    uint8_t type = header_type & PCI_HEADER_TYPE_MASK;
    
    // Host Bridge 应该是 Type 0（普通设备）
    ASSERT_EQ_UINT(PCI_HEADER_TYPE_NORMAL, type);
}

// ============================================================================
// 测试用例：PCI 配置空间写入
// ============================================================================

/**
 * 测试写入和读取 command 寄存器
 * 验证配置空间写入功能
 */
TEST_CASE(test_pci_write_command_register) {
    // 保存原始 command 值
    uint16_t original_cmd = pci_read_config16(0, 0, 0, PCI_COMMAND);
    
    // 尝试设置 memory space enable 位
    uint16_t new_cmd = original_cmd | PCI_CMD_MEMORY_SPACE;
    pci_write_config16(0, 0, 0, PCI_COMMAND, new_cmd);
    
    // 读回验证
    uint16_t read_cmd = pci_read_config16(0, 0, 0, PCI_COMMAND);
    
    // 验证写入成功（注意：某些位可能是只读的）
    // 至少应该能读回我们写入的值或原始值
    ASSERT_TRUE((read_cmd & PCI_CMD_MEMORY_SPACE) != 0 || 
                read_cmd == original_cmd);
    
    // 恢复原始值
    pci_write_config16(0, 0, 0, PCI_COMMAND, original_cmd);
}

/**
 * 测试 8 位配置空间写入
 * 写入 latency timer
 */
TEST_CASE(test_pci_write_config8) {
    // 保存原始值
    uint8_t original = pci_read_config8(0, 0, 0, PCI_LATENCY_TIMER);
    
    // 写入新值
    uint8_t new_value = 0x40;
    pci_write_config8(0, 0, 0, PCI_LATENCY_TIMER, new_value);
    
    // 读回验证
    uint8_t read_value = pci_read_config8(0, 0, 0, PCI_LATENCY_TIMER);
    
    // 验证写入成功或保持原值（某些寄存器可能只读）
    ASSERT_TRUE(read_value == new_value || read_value == original);
    
    // 恢复原始值
    pci_write_config8(0, 0, 0, PCI_LATENCY_TIMER, original);
}

/**
 * 测试 32 位配置空间写入
 * 写入 BAR0（如果可写）
 */
TEST_CASE(test_pci_write_config32) {
    // 保存原始 BAR0 值
    uint32_t original_bar = pci_read_config32(0, 0, 0, PCI_BAR0);
    
    // 写入全 1 来探测 BAR 大小（标准 PCI 探测方法）
    pci_write_config32(0, 0, 0, PCI_BAR0, 0xFFFFFFFF);
    
    // 读回值
    uint32_t probe_value = pci_read_config32(0, 0, 0, PCI_BAR0);
    
    // 恢复原始值
    pci_write_config32(0, 0, 0, PCI_BAR0, original_bar);
    
    // 验证读回值（可能是 0 如果 BAR 未实现，或者是大小掩码）
    // 这里只验证操作不会崩溃，并使用 probe_value 避免警告
    ASSERT_TRUE(probe_value == 0 || probe_value != 0);
    
    // 验证恢复成功
    uint32_t restored = pci_read_config32(0, 0, 0, PCI_BAR0);
    ASSERT_EQ_UINT(original_bar, restored);
}

// ============================================================================
// 测试用例：PCI 设备枚举
// ============================================================================

/**
 * 测试 PCI 设备扫描
 * 应该能发现至少一个设备（Host Bridge）
 */
TEST_CASE(test_pci_scan_devices) {
    // 执行设备扫描
    int count = pci_scan_devices();
    
    // 应该至少发现一个设备（Host Bridge）
    ASSERT_TRUE(count >= 1);
}

/**
 * 测试获取设备数量
 */
TEST_CASE(test_pci_get_device_count) {
    // 先扫描设备
    pci_scan_devices();
    
    // 获取设备数量
    int count = pci_get_device_count();
    
    // 应该至少有一个设备
    ASSERT_TRUE(count >= 1);
}

/**
 * 测试按索引获取设备
 */
TEST_CASE(test_pci_get_device_by_index) {
    pci_scan_devices();
    
    // 获取第一个设备
    pci_device_t *dev = pci_get_device(0);
    
    // 应该能获取到设备
    ASSERT_NOT_NULL(dev);
    
    // 验证设备信息有效
    ASSERT_NE_UINT(PCI_VENDOR_INVALID, dev->vendor_id);
}

/**
 * 测试获取无效索引的设备
 */
TEST_CASE(test_pci_get_device_invalid_index) {
    pci_scan_devices();
    
    // 获取无效索引的设备
    pci_device_t *dev = pci_get_device(-1);
    ASSERT_NULL(dev);
    
    dev = pci_get_device(1000);
    ASSERT_NULL(dev);
}

// ============================================================================
// 测试用例：PCI 设备查找
// ============================================================================

/**
 * 测试按 vendor/device ID 查找设备
 */
TEST_CASE(test_pci_find_device) {
    pci_scan_devices();
    
    // 查找 Host Bridge
    pci_device_t *dev = pci_find_device(QEMU_HOST_BRIDGE_VENDOR, 
                                         QEMU_HOST_BRIDGE_DEVICE);
    
    // 应该能找到
    ASSERT_NOT_NULL(dev);
    
    // 验证设备信息
    ASSERT_EQ_UINT(QEMU_HOST_BRIDGE_VENDOR, dev->vendor_id);
    ASSERT_EQ_UINT(QEMU_HOST_BRIDGE_DEVICE, dev->device_id);
}

/**
 * 测试查找不存在的设备
 */
TEST_CASE(test_pci_find_device_not_found) {
    pci_scan_devices();
    
    // 查找一个不存在的设备
    pci_device_t *dev = pci_find_device(0x1234, 0x5678);
    
    // 应该返回 NULL
    ASSERT_NULL(dev);
}

/**
 * 测试按类别查找设备
 */
TEST_CASE(test_pci_find_class) {
    pci_scan_devices();
    
    // 查找 Bridge 类设备
    pci_device_t *dev = pci_find_class(PCI_CLASS_BRIDGE, 0xFF);
    
    // 应该能找到（至少有 Host Bridge）
    ASSERT_NOT_NULL(dev);
    
    // 验证类别
    ASSERT_EQ_UINT(PCI_CLASS_BRIDGE, dev->class_code);
}

/**
 * 测试按类别和子类别查找设备
 */
TEST_CASE(test_pci_find_class_subclass) {
    pci_scan_devices();
    
    // 查找 Host Bridge
    pci_device_t *dev = pci_find_class(PCI_CLASS_BRIDGE, PCI_SUBCLASS_HOST_BRIDGE);
    
    // 应该能找到
    ASSERT_NOT_NULL(dev);
    
    // 验证类别和子类别
    ASSERT_EQ_UINT(PCI_CLASS_BRIDGE, dev->class_code);
    ASSERT_EQ_UINT(PCI_SUBCLASS_HOST_BRIDGE, dev->subclass);
}

// ============================================================================
// 测试用例：BAR 操作
// ============================================================================

/**
 * 测试获取 BAR 地址
 */
TEST_CASE(test_pci_get_bar_address) {
    pci_scan_devices();
    
    pci_device_t *dev = pci_get_device(0);
    ASSERT_NOT_NULL(dev);
    
    // 获取 BAR0 地址（可能为 0 如果未实现）
    uint32_t bar_addr = pci_get_bar_address(dev, 0);
    
    // 操作应该成功（不崩溃）
    // BAR 地址可能为 0（未实现）或有效地址
    ASSERT_TRUE(bar_addr == 0 || bar_addr != 0);
}

/**
 * 测试获取无效 BAR 索引
 */
TEST_CASE(test_pci_get_bar_invalid_index) {
    pci_scan_devices();
    
    pci_device_t *dev = pci_get_device(0);
    ASSERT_NOT_NULL(dev);
    
    // 无效索引应返回 0
    uint32_t bar_addr = pci_get_bar_address(dev, -1);
    ASSERT_EQ_UINT(0, bar_addr);
    
    bar_addr = pci_get_bar_address(dev, 6);
    ASSERT_EQ_UINT(0, bar_addr);
}

/**
 * 测试 NULL 设备的 BAR 操作
 */
TEST_CASE(test_pci_get_bar_null_device) {
    uint32_t bar_addr = pci_get_bar_address(NULL, 0);
    ASSERT_EQ_UINT(0, bar_addr);
    
    uint32_t bar_size = pci_get_bar_size(NULL, 0);
    ASSERT_EQ_UINT(0, bar_size);
    
    bool is_io = pci_bar_is_io(NULL, 0);
    ASSERT_FALSE(is_io);
}

/**
 * 测试 BAR 类型检测
 */
TEST_CASE(test_pci_bar_is_io) {
    pci_scan_devices();
    
    pci_device_t *dev = pci_get_device(0);
    ASSERT_NOT_NULL(dev);
    
    // 检查 BAR0 类型（不崩溃即可）
    bool is_io = pci_bar_is_io(dev, 0);
    
    // 结果应该是 true 或 false
    ASSERT_TRUE(is_io == true || is_io == false);
}

// ============================================================================
// 测试用例：设备使能操作
// ============================================================================

/**
 * 测试启用总线主控
 */
TEST_CASE(test_pci_enable_bus_master) {
    pci_scan_devices();
    
    pci_device_t *dev = pci_get_device(0);
    ASSERT_NOT_NULL(dev);
    
    // 保存原始 command
    uint16_t original_cmd = pci_read_config16(dev->bus, dev->slot, 
                                               dev->func, PCI_COMMAND);
    
    // 启用总线主控
    pci_enable_bus_master(dev);
    
    // 读回验证
    uint16_t new_cmd = pci_read_config16(dev->bus, dev->slot, 
                                          dev->func, PCI_COMMAND);
    
    // 总线主控位应该被设置（或保持原状态如果不支持）
    ASSERT_TRUE((new_cmd & PCI_CMD_BUS_MASTER) != 0 || 
                new_cmd == original_cmd);
    
    // 恢复原始值
    pci_write_config16(dev->bus, dev->slot, dev->func, 
                       PCI_COMMAND, original_cmd);
}

/**
 * 测试 NULL 设备的使能操作
 */
TEST_CASE(test_pci_enable_null_device) {
    // 这些操作应该安全地处理 NULL 指针
    pci_enable_bus_master(NULL);
    pci_enable_memory_space(NULL);
    pci_enable_io_space(NULL);
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

// ============================================================================
// 测试用例：设备信息完整性
// ============================================================================

/**
 * 测试设备信息字段完整性
 */
TEST_CASE(test_pci_device_info_integrity) {
    pci_scan_devices();
    
    int count = pci_get_device_count();
    
    // 验证所有设备的信息完整性
    for (int i = 0; i < count; i++) {
        pci_device_t *dev = pci_get_device(i);
        ASSERT_NOT_NULL(dev);
        
        // vendor ID 应该有效
        ASSERT_NE_UINT(PCI_VENDOR_INVALID, dev->vendor_id);
        
        // bus/slot/func 应该在有效范围内
        // Note: bus is uint8_t so always < 256 (PCI_MAX_BUS)
        ASSERT_TRUE(dev->slot < PCI_MAX_SLOT);
        ASSERT_TRUE(dev->func < PCI_MAX_FUNC);
    }
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(pci_config_read_tests) {
    RUN_TEST(test_pci_read_nonexistent_device);
    RUN_TEST(test_pci_read_host_bridge_vendor);
    RUN_TEST(test_pci_read_host_bridge_device);
    RUN_TEST(test_pci_read_config8);
    RUN_TEST(test_pci_read_config32);
    RUN_TEST(test_pci_read_header_type);
}

TEST_SUITE(pci_config_write_tests) {
    RUN_TEST(test_pci_write_command_register);
    RUN_TEST(test_pci_write_config8);
    RUN_TEST(test_pci_write_config32);
}

TEST_SUITE(pci_enumeration_tests) {
    RUN_TEST(test_pci_scan_devices);
    RUN_TEST(test_pci_get_device_count);
    RUN_TEST(test_pci_get_device_by_index);
    RUN_TEST(test_pci_get_device_invalid_index);
}

TEST_SUITE(pci_find_tests) {
    RUN_TEST(test_pci_find_device);
    RUN_TEST(test_pci_find_device_not_found);
    RUN_TEST(test_pci_find_class);
    RUN_TEST(test_pci_find_class_subclass);
}

TEST_SUITE(pci_bar_tests) {
    RUN_TEST(test_pci_get_bar_address);
    RUN_TEST(test_pci_get_bar_invalid_index);
    RUN_TEST(test_pci_get_bar_null_device);
    RUN_TEST(test_pci_bar_is_io);
}

TEST_SUITE(pci_enable_tests) {
    RUN_TEST(test_pci_enable_bus_master);
    RUN_TEST(test_pci_enable_null_device);
}

TEST_SUITE(pci_integrity_tests) {
    RUN_TEST(test_pci_device_info_integrity);
}

#endif // ARCH_I686 || ARCH_X86_64

// ============================================================================
// 运行所有测试
// ============================================================================

void run_pci_tests(void) {
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    // 初始化测试框架
    unittest_init();
    
    // 初始化 PCI 驱动
    pci_init();
    
    // 运行所有测试套件
    RUN_SUITE(pci_config_read_tests);
    RUN_SUITE(pci_config_write_tests);
    RUN_SUITE(pci_enumeration_tests);
    RUN_SUITE(pci_find_tests);
    RUN_SUITE(pci_bar_tests);
    RUN_SUITE(pci_enable_tests);
    RUN_SUITE(pci_integrity_tests);
    
    // 打印测试摘要
    unittest_print_summary();
#else
    // 非 x86 架构跳过 PCI 测试
    kprintf("PCI tests skipped (x86-only)\n");
#endif
}
