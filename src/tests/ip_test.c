// ============================================================================
// ip_test.c - IP 协议模块单元测试
// ============================================================================
//
// 测试 IPv4 协议实现，包括 IP 包构造、头部字段验证和校验和计算
// 实现 RFC 791 定义的 IPv4 协议测试
//
// **Feature: test-refactor**
// **Validates: Requirements 5.2**
//
// 测试覆盖:
//   - IP 头部构造和字段设置
//   - IP 头部校验和计算和验证
//   - IP 地址转换函数 (ip_to_str, str_to_ip)
//   - IP 子网检查 (ip_same_subnet)
//   - IP 下一跳计算 (ip_get_next_hop)
//   - IP 版本和头部长度提取
// ============================================================================

#include <tests/ktest.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <lib/string.h>
#include <lib/kprintf.h>

// ============================================================================
// 测试数据定义
// ============================================================================

// 测试用 IP 地址（网络字节序）
#define TEST_SRC_IP  IP_ADDR(192, 168, 1, 100)   // 192.168.1.100
#define TEST_DST_IP  IP_ADDR(192, 168, 1, 1)     // 192.168.1.1
#define TEST_IP_GW   IP_ADDR(192, 168, 1, 254)   // 192.168.1.254
#define TEST_IP_EXT  IP_ADDR(8, 8, 8, 8)         // 8.8.8.8

// 测试用子网掩码
#define TEST_NETMASK IP_ADDR(255, 255, 255, 0)   // 255.255.255.0

// ============================================================================
// 测试用例：IP 头部构造和字段设置
// ============================================================================

/**
 * 测试 IP 头部版本和 IHL 字段
 * 版本应为 4，IHL 应为 5（20 字节头部）
 */
TEST_CASE(test_ip_header_version_ihl) {
    ip_header_t ip;
    memset(&ip, 0, sizeof(ip));
    
    // 设置版本和 IHL
    ip.version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    
    // 验证版本
    uint8_t version = ip_version(&ip);
    ASSERT_EQ(IP_VERSION_4, version);
    
    // 验证头部长度
    uint8_t hdr_len = ip_header_len(&ip);
    ASSERT_EQ(IP_HEADER_MIN_LEN, hdr_len);
}

/**
 * 测试 IP 头部基本字段设置
 */
TEST_CASE(test_ip_header_basic_fields) {
    ip_header_t ip;
    memset(&ip, 0, sizeof(ip));
    
    // 设置基本字段
    ip.version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip.tos = 0;
    ip.total_length = htons(100);
    ip.identification = htons(0x1234);
    ip.flags_fragment = htons(IP_FLAG_DF);
    ip.ttl = 64;
    ip.protocol = IP_PROTO_TCP;
    ip.src_addr = TEST_SRC_IP;
    ip.dst_addr = TEST_DST_IP;
    
    // 验证字段
    ASSERT_EQ(100, ntohs(ip.total_length));
    ASSERT_EQ(0x1234, ntohs(ip.identification));
    ASSERT_EQ(IP_FLAG_DF, ntohs(ip.flags_fragment) & IP_FLAG_DF);
    ASSERT_EQ(64, ip.ttl);
    ASSERT_EQ(IP_PROTO_TCP, ip.protocol);
    ASSERT_EQ(TEST_SRC_IP, ip.src_addr);
    ASSERT_EQ(TEST_DST_IP, ip.dst_addr);
}

/**
 * 测试 IP 头部大小
 * 最小头部应为 20 字节
 */
TEST_CASE(test_ip_header_size) {
    ASSERT_EQ(20, sizeof(ip_header_t));
    ASSERT_EQ(20, IP_HEADER_MIN_LEN);
}

/**
 * 测试 IP 头部字节对齐
 * 头部应按字节对齐，无填充
 */
TEST_CASE(test_ip_header_packed) {
    // 验证结构体大小等于字段大小之和
    // version_ihl(1) + tos(1) + total_length(2) + identification(2) +
    // flags_fragment(2) + ttl(1) + protocol(1) + checksum(2) +
    // src_addr(4) + dst_addr(4) = 20 字节
    ASSERT_EQ(20, sizeof(ip_header_t));
}

// ============================================================================
// 测试用例：IP 校验和计算和验证
// ============================================================================

/**
 * 测试 IP 头部校验和计算
 * 校验和应正确计算
 */
TEST_CASE(test_ip_checksum_calculation) {
    ip_header_t ip;
    memset(&ip, 0, sizeof(ip));
    
    // 设置头部字段
    ip.version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip.tos = 0;
    ip.total_length = htons(60);
    ip.identification = htons(0x1c46);
    ip.flags_fragment = htons(0x4000);  // DF flag
    ip.ttl = 64;
    ip.protocol = IP_PROTO_TCP;
    ip.checksum = 0;  // 先设为 0
    ip.src_addr = IP_ADDR(172, 16, 10, 99);
    ip.dst_addr = IP_ADDR(172, 16, 10, 12);
    
    // 计算校验和
    uint16_t cs = ip_checksum(&ip, IP_HEADER_MIN_LEN);
    
    // 校验和应非零
    ASSERT_NE(0, cs);
    
    // 将校验和填入头部
    ip.checksum = cs;
    
    // 验证：包含校验和的头部再次计算应得到 0
    uint16_t verify_cs = ip_checksum(&ip, IP_HEADER_MIN_LEN);
    ASSERT_EQ(0, verify_cs);
}

/**
 * 测试 IP 校验和验证函数
 */
TEST_CASE(test_ip_checksum_verify) {
    ip_header_t ip;
    memset(&ip, 0, sizeof(ip));
    
    // 设置头部字段
    ip.version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip.tos = 0;
    ip.total_length = htons(60);
    ip.identification = htons(0x1c46);
    ip.flags_fragment = htons(0x4000);
    ip.ttl = 64;
    ip.protocol = IP_PROTO_TCP;
    ip.checksum = 0;
    ip.src_addr = IP_ADDR(172, 16, 10, 99);
    ip.dst_addr = IP_ADDR(172, 16, 10, 12);
    
    // 计算并填入校验和
    ip.checksum = ip_checksum(&ip, IP_HEADER_MIN_LEN);
    
    // 验证应通过
    uint16_t verify_result = ip_checksum(&ip, IP_HEADER_MIN_LEN);
    ASSERT_EQ(0, verify_result);
}

/**
 * 测试 IP 校验和对数据修改的敏感性
 * 修改任何字段后校验和应改变
 */
TEST_CASE(test_ip_checksum_sensitivity) {
    ip_header_t ip1, ip2;
    memset(&ip1, 0, sizeof(ip1));
    memset(&ip2, 0, sizeof(ip2));
    
    // 设置相同的初始字段
    ip1.version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip1.tos = 0;
    ip1.total_length = htons(60);
    ip1.identification = htons(0x1234);
    ip1.flags_fragment = htons(0x4000);
    ip1.ttl = 64;
    ip1.protocol = IP_PROTO_TCP;
    ip1.src_addr = TEST_SRC_IP;
    ip1.dst_addr = TEST_DST_IP;
    
    memcpy(&ip2, &ip1, sizeof(ip1));
    
    // 计算第一个头部的校验和
    uint16_t cs1 = ip_checksum(&ip1, IP_HEADER_MIN_LEN);
    
    // 修改第二个头部的 TTL
    ip2.ttl = 63;
    uint16_t cs2 = ip_checksum(&ip2, IP_HEADER_MIN_LEN);
    
    // 校验和应不同
    ASSERT_NE(cs1, cs2);
}

/**
 * 测试 IP 校验和对不同 TTL 的影响
 */
TEST_CASE(test_ip_checksum_ttl_change) {
    ip_header_t ip;
    memset(&ip, 0, sizeof(ip));
    
    ip.version_ihl = (IP_VERSION_4 << 4) | (IP_HEADER_MIN_LEN / 4);
    ip.tos = 0;
    ip.total_length = htons(60);
    ip.identification = htons(0x1234);
    ip.flags_fragment = htons(0x4000);
    ip.ttl = 64;
    ip.protocol = IP_PROTO_TCP;
    ip.src_addr = TEST_SRC_IP;
    ip.dst_addr = TEST_DST_IP;
    
    // 计算初始校验和
    uint16_t cs_initial = ip_checksum(&ip, IP_HEADER_MIN_LEN);
    
    // 减少 TTL（模拟转发）
    ip.ttl = 63;
    uint16_t cs_after_ttl = ip_checksum(&ip, IP_HEADER_MIN_LEN);
    
    // 校验和应改变
    ASSERT_NE(cs_initial, cs_after_ttl);
}

// ============================================================================
// 测试用例：IP 地址转换函数
// ============================================================================

/**
 * 测试 ip_to_str 函数
 * IP 地址应正确转换为字符串
 */
TEST_CASE(test_ip_to_str_basic) {
    char buf[16];
    
    // 测试 192.168.1.1
    uint32_t ip = IP_ADDR(192, 168, 1, 1);
    ip_to_str(ip, buf);
    
    // 验证字符串格式
    ASSERT_EQ('1', buf[0]);  // 第一个字符应是 '1'（192 的最后一位）
    
    // 验证包含点号
    bool has_dots = false;
    for (int i = 0; i < 15; i++) {
        if (buf[i] == '.') {
            has_dots = true;
            break;
        }
    }
    ASSERT_TRUE(has_dots);
}

/**
 * 测试 ip_to_str 多个地址
 */
TEST_CASE(test_ip_to_str_multiple) {
    char buf1[16], buf2[16], buf3[16];
    
    uint32_t ip1 = IP_ADDR(127, 0, 0, 1);
    uint32_t ip2 = IP_ADDR(255, 255, 255, 255);
    uint32_t ip3 = IP_ADDR(0, 0, 0, 0);
    
    ip_to_str(ip1, buf1);
    ip_to_str(ip2, buf2);
    ip_to_str(ip3, buf3);
    
    // 验证字符串非空
    ASSERT_NE(0, buf1[0]);
    ASSERT_NE(0, buf2[0]);
    ASSERT_NE(0, buf3[0]);
    
    // 验证字符串不同
    ASSERT_NE(0, strcmp(buf1, buf2));
    ASSERT_NE(0, strcmp(buf2, buf3));
}

/**
 * 测试 str_to_ip 函数
 * 字符串应正确转换为 IP 地址
 */
TEST_CASE(test_str_to_ip_basic) {
    uint32_t ip;
    
    // 测试有效的 IP 地址字符串
    int ret = str_to_ip("192.168.1.1", &ip);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(IP_ADDR(192, 168, 1, 1), ip);
}

/**
 * 测试 str_to_ip 多个地址
 */
TEST_CASE(test_str_to_ip_multiple) {
    uint32_t ip;
    
    // 测试 127.0.0.1
    int ret = str_to_ip("127.0.0.1", &ip);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(IP_ADDR(127, 0, 0, 1), ip);
    
    // 测试 255.255.255.255
    ret = str_to_ip("255.255.255.255", &ip);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(IP_ADDR(255, 255, 255, 255), ip);
    
    // 测试 0.0.0.0
    ret = str_to_ip("0.0.0.0", &ip);
    ASSERT_EQ(0, ret);
    ASSERT_EQ(IP_ADDR(0, 0, 0, 0), ip);
}

/**
 * 测试 str_to_ip 无效输入
 */
TEST_CASE(test_str_to_ip_invalid) {
    uint32_t ip;
    
    // 测试无效格式
    int ret = str_to_ip("256.1.1.1", &ip);
    ASSERT_EQ(-1, ret);
    
    ret = str_to_ip("192.168.1", &ip);
    ASSERT_EQ(-1, ret);
    
    ret = str_to_ip("192.168.1.1.1", &ip);
    ASSERT_EQ(-1, ret);
    
    ret = str_to_ip("abc.def.ghi.jkl", &ip);
    ASSERT_EQ(-1, ret);
}

/**
 * 测 str_to_ip NULL 参数
 */
TEST_CASE(test_str_to_ip_null) {
    uint32_t ip;
    
    int ret = str_to_ip(NULL, &ip);
    ASSERT_EQ(-1, ret);
    
    ret = str_to_ip("192.168.1.1", NULL);
    ASSERT_EQ(-1, ret);
}

/**
 * 测试 ip_to_str 和 str_to_ip 往返一致性
 * 转换后再转换回来应得到相同的 IP 地址
 */
TEST_CASE(test_ip_addr_roundtrip) {
    char buf[16];
    uint32_t original = IP_ADDR(192, 168, 1, 100);
    uint32_t converted;
    
    // 转换为字符串
    ip_to_str(original, buf);
    
    // 转换回 IP 地址
    int ret = str_to_ip(buf, &converted);
    ASSERT_EQ(0, ret);
    
    // 应相等
    ASSERT_EQ(original, converted);
}

// ============================================================================
// 测试用例：IP 子网检查
// ============================================================================

/**
 * 测试 ip_same_subnet 同一子网
 */
TEST_CASE(test_ip_same_subnet_true) {
    uint32_t ip1 = IP_ADDR(192, 168, 1, 100);
    uint32_t ip2 = IP_ADDR(192, 168, 1, 200);
    uint32_t netmask = IP_ADDR(255, 255, 255, 0);
    
    bool result = ip_same_subnet(ip1, ip2, netmask);
    ASSERT_TRUE(result);
}

/**
 * 测试 ip_same_subnet 不同子网
 */
TEST_CASE(test_ip_same_subnet_false) {
    uint32_t ip1 = IP_ADDR(192, 168, 1, 100);
    uint32_t ip2 = IP_ADDR(192, 168, 2, 100);
    uint32_t netmask = IP_ADDR(255, 255, 255, 0);
    
    bool result = ip_same_subnet(ip1, ip2, netmask);
    ASSERT_FALSE(result);
}

/**
 * 测试 ip_same_subnet 不同子网掩码
 */
TEST_CASE(test_ip_same_subnet_different_mask) {
    uint32_t ip1 = IP_ADDR(192, 168, 1, 100);
    uint32_t ip2 = IP_ADDR(192, 168, 2, 100);
    
    // /24 掩码：不同子网
    uint32_t mask24 = IP_ADDR(255, 255, 255, 0);
    ASSERT_FALSE(ip_same_subnet(ip1, ip2, mask24));
    
    // /16 掩码：同一子网
    uint32_t mask16 = IP_ADDR(255, 255, 0, 0);
    ASSERT_TRUE(ip_same_subnet(ip1, ip2, mask16));
}

/**
 * 测试 ip_same_subnet 全 0 和全 1 掩码
 */
TEST_CASE(test_ip_same_subnet_edge_masks) {
    uint32_t ip1 = IP_ADDR(192, 168, 1, 100);
    uint32_t ip2 = IP_ADDR(10, 0, 0, 1);
    
    // 全 0 掩码：所有地址都在同一子网
    uint32_t mask_zero = IP_ADDR(0, 0, 0, 0);
    ASSERT_TRUE(ip_same_subnet(ip1, ip2, mask_zero));
    
    // 全 1 掩码：只有相同地址才在同一子网
    uint32_t mask_all = IP_ADDR(255, 255, 255, 255);
    ASSERT_FALSE(ip_same_subnet(ip1, ip2, mask_all));
    ASSERT_TRUE(ip_same_subnet(ip1, ip1, mask_all));
}

// ============================================================================
// 测试用例：IP 下一跳计算
// ============================================================================

/**
 * 测试 ip_get_next_hop 同一子网
 * 同一子网的目的地址应直接作为下一跳
 */
TEST_CASE(test_ip_get_next_hop_same_subnet) {
    // 模拟网络设备
    netdev_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.ip_addr = IP_ADDR(192, 168, 1, 100);
    dev.netmask = IP_ADDR(255, 255, 255, 0);
    dev.gateway = 0;
    
    uint32_t dst_ip = IP_ADDR(192, 168, 1, 200);
    uint32_t next_hop = ip_get_next_hop(&dev, dst_ip);
    
    // 下一跳应是目的地址本身
    ASSERT_EQ(dst_ip, next_hop);
}

/**
 * 测试 ip_get_next_hop 不同子网有网关
 * 不同子网且有网关时应返回网关地址
 */
TEST_CASE(test_ip_get_next_hop_different_subnet_with_gw) {
    netdev_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.ip_addr = IP_ADDR(192, 168, 1, 100);
    dev.netmask = IP_ADDR(255, 255, 255, 0);
    dev.gateway = IP_ADDR(192, 168, 1, 254);
    
    uint32_t dst_ip = IP_ADDR(8, 8, 8, 8);
    uint32_t next_hop = ip_get_next_hop(&dev, dst_ip);
    
    // 下一跳应是网关地址
    ASSERT_EQ(dev.gateway, next_hop);
}

/**
 * 测试 ip_get_next_hop 不同子网无网关
 * 不同子网且无网关时应返回目的地址
 */
TEST_CASE(test_ip_get_next_hop_different_subnet_no_gw) {
    netdev_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.ip_addr = IP_ADDR(192, 168, 1, 100);
    dev.netmask = IP_ADDR(255, 255, 255, 0);
    dev.gateway = 0;
    
    uint32_t dst_ip = IP_ADDR(8, 8, 8, 8);
    uint32_t next_hop = ip_get_next_hop(&dev, dst_ip);
    
    // 下一跳应是目的地址本身
    ASSERT_EQ(dst_ip, next_hop);
}

/**
 * 测试 ip_get_next_hop NULL 设备
 */
TEST_CASE(test_ip_get_next_hop_null_dev) {
    uint32_t dst_ip = IP_ADDR(192, 168, 1, 1);
    uint32_t next_hop = ip_get_next_hop(NULL, dst_ip);
    
    // 应返回目的地址
    ASSERT_EQ(dst_ip, next_hop);
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(ip_header_tests) {
    RUN_TEST(test_ip_header_version_ihl);
    RUN_TEST(test_ip_header_basic_fields);
    RUN_TEST(test_ip_header_size);
    RUN_TEST(test_ip_header_packed);
}

TEST_SUITE(ip_checksum_tests) {
    RUN_TEST(test_ip_checksum_calculation);
    RUN_TEST(test_ip_checksum_verify);
    RUN_TEST(test_ip_checksum_sensitivity);
    RUN_TEST(test_ip_checksum_ttl_change);
}

TEST_SUITE(ip_addr_conversion_tests) {
    RUN_TEST(test_ip_to_str_basic);
    RUN_TEST(test_ip_to_str_multiple);
    RUN_TEST(test_str_to_ip_basic);
    RUN_TEST(test_str_to_ip_multiple);
    RUN_TEST(test_str_to_ip_invalid);
    RUN_TEST(test_str_to_ip_null);
    RUN_TEST(test_ip_addr_roundtrip);
}

TEST_SUITE(ip_subnet_tests) {
    RUN_TEST(test_ip_same_subnet_true);
    RUN_TEST(test_ip_same_subnet_false);
    RUN_TEST(test_ip_same_subnet_different_mask);
    RUN_TEST(test_ip_same_subnet_edge_masks);
}

TEST_SUITE(ip_next_hop_tests) {
    RUN_TEST(test_ip_get_next_hop_same_subnet);
    RUN_TEST(test_ip_get_next_hop_different_subnet_with_gw);
    RUN_TEST(test_ip_get_next_hop_different_subnet_no_gw);
    RUN_TEST(test_ip_get_next_hop_null_dev);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_ip_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(ip_header_tests);
    RUN_SUITE(ip_checksum_tests);
    RUN_SUITE(ip_addr_conversion_tests);
    RUN_SUITE(ip_subnet_tests);
    RUN_SUITE(ip_next_hop_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
