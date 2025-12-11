// ============================================================================
// arp_test.c - ARP 模块单元测试
// ============================================================================
//
// 测试 ARP（地址解析协议）缓存表的添加、查找和条目管理
// 实现 RFC 826 定义的 ARP 协议缓存功能测试
//
// **Feature: test-refactor**
// **Validates: Requirements 5.4**
//
// 测试覆盖:
//   - arp_cache_update(): 添加/更新 ARP 缓存条目
//   - arp_cache_lookup(): 查找 ARP 缓存
//   - arp_cache_delete(): 删除 ARP 缓存条目
//   - arp_cache_clear(): 清空所有 ARP 缓存
//   - arp_cache_count(): 获取缓存条目数量
//   - arp_cache_get_entry(): 获取指定索引的缓存条目
// ============================================================================

#include <tests/ktest.h>
#include <tests/arp_test.h>
#include <net/arp.h>
#include <lib/string.h>

// ============================================================================
// 测试辅助函数和数据
// ============================================================================

// 测试用 MAC 地址
static const uint8_t test_mac1[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t test_mac2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
static const uint8_t test_mac3[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
static const uint8_t zero_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 测试用 IP 地址（网络字节序）
// 192.168.1.1 = 0x0101A8C0 (little-endian storage)
#define TEST_IP1 0x0101A8C0  // 192.168.1.1
#define TEST_IP2 0x0201A8C0  // 192.168.1.2
#define TEST_IP3 0x0301A8C0  // 192.168.1.3
#define TEST_IP4 0x0401A8C0  // 192.168.1.4

/**
 * 比较两个 MAC 地址是否相等
 */
static bool mac_equal(const uint8_t *mac1, const uint8_t *mac2) {
    return memcmp(mac1, mac2, 6) == 0;
}

// ============================================================================
// 测试用例：arp_cache_update() 添加/更新缓存
// ============================================================================

/**
 * 测试添加单个 ARP 缓存条目
 * 添加后应能通过 lookup 找到
 */
TEST_CASE(test_arp_cache_add_single) {
    // 清空缓存确保干净状态
    arp_cache_clear();
    ASSERT_EQ(0, arp_cache_count());
    
    // 添加一个条目
    arp_cache_update(TEST_IP1, test_mac1);
    
    // 验证条目数量
    ASSERT_EQ(1, arp_cache_count());
    
    // 验证能查找到
    uint8_t mac_out[6];
    int ret = arp_cache_lookup(TEST_IP1, mac_out);
    ASSERT_EQ(0, ret);
    ASSERT_TRUE(mac_equal(mac_out, test_mac1));
    
    // 清理
    arp_cache_clear();
}

/**
 * 测试添加多个 ARP 缓存条目
 * 所有条目都应能正确查找
 */
TEST_CASE(test_arp_cache_add_multiple) {
    arp_cache_clear();
    
    // 添加多个条目
    arp_cache_update(TEST_IP1, test_mac1);
    arp_cache_update(TEST_IP2, test_mac2);
    arp_cache_update(TEST_IP3, test_mac3);
    
    // 验证条目数量
    ASSERT_EQ(3, arp_cache_count());
    
    // 验证每个条目都能查找到
    uint8_t mac_out[6];
    
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP1, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac1));
    
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP2, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac2));
    
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP3, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac3));
    
    arp_cache_clear();
}

/**
 * 测试更新已存在的 ARP 缓存条目
 * 更新后应返回新的 MAC 地址
 */
TEST_CASE(test_arp_cache_update_existing) {
    arp_cache_clear();
    
    // 添加初始条目
    arp_cache_update(TEST_IP1, test_mac1);
    
    uint8_t mac_out[6];
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP1, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac1));
    
    // 更新为新的 MAC 地址
    arp_cache_update(TEST_IP1, test_mac2);
    
    // 条目数量应保持不变
    ASSERT_EQ(1, arp_cache_count());
    
    // 应返回新的 MAC 地址
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP1, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac2));
    
    arp_cache_clear();
}

/**
 * 测试添加零 MAC 地址
 * 零 MAC 地址应被忽略
 */
TEST_CASE(test_arp_cache_add_zero_mac) {
    arp_cache_clear();
    
    // 尝试添加零 MAC 地址
    arp_cache_update(TEST_IP1, zero_mac);
    
    // 应该被忽略，条目数量为 0
    ASSERT_EQ(0, arp_cache_count());
    
    // 查找应失败
    uint8_t mac_out[6];
    int ret = arp_cache_lookup(TEST_IP1, mac_out);
    ASSERT_EQ(-1, ret);
    
    arp_cache_clear();
}

// ============================================================================
// 测试用例：arp_cache_lookup() 查找缓存
// ============================================================================

/**
 * 测试查找存在的条目
 * 应返回 0 并填充正确的 MAC 地址
 */
TEST_CASE(test_arp_cache_lookup_exists) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    
    uint8_t mac_out[6];
    int ret = arp_cache_lookup(TEST_IP1, mac_out);
    
    ASSERT_EQ(0, ret);
    ASSERT_TRUE(mac_equal(mac_out, test_mac1));
    
    arp_cache_clear();
}

/**
 * 测试查找不存在的条目
 * 应返回 -1
 */
TEST_CASE(test_arp_cache_lookup_not_exists) {
    arp_cache_clear();
    
    // 添加一个不同的 IP
    arp_cache_update(TEST_IP1, test_mac1);
    
    // 查找不存在的 IP
    uint8_t mac_out[6];
    int ret = arp_cache_lookup(TEST_IP2, mac_out);
    
    ASSERT_EQ(-1, ret);
    
    arp_cache_clear();
}

/**
 * 测试空缓存的查找
 * 应返回 -1
 */
TEST_CASE(test_arp_cache_lookup_empty) {
    arp_cache_clear();
    
    uint8_t mac_out[6];
    int ret = arp_cache_lookup(TEST_IP1, mac_out);
    
    ASSERT_EQ(-1, ret);
}

/**
 * 测试 NULL 参数的查找
 * 应返回 -1
 */
TEST_CASE(test_arp_cache_lookup_null_mac) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    
    int ret = arp_cache_lookup(TEST_IP1, NULL);
    ASSERT_EQ(-1, ret);
    
    arp_cache_clear();
}

// ============================================================================
// 测试用例：arp_cache_delete() 删除缓存
// ============================================================================

/**
 * 测试删除存在的条目
 * 删除后应无法查找到
 */
TEST_CASE(test_arp_cache_delete_exists) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    arp_cache_update(TEST_IP2, test_mac2);
    ASSERT_EQ(2, arp_cache_count());
    
    // 删除第一个条目
    int ret = arp_cache_delete(TEST_IP1);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, arp_cache_count());
    
    // 验证已删除
    uint8_t mac_out[6];
    ASSERT_EQ(-1, arp_cache_lookup(TEST_IP1, mac_out));
    
    // 验证另一个条目仍存在
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP2, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac2));
    
    arp_cache_clear();
}

/**
 * 测试删除不存在的条目
 * 应返回 -1
 */
TEST_CASE(test_arp_cache_delete_not_exists) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    
    // 尝试删除不存在的条目
    int ret = arp_cache_delete(TEST_IP2);
    ASSERT_EQ(-1, ret);
    
    // 原条目应仍存在
    ASSERT_EQ(1, arp_cache_count());
    
    arp_cache_clear();
}

/**
 * 测试删除空缓存中的条目
 * 应返回 -1
 */
TEST_CASE(test_arp_cache_delete_empty) {
    arp_cache_clear();
    
    int ret = arp_cache_delete(TEST_IP1);
    ASSERT_EQ(-1, ret);
}

// ============================================================================
// 测试用例：arp_cache_clear() 清空缓存
// ============================================================================

/**
 * 测试清空缓存
 * 清空后条目数量应为 0
 */
TEST_CASE(test_arp_cache_clear_all) {
    arp_cache_clear();
    
    // 添加多个条目
    arp_cache_update(TEST_IP1, test_mac1);
    arp_cache_update(TEST_IP2, test_mac2);
    arp_cache_update(TEST_IP3, test_mac3);
    ASSERT_EQ(3, arp_cache_count());
    
    // 清空
    arp_cache_clear();
    
    // 验证为空
    ASSERT_EQ(0, arp_cache_count());
    
    // 验证所有条目都无法查找
    uint8_t mac_out[6];
    ASSERT_EQ(-1, arp_cache_lookup(TEST_IP1, mac_out));
    ASSERT_EQ(-1, arp_cache_lookup(TEST_IP2, mac_out));
    ASSERT_EQ(-1, arp_cache_lookup(TEST_IP3, mac_out));
}

/**
 * 测试清空空缓存
 * 应安全处理
 */
TEST_CASE(test_arp_cache_clear_empty) {
    arp_cache_clear();
    ASSERT_EQ(0, arp_cache_count());
    
    // 再次清空应安全
    arp_cache_clear();
    ASSERT_EQ(0, arp_cache_count());
}

// ============================================================================
// 测试用例：arp_cache_count() 获取条目数量
// ============================================================================

/**
 * 测试空缓存的条目数量
 */
TEST_CASE(test_arp_cache_count_empty) {
    arp_cache_clear();
    ASSERT_EQ(0, arp_cache_count());
}

/**
 * 测试添加后的条目数量
 */
TEST_CASE(test_arp_cache_count_after_add) {
    arp_cache_clear();
    
    ASSERT_EQ(0, arp_cache_count());
    
    arp_cache_update(TEST_IP1, test_mac1);
    ASSERT_EQ(1, arp_cache_count());
    
    arp_cache_update(TEST_IP2, test_mac2);
    ASSERT_EQ(2, arp_cache_count());
    
    arp_cache_update(TEST_IP3, test_mac3);
    ASSERT_EQ(3, arp_cache_count());
    
    arp_cache_clear();
}

/**
 * 测试删除后的条目数量
 */
TEST_CASE(test_arp_cache_count_after_delete) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    arp_cache_update(TEST_IP2, test_mac2);
    ASSERT_EQ(2, arp_cache_count());
    
    arp_cache_delete(TEST_IP1);
    ASSERT_EQ(1, arp_cache_count());
    
    arp_cache_delete(TEST_IP2);
    ASSERT_EQ(0, arp_cache_count());
}

// ============================================================================
// 测试用例：arp_cache_get_entry() 获取指定条目
// ============================================================================

/**
 * 测试获取有效条目
 */
TEST_CASE(test_arp_cache_get_entry_valid) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    
    // 遍历查找添加的条目
    bool found = false;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        uint32_t ip;
        uint8_t mac[6];
        uint8_t state;
        
        if (arp_cache_get_entry(i, &ip, mac, &state) == 0) {
            if (ip == TEST_IP1) {
                found = true;
                ASSERT_TRUE(mac_equal(mac, test_mac1));
                ASSERT_EQ(ARP_STATE_RESOLVED, state);
                break;
            }
        }
    }
    
    ASSERT_TRUE(found);
    
    arp_cache_clear();
}

/**
 * 测试获取无效索引的条目
 */
TEST_CASE(test_arp_cache_get_entry_invalid_index) {
    arp_cache_clear();
    
    uint32_t ip;
    uint8_t mac[6];
    uint8_t state;
    
    // 负索引
    int ret = arp_cache_get_entry(-1, &ip, mac, &state);
    ASSERT_EQ(-1, ret);
    
    // 超出范围的索引
    ret = arp_cache_get_entry(ARP_CACHE_SIZE, &ip, mac, &state);
    ASSERT_EQ(-1, ret);
    
    ret = arp_cache_get_entry(ARP_CACHE_SIZE + 100, &ip, mac, &state);
    ASSERT_EQ(-1, ret);
}

/**
 * 测试获取空闲槽位的条目
 */
TEST_CASE(test_arp_cache_get_entry_free_slot) {
    arp_cache_clear();
    
    uint32_t ip;
    uint8_t mac[6];
    uint8_t state;
    
    // 空缓存中所有槽位都应返回 -1
    int ret = arp_cache_get_entry(0, &ip, mac, &state);
    ASSERT_EQ(-1, ret);
}

/**
 * 测试 NULL 参数
 */
TEST_CASE(test_arp_cache_get_entry_null_params) {
    arp_cache_clear();
    
    arp_cache_update(TEST_IP1, test_mac1);
    
    uint32_t ip;
    uint8_t mac[6];
    uint8_t state;
    
    // NULL ip 参数
    int ret = arp_cache_get_entry(0, NULL, mac, &state);
    ASSERT_EQ(-1, ret);
    
    // NULL mac 参数
    ret = arp_cache_get_entry(0, &ip, NULL, &state);
    ASSERT_EQ(-1, ret);
    
    // NULL state 参数
    ret = arp_cache_get_entry(0, &ip, mac, NULL);
    ASSERT_EQ(-1, ret);
    
    arp_cache_clear();
}

// ============================================================================
// 测试用例：arp_cache_add_static() 添加静态条目
// ============================================================================

/**
 * 测试添加静态 ARP 条目
 */
TEST_CASE(test_arp_cache_add_static_basic) {
    arp_cache_clear();
    
    int ret = arp_cache_add_static(TEST_IP1, test_mac1);
    ASSERT_EQ(0, ret);
    
    // 验证能查找到
    uint8_t mac_out[6];
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP1, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac1));
    
    arp_cache_clear();
}

/**
 * 测试添加静态条目 NULL MAC
 */
TEST_CASE(test_arp_cache_add_static_null_mac) {
    arp_cache_clear();
    
    int ret = arp_cache_add_static(TEST_IP1, NULL);
    ASSERT_EQ(-1, ret);
    
    ASSERT_EQ(0, arp_cache_count());
    
    arp_cache_clear();
}

// ============================================================================
// 测试用例：往返一致性测试
// ============================================================================

/**
 * 测试添加-查找往返一致性
 * 添加的 IP-MAC 映射应能正确查找回来
 */
TEST_CASE(test_arp_cache_roundtrip) {
    arp_cache_clear();
    
    // 测试多个不同的 IP-MAC 对
    struct {
        uint32_t ip;
        const uint8_t *mac;
    } test_pairs[] = {
        {TEST_IP1, test_mac1},
        {TEST_IP2, test_mac2},
        {TEST_IP3, test_mac3},
    };
    
    int num_pairs = sizeof(test_pairs) / sizeof(test_pairs[0]);
    
    // 添加所有条目
    for (int i = 0; i < num_pairs; i++) {
        arp_cache_update(test_pairs[i].ip, test_pairs[i].mac);
    }
    
    // 验证所有条目都能正确查找
    for (int i = 0; i < num_pairs; i++) {
        uint8_t mac_out[6];
        int ret = arp_cache_lookup(test_pairs[i].ip, mac_out);
        ASSERT_EQ(0, ret);
        ASSERT_TRUE(mac_equal(mac_out, test_pairs[i].mac));
    }
    
    arp_cache_clear();
}

/**
 * 测试添加-删除-查找一致性
 * 删除后应无法查找到
 */
TEST_CASE(test_arp_cache_add_delete_consistency) {
    arp_cache_clear();
    
    // 添加条目
    arp_cache_update(TEST_IP1, test_mac1);
    
    // 验证存在
    uint8_t mac_out[6];
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP1, mac_out));
    
    // 删除
    ASSERT_EQ(0, arp_cache_delete(TEST_IP1));
    
    // 验证不存在
    ASSERT_EQ(-1, arp_cache_lookup(TEST_IP1, mac_out));
    
    // 重新添加应该成功
    arp_cache_update(TEST_IP1, test_mac2);
    ASSERT_EQ(0, arp_cache_lookup(TEST_IP1, mac_out));
    ASSERT_TRUE(mac_equal(mac_out, test_mac2));
    
    arp_cache_clear();
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(arp_cache_update_tests) {
    RUN_TEST(test_arp_cache_add_single);
    RUN_TEST(test_arp_cache_add_multiple);
    RUN_TEST(test_arp_cache_update_existing);
    RUN_TEST(test_arp_cache_add_zero_mac);
}

TEST_SUITE(arp_cache_lookup_tests) {
    RUN_TEST(test_arp_cache_lookup_exists);
    RUN_TEST(test_arp_cache_lookup_not_exists);
    RUN_TEST(test_arp_cache_lookup_empty);
    RUN_TEST(test_arp_cache_lookup_null_mac);
}

TEST_SUITE(arp_cache_delete_tests) {
    RUN_TEST(test_arp_cache_delete_exists);
    RUN_TEST(test_arp_cache_delete_not_exists);
    RUN_TEST(test_arp_cache_delete_empty);
}

TEST_SUITE(arp_cache_clear_tests) {
    RUN_TEST(test_arp_cache_clear_all);
    RUN_TEST(test_arp_cache_clear_empty);
}

TEST_SUITE(arp_cache_count_tests) {
    RUN_TEST(test_arp_cache_count_empty);
    RUN_TEST(test_arp_cache_count_after_add);
    RUN_TEST(test_arp_cache_count_after_delete);
}

TEST_SUITE(arp_cache_get_entry_tests) {
    RUN_TEST(test_arp_cache_get_entry_valid);
    RUN_TEST(test_arp_cache_get_entry_invalid_index);
    RUN_TEST(test_arp_cache_get_entry_free_slot);
    RUN_TEST(test_arp_cache_get_entry_null_params);
}

TEST_SUITE(arp_cache_static_tests) {
    RUN_TEST(test_arp_cache_add_static_basic);
    RUN_TEST(test_arp_cache_add_static_null_mac);
}

TEST_SUITE(arp_cache_consistency_tests) {
    RUN_TEST(test_arp_cache_roundtrip);
    RUN_TEST(test_arp_cache_add_delete_consistency);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_arp_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(arp_cache_update_tests);
    RUN_SUITE(arp_cache_lookup_tests);
    RUN_SUITE(arp_cache_delete_tests);
    RUN_SUITE(arp_cache_clear_tests);
    RUN_SUITE(arp_cache_count_tests);
    RUN_SUITE(arp_cache_get_entry_tests);
    RUN_SUITE(arp_cache_static_tests);
    RUN_SUITE(arp_cache_consistency_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
