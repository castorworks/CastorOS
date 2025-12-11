// ============================================================================
// tcp_test.c - TCP 协议模块单元测试
// ============================================================================
//
// 测试 TCP 协议实现，包括 TCP 段解析、头部字段提取和校验和计算
// 实现 RFC 793 定义的 TCP 协议测试
//
// **Feature: test-refactor**
// **Validates: Requirements 5.3**
//
// 测试覆盖:
//   - TCP 头部构造和字段设置
//   - TCP 头部大小和字节对齐
//   - TCP 标志位提取和设置
//   - TCP 序列号和确认号处理
//   - TCP 校验和计算和验证
//   - TCP 数据偏移和头部长度提取
//   - TCP 窗口大小处理
// ============================================================================

#include <tests/ktest.h>
#include <net/tcp.h>
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

// 测试用端口（主机字节序）
#define TEST_SRC_PORT  12345
#define TEST_DST_PORT  80

// ============================================================================
// 测试用例：TCP 头部构造和字段设置
// ============================================================================

/**
 * 测试 TCP 头部版本和数据偏移字段
 * 数据偏移应为 5（20 字节头部）
 */
TEST_CASE(test_tcp_header_data_offset) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置数据偏移为 5（20 字节头部）
    tcp.data_offset = (5 << 4);
    
    // 验证数据偏移
    uint8_t offset = tcp_header_len(&tcp) / 4;
    ASSERT_EQ(5, offset);
}

/**
 * 测试 TCP 头部基本字段设置
 */
TEST_CASE(test_tcp_header_basic_fields) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置基本字段
    tcp.src_port = htons(TEST_SRC_PORT);
    tcp.dst_port = htons(TEST_DST_PORT);
    tcp.seq_num = htonl(1000);
    tcp.ack_num = htonl(2000);
    tcp.data_offset = (5 << 4);
    tcp.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
    tcp.window = htons(4096);
    tcp.checksum = 0;
    tcp.urgent_ptr = 0;
    
    // 验证字段
    ASSERT_EQ(TEST_SRC_PORT, ntohs(tcp.src_port));
    ASSERT_EQ(TEST_DST_PORT, ntohs(tcp.dst_port));
    ASSERT_EQ(1000, ntohl(tcp.seq_num));
    ASSERT_EQ(2000, ntohl(tcp.ack_num));
    ASSERT_EQ(TCP_FLAG_SYN | TCP_FLAG_ACK, tcp.flags);
    ASSERT_EQ(4096, ntohs(tcp.window));
}

/**
 * 测试 TCP 头部大小
 * 最小头部应为 20 字节
 */
TEST_CASE(test_tcp_header_size) {
    ASSERT_EQ(20, sizeof(tcp_header_t));
    ASSERT_EQ(20, TCP_HEADER_MIN_LEN);
}

/**
 * 测试 TCP 头部字节对齐
 * 头部应按字节对齐，无填充
 */
TEST_CASE(test_tcp_header_packed) {
    // 验证结构体大小等于字段大小之和
    // src_port(2) + dst_port(2) + seq_num(4) + ack_num(4) +
    // data_offset(1) + flags(1) + window(2) + checksum(2) +
    // urgent_ptr(2) = 20 字节
    ASSERT_EQ(20, sizeof(tcp_header_t));
}

// ============================================================================
// 测试用例：TCP 标志位提取和设置
// ============================================================================

/**
 * 测试 TCP SYN 标志位
 */
TEST_CASE(test_tcp_flag_syn) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 SYN 标志
    tcp.flags = TCP_FLAG_SYN;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_SYN);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_ACK);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_FIN);
}

/**
 * 测试 TCP ACK 标志位
 */
TEST_CASE(test_tcp_flag_ack) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 ACK 标志
    tcp.flags = TCP_FLAG_ACK;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_ACK);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_SYN);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_FIN);
}

/**
 * 测试 TCP FIN 标志位
 */
TEST_CASE(test_tcp_flag_fin) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 FIN 标志
    tcp.flags = TCP_FLAG_FIN;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_FIN);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_ACK);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_SYN);
}

/**
 * 测试 TCP RST 标志位
 */
TEST_CASE(test_tcp_flag_rst) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 RST 标志
    tcp.flags = TCP_FLAG_RST;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_RST);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_ACK);
}

/**
 * 测试 TCP PSH 标志位
 */
TEST_CASE(test_tcp_flag_psh) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 PSH 标志
    tcp.flags = TCP_FLAG_PSH;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_PSH);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_ACK);
}

/**
 * 测试 TCP URG 标志位
 */
TEST_CASE(test_tcp_flag_urg) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 URG 标志
    tcp.flags = TCP_FLAG_URG;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_URG);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_ACK);
}

/**
 * 测试 TCP 多个标志位组合
 */
TEST_CASE(test_tcp_flags_combination) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置 SYN + ACK 标志
    tcp.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_SYN);
    ASSERT_TRUE(tcp.flags & TCP_FLAG_ACK);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_FIN);
    
    // 设置 FIN + ACK 标志
    tcp.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
    
    // 验证标志
    ASSERT_TRUE(tcp.flags & TCP_FLAG_FIN);
    ASSERT_TRUE(tcp.flags & TCP_FLAG_ACK);
    ASSERT_FALSE(tcp.flags & TCP_FLAG_SYN);
}

// ============================================================================
// 测试用例：TCP 序列号和确认号处理
// ============================================================================

/**
 * 测试 TCP 序列号设置和提取
 */
TEST_CASE(test_tcp_sequence_number) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置序列号
    uint32_t seq = 0x12345678;
    tcp.seq_num = htonl(seq);
    
    // 验证序列号
    ASSERT_EQ(seq, ntohl(tcp.seq_num));
}

/**
 * 测试 TCP 确认号设置和提取
 */
TEST_CASE(test_tcp_ack_number) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置确认号
    uint32_t ack = 0x87654321;
    tcp.ack_num = htonl(ack);
    
    // 验证确认号
    ASSERT_EQ(ack, ntohl(tcp.ack_num));
}

/**
 * 测试 TCP 序列号比较宏 - 小于
 */
TEST_CASE(test_tcp_seq_lt) {
    uint32_t seq1 = 100;
    uint32_t seq2 = 200;
    
    // seq1 < seq2
    ASSERT_TRUE(TCP_SEQ_LT(seq1, seq2));
    ASSERT_FALSE(TCP_SEQ_LT(seq2, seq1));
}

/**
 * 测试 TCP 序列号比较宏 - 小于等于
 */
TEST_CASE(test_tcp_seq_leq) {
    uint32_t seq1 = 100;
    uint32_t seq2 = 100;
    uint32_t seq3 = 200;
    
    // seq1 <= seq2
    ASSERT_TRUE(TCP_SEQ_LEQ(seq1, seq2));
    // seq1 <= seq3
    ASSERT_TRUE(TCP_SEQ_LEQ(seq1, seq3));
    // seq3 <= seq1 应为假
    ASSERT_FALSE(TCP_SEQ_LEQ(seq3, seq1));
}

/**
 * 测试 TCP 序列号比较宏 - 大于
 */
TEST_CASE(test_tcp_seq_gt) {
    uint32_t seq1 = 200;
    uint32_t seq2 = 100;
    
    // seq1 > seq2
    ASSERT_TRUE(TCP_SEQ_GT(seq1, seq2));
    ASSERT_FALSE(TCP_SEQ_GT(seq2, seq1));
}

/**
 * 测试 TCP 序列号比较宏 - 大于等于
 */
TEST_CASE(test_tcp_seq_geq) {
    uint32_t seq1 = 200;
    uint32_t seq2 = 200;
    uint32_t seq3 = 100;
    
    // seq1 >= seq2
    ASSERT_TRUE(TCP_SEQ_GEQ(seq1, seq2));
    // seq1 >= seq3
    ASSERT_TRUE(TCP_SEQ_GEQ(seq1, seq3));
    // seq3 >= seq1 应为假
    ASSERT_FALSE(TCP_SEQ_GEQ(seq3, seq1));
}

/**
 * 测试 TCP 序列号范围检查
 */
TEST_CASE(test_tcp_seq_between) {
    uint32_t seq = 150;
    uint32_t start = 100;
    uint32_t end = 200;
    
    // seq 在 [start, end] 范围内
    ASSERT_TRUE(TCP_SEQ_BETWEEN(seq, start, end));
    
    // 边界值
    ASSERT_TRUE(TCP_SEQ_BETWEEN(start, start, end));
    ASSERT_TRUE(TCP_SEQ_BETWEEN(end, start, end));
    
    // 超出范围
    ASSERT_FALSE(TCP_SEQ_BETWEEN(50, start, end));
    ASSERT_FALSE(TCP_SEQ_BETWEEN(250, start, end));
}

// ============================================================================
// 测试用例：TCP 数据偏移和头部长度提取
// ============================================================================

/**
 * 测试 TCP 头部长度提取 - 最小头部
 */
TEST_CASE(test_tcp_header_len_min) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置数据偏移为 5（20 字节）
    tcp.data_offset = (5 << 4);
    
    // 验证头部长度
    uint8_t len = tcp_header_len(&tcp);
    ASSERT_EQ(20, len);
}

/**
 * 测试 TCP 头部长度提取 - 带选项
 */
TEST_CASE(test_tcp_header_len_with_options) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置数据偏移为 8（32 字节，包含 12 字节选项）
    tcp.data_offset = (8 << 4);
    
    // 验证头部长度
    uint8_t len = tcp_header_len(&tcp);
    ASSERT_EQ(32, len);
}

/**
 * 测试 TCP 头部长度提取 - 最大头部
 */
TEST_CASE(test_tcp_header_len_max) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置数据偏移为 15（60 字节，最大值）
    tcp.data_offset = (15 << 4);
    
    // 验证头部长度
    uint8_t len = tcp_header_len(&tcp);
    ASSERT_EQ(60, len);
}

// ============================================================================
// 测试用例：TCP 窗口大小处理
// ============================================================================

/**
 * 测试 TCP 窗口大小设置和提取
 */
TEST_CASE(test_tcp_window_size) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置窗口大小
    uint16_t window = 4096;
    tcp.window = htons(window);
    
    // 验证窗口大小
    ASSERT_EQ(window, ntohs(tcp.window));
}

/**
 * 测试 TCP 窗口大小 - 最小值
 */
TEST_CASE(test_tcp_window_min) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置最小窗口大小
    tcp.window = htons(0);
    
    // 验证窗口大小
    ASSERT_EQ(0, ntohs(tcp.window));
}

/**
 * 测试 TCP 窗口大小 - 最大值
 */
TEST_CASE(test_tcp_window_max) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置最大窗口大小
    uint16_t max_window = 0xFFFF;
    tcp.window = htons(max_window);
    
    // 验证窗口大小
    ASSERT_EQ(max_window, ntohs(tcp.window));
}

// ============================================================================
// 测试用例：TCP 紧急指针处理
// ============================================================================

/**
 * 测试 TCP 紧急指针设置和提取
 */
TEST_CASE(test_tcp_urgent_pointer) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置紧急指针
    uint16_t urgent = 100;
    tcp.urgent_ptr = htons(urgent);
    tcp.flags = TCP_FLAG_URG;
    
    // 验证紧急指针
    ASSERT_EQ(urgent, ntohs(tcp.urgent_ptr));
    ASSERT_TRUE(tcp.flags & TCP_FLAG_URG);
}

/**
 * 测试 TCP 紧急指针 - 无 URG 标志
 */
TEST_CASE(test_tcp_urgent_pointer_no_flag) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置紧急指针但不设置 URG 标志
    tcp.urgent_ptr = htons(100);
    tcp.flags = 0;
    
    // 验证 URG 标志未设置
    ASSERT_FALSE(tcp.flags & TCP_FLAG_URG);
}

// ============================================================================
// 测试用例：TCP 校验和计算
// ============================================================================

/**
 * 测试 TCP 校验和计算 - 基本情况
 */
TEST_CASE(test_tcp_checksum_basic) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置头部字段
    tcp.src_port = htons(TEST_SRC_PORT);
    tcp.dst_port = htons(TEST_DST_PORT);
    tcp.seq_num = htonl(1000);
    tcp.ack_num = htonl(2000);
    tcp.data_offset = (5 << 4);
    tcp.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
    tcp.window = htons(4096);
    tcp.checksum = 0;
    tcp.urgent_ptr = 0;
    
    // 计算校验和
    uint16_t cs = tcp_checksum(TEST_SRC_IP, TEST_DST_IP, &tcp, TCP_HEADER_MIN_LEN);
    
    // 校验和应非零
    ASSERT_NE(0, cs);
}

/**
 * 测试 TCP 校验和对源 IP 的敏感性
 */
TEST_CASE(test_tcp_checksum_src_ip_sensitivity) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置头部字段
    tcp.src_port = htons(TEST_SRC_PORT);
    tcp.dst_port = htons(TEST_DST_PORT);
    tcp.seq_num = htonl(1000);
    tcp.ack_num = htonl(2000);
    tcp.data_offset = (5 << 4);
    tcp.flags = TCP_FLAG_SYN;
    tcp.window = htons(4096);
    tcp.checksum = 0;
    
    // 计算两个不同源 IP 的校验和
    uint32_t src_ip1 = TEST_SRC_IP;
    uint32_t src_ip2 = IP_ADDR(192, 168, 1, 101);
    
    uint16_t cs1 = tcp_checksum(src_ip1, TEST_DST_IP, &tcp, TCP_HEADER_MIN_LEN);
    uint16_t cs2 = tcp_checksum(src_ip2, TEST_DST_IP, &tcp, TCP_HEADER_MIN_LEN);
    
    // 校验和应不同
    ASSERT_NE(cs1, cs2);
}

/**
 * 测试 TCP 校验和对目的 IP 的敏感性
 */
TEST_CASE(test_tcp_checksum_dst_ip_sensitivity) {
    tcp_header_t tcp;
    memset(&tcp, 0, sizeof(tcp));
    
    // 设置头部字段
    tcp.src_port = htons(TEST_SRC_PORT);
    tcp.dst_port = htons(TEST_DST_PORT);
    tcp.seq_num = htonl(1000);
    tcp.ack_num = htonl(2000);
    tcp.data_offset = (5 << 4);
    tcp.flags = TCP_FLAG_SYN;
    tcp.window = htons(4096);
    tcp.checksum = 0;
    
    // 计算两个不同目的 IP 的校验和
    uint32_t dst_ip1 = TEST_DST_IP;
    uint32_t dst_ip2 = IP_ADDR(192, 168, 1, 2);
    
    uint16_t cs1 = tcp_checksum(TEST_SRC_IP, dst_ip1, &tcp, TCP_HEADER_MIN_LEN);
    uint16_t cs2 = tcp_checksum(TEST_SRC_IP, dst_ip2, &tcp, TCP_HEADER_MIN_LEN);
    
    // 校验和应不同
    ASSERT_NE(cs1, cs2);
}

/**
 * 测试 TCP 校验和对序列号的敏感性
 */
TEST_CASE(test_tcp_checksum_seq_sensitivity) {
    tcp_header_t tcp1, tcp2;
    memset(&tcp1, 0, sizeof(tcp1));
    memset(&tcp2, 0, sizeof(tcp2));
    
    // 设置相同的初始字段
    tcp1.src_port = htons(TEST_SRC_PORT);
    tcp1.dst_port = htons(TEST_DST_PORT);
    tcp1.seq_num = htonl(1000);
    tcp1.ack_num = htonl(2000);
    tcp1.data_offset = (5 << 4);
    tcp1.flags = TCP_FLAG_SYN;
    tcp1.window = htons(4096);
    tcp1.checksum = 0;
    
    memcpy(&tcp2, &tcp1, sizeof(tcp1));
    
    // 修改第二个头部的序列号
    tcp2.seq_num = htonl(1001);
    
    // 计算校验和
    uint16_t cs1 = tcp_checksum(TEST_SRC_IP, TEST_DST_IP, &tcp1, TCP_HEADER_MIN_LEN);
    uint16_t cs2 = tcp_checksum(TEST_SRC_IP, TEST_DST_IP, &tcp2, TCP_HEADER_MIN_LEN);
    
    // 校验和应不同
    ASSERT_NE(cs1, cs2);
}

/**
 * 测试 TCP 校验和对标志位的敏感性
 */
TEST_CASE(test_tcp_checksum_flags_sensitivity) {
    tcp_header_t tcp1, tcp2;
    memset(&tcp1, 0, sizeof(tcp1));
    memset(&tcp2, 0, sizeof(tcp2));
    
    // 设置相同的初始字段
    tcp1.src_port = htons(TEST_SRC_PORT);
    tcp1.dst_port = htons(TEST_DST_PORT);
    tcp1.seq_num = htonl(1000);
    tcp1.ack_num = htonl(2000);
    tcp1.data_offset = (5 << 4);
    tcp1.flags = TCP_FLAG_SYN;
    tcp1.window = htons(4096);
    tcp1.checksum = 0;
    
    memcpy(&tcp2, &tcp1, sizeof(tcp1));
    
    // 修改第二个头部的标志位
    tcp2.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
    
    // 计算校验和
    uint16_t cs1 = tcp_checksum(TEST_SRC_IP, TEST_DST_IP, &tcp1, TCP_HEADER_MIN_LEN);
    uint16_t cs2 = tcp_checksum(TEST_SRC_IP, TEST_DST_IP, &tcp2, TCP_HEADER_MIN_LEN);
    
    // 校验和应不同
    ASSERT_NE(cs1, cs2);
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(tcp_header_tests) {
    RUN_TEST(test_tcp_header_data_offset);
    RUN_TEST(test_tcp_header_basic_fields);
    RUN_TEST(test_tcp_header_size);
    RUN_TEST(test_tcp_header_packed);
}

TEST_SUITE(tcp_flags_tests) {
    RUN_TEST(test_tcp_flag_syn);
    RUN_TEST(test_tcp_flag_ack);
    RUN_TEST(test_tcp_flag_fin);
    RUN_TEST(test_tcp_flag_rst);
    RUN_TEST(test_tcp_flag_psh);
    RUN_TEST(test_tcp_flag_urg);
    RUN_TEST(test_tcp_flags_combination);
}

TEST_SUITE(tcp_sequence_tests) {
    RUN_TEST(test_tcp_sequence_number);
    RUN_TEST(test_tcp_ack_number);
    RUN_TEST(test_tcp_seq_lt);
    RUN_TEST(test_tcp_seq_leq);
    RUN_TEST(test_tcp_seq_gt);
    RUN_TEST(test_tcp_seq_geq);
    RUN_TEST(test_tcp_seq_between);
}

TEST_SUITE(tcp_header_len_tests) {
    RUN_TEST(test_tcp_header_len_min);
    RUN_TEST(test_tcp_header_len_with_options);
    RUN_TEST(test_tcp_header_len_max);
}

TEST_SUITE(tcp_window_tests) {
    RUN_TEST(test_tcp_window_size);
    RUN_TEST(test_tcp_window_min);
    RUN_TEST(test_tcp_window_max);
}

TEST_SUITE(tcp_urgent_tests) {
    RUN_TEST(test_tcp_urgent_pointer);
    RUN_TEST(test_tcp_urgent_pointer_no_flag);
}

TEST_SUITE(tcp_checksum_tests) {
    RUN_TEST(test_tcp_checksum_basic);
    RUN_TEST(test_tcp_checksum_src_ip_sensitivity);
    RUN_TEST(test_tcp_checksum_dst_ip_sensitivity);
    RUN_TEST(test_tcp_checksum_seq_sensitivity);
    RUN_TEST(test_tcp_checksum_flags_sensitivity);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_tcp_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(tcp_header_tests);
    RUN_SUITE(tcp_flags_tests);
    RUN_SUITE(tcp_sequence_tests);
    RUN_SUITE(tcp_header_len_tests);
    RUN_SUITE(tcp_window_tests);
    RUN_SUITE(tcp_urgent_tests);
    RUN_SUITE(tcp_checksum_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
