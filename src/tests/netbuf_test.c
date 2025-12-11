// ============================================================================
// netbuf_test.c - 网络缓冲区模块单元测试
// ============================================================================
//
// 测试网络缓冲区（netbuf）的分配、释放和数据操作
// 类似 Linux 的 sk_buff 结构
//
// **Feature: test-refactor**
// **Validates: Requirements 5.5**
//
// 测试覆盖:
//   - netbuf_alloc(): 缓冲区分配
//   - netbuf_free(): 缓冲区释放
//   - netbuf_push(): 在数据前添加空间
//   - netbuf_pull(): 从数据前移除空间
//   - netbuf_put(): 在数据后添加空间
//   - netbuf_clone(): 缓冲区复制
//   - netbuf_reset(): 缓冲区重置
//   - netbuf_headroom(): 获取头部空间
//   - netbuf_tailroom(): 获取尾部空间
// ============================================================================

#include <tests/ktest.h>
#include <tests/netbuf_test.h>
#include <net/netbuf.h>
#include <lib/string.h>

// ============================================================================
// 测试用例：netbuf_alloc() 缓冲区分配
// ============================================================================

/**
 * 测试基本缓冲区分配
 * 分配应返回有效的缓冲区指针
 */
TEST_CASE(test_netbuf_alloc_basic) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    ASSERT_NOT_NULL(buf->head);
    ASSERT_NOT_NULL(buf->data);
    ASSERT_NOT_NULL(buf->tail);
    ASSERT_NOT_NULL(buf->end);
    ASSERT_EQ_UINT(0, buf->len);
    netbuf_free(buf);
}

/**
 * 测试分配后的初始状态
 * data 应指向 head + NETBUF_HEADROOM
 */
TEST_CASE(test_netbuf_alloc_initial_state) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // data 应该在 head 之后 NETBUF_HEADROOM 字节处
    ASSERT_EQ_PTR(buf->head + NETBUF_HEADROOM, buf->data);
    
    // tail 应该等于 data（初始无数据）
    ASSERT_EQ_PTR(buf->data, buf->tail);
    
    // len 应该为 0
    ASSERT_EQ_UINT(0, buf->len);
    
    netbuf_free(buf);
}


/**
 * 测试零大小分配
 * 应该仍然分配带有 headroom 的缓冲区
 */
TEST_CASE(test_netbuf_alloc_zero_size) {
    netbuf_t *buf = netbuf_alloc(0);
    ASSERT_NOT_NULL(buf);
    ASSERT_NOT_NULL(buf->head);
    ASSERT_EQ_UINT(0, buf->len);
    netbuf_free(buf);
}

/**
 * 测试大缓冲区分配
 * 超过 NETBUF_MAX_SIZE 应被截断
 */
TEST_CASE(test_netbuf_alloc_large_size) {
    netbuf_t *buf = netbuf_alloc(NETBUF_MAX_SIZE + 1000);
    ASSERT_NOT_NULL(buf);
    // total_size 应该被限制在 NETBUF_MAX_SIZE
    ASSERT_TRUE(buf->total_size <= NETBUF_MAX_SIZE);
    netbuf_free(buf);
}

/**
 * 测试协议头指针初始化
 * 所有协议头指针应初始化为 NULL
 */
TEST_CASE(test_netbuf_alloc_headers_null) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    ASSERT_NULL(buf->mac_header);
    ASSERT_NULL(buf->network_header);
    ASSERT_NULL(buf->transport_header);
    ASSERT_NULL(buf->dev);
    ASSERT_NULL(buf->next);
    netbuf_free(buf);
}

// ============================================================================
// 测试用例：netbuf_free() 缓冲区释放
// ============================================================================

/**
 * 测试释放 NULL 缓冲区
 * 应该安全处理 NULL 指针
 */
TEST_CASE(test_netbuf_free_null) {
    // 不应崩溃
    netbuf_free(NULL);
    ASSERT_TRUE(true);
}

/**
 * 测试正常释放
 * 分配后释放应该成功
 */
TEST_CASE(test_netbuf_free_normal) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    netbuf_free(buf);
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

// ============================================================================
// 测试用例：netbuf_put() 在数据后添加空间
// ============================================================================

/**
 * 测试基本 put 操作
 * 应该增加 len 并移动 tail
 */
TEST_CASE(test_netbuf_put_basic) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    uint8_t *old_tail = buf->tail;
    uint8_t *result = netbuf_put(buf, 64);
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_PTR(old_tail, result);
    ASSERT_EQ_UINT(64, buf->len);
    ASSERT_EQ_PTR(old_tail + 64, buf->tail);
    
    netbuf_free(buf);
}

/**
 * 测试多次 put 操作
 * 应该累积增加 len
 */
TEST_CASE(test_netbuf_put_multiple) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    netbuf_put(buf, 32);
    ASSERT_EQ_UINT(32, buf->len);
    
    netbuf_put(buf, 32);
    ASSERT_EQ_UINT(64, buf->len);
    
    netbuf_put(buf, 32);
    ASSERT_EQ_UINT(96, buf->len);
    
    netbuf_free(buf);
}

/**
 * 测试 put 超出 tailroom
 * 应该返回 NULL
 */
TEST_CASE(test_netbuf_put_overflow) {
    netbuf_t *buf = netbuf_alloc(64);
    ASSERT_NOT_NULL(buf);
    
    uint32_t tailroom = netbuf_tailroom(buf);
    
    // 尝试 put 超过 tailroom 的数据
    uint8_t *result = netbuf_put(buf, tailroom + 100);
    ASSERT_NULL(result);
    ASSERT_EQ_UINT(0, buf->len);  // len 不应改变
    
    netbuf_free(buf);
}

// ============================================================================
// 测试用例：netbuf_push() 在数据前添加空间
// ============================================================================

/**
 * 测试基本 push 操作
 * 应该减少 data 指针并增加 len
 */
TEST_CASE(test_netbuf_push_basic) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 先 put 一些数据
    netbuf_put(buf, 64);
    uint8_t *old_data = buf->data;
    uint32_t old_len = buf->len;
    
    // push 添加头部空间
    uint8_t *result = netbuf_push(buf, 14);  // 以太网头大小
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_PTR(old_data - 14, result);
    ASSERT_EQ_PTR(result, buf->data);
    ASSERT_EQ_UINT(old_len + 14, buf->len);
    
    netbuf_free(buf);
}

/**
 * 测试 push 超出 headroom
 * 应该返回 NULL
 */
TEST_CASE(test_netbuf_push_overflow) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    uint32_t headroom = netbuf_headroom(buf);
    
    // 尝试 push 超过 headroom 的数据
    uint8_t *result = netbuf_push(buf, headroom + 100);
    ASSERT_NULL(result);
    ASSERT_EQ_UINT(0, buf->len);  // len 不应改变
    
    netbuf_free(buf);
}

/**
 * 测试多次 push 操作
 * 应该累积减少 data 指针
 */
TEST_CASE(test_netbuf_push_multiple) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 先 put 一些数据
    netbuf_put(buf, 64);
    
    // 多次 push
    netbuf_push(buf, 20);  // IP 头
    ASSERT_EQ_UINT(84, buf->len);
    
    netbuf_push(buf, 14);  // 以太网头
    ASSERT_EQ_UINT(98, buf->len);
    
    netbuf_free(buf);
}

// ============================================================================
// 测试用例：netbuf_pull() 从数据前移除空间
// ============================================================================

/**
 * 测试基本 pull 操作
 * 应该增加 data 指针并减少 len
 */
TEST_CASE(test_netbuf_pull_basic) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 先 put 一些数据
    netbuf_put(buf, 64);
    uint8_t *old_data = buf->data;
    
    // pull 移除头部
    uint8_t *result = netbuf_pull(buf, 14);
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_PTR(old_data + 14, result);
    ASSERT_EQ_PTR(result, buf->data);
    ASSERT_EQ_UINT(50, buf->len);
    
    netbuf_free(buf);
}

/**
 * 测试 pull 超出数据长度
 * 应该返回 NULL
 */
TEST_CASE(test_netbuf_pull_overflow) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 先 put 一些数据
    netbuf_put(buf, 32);
    
    // 尝试 pull 超过数据长度
    uint8_t *result = netbuf_pull(buf, 64);
    ASSERT_NULL(result);
    ASSERT_EQ_UINT(32, buf->len);  // len 不应改变
    
    netbuf_free(buf);
}


// ============================================================================
// 测试用例：netbuf_clone() 缓冲区复制
// ============================================================================

/**
 * 测试基本克隆操作
 * 克隆应该创建独立的副本
 */
TEST_CASE(test_netbuf_clone_basic) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 添加一些数据
    uint8_t *data = netbuf_put(buf, 64);
    for (int i = 0; i < 64; i++) {
        data[i] = (uint8_t)i;
    }
    
    // 克隆
    netbuf_t *clone = netbuf_clone(buf);
    ASSERT_NOT_NULL(clone);
    
    // 验证克隆的属性
    ASSERT_EQ_UINT(buf->len, clone->len);
    
    // 验证数据内容相同
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ_UINT(buf->data[i], clone->data[i]);
    }
    
    // 验证是独立的缓冲区
    ASSERT_NE_PTR(buf->head, clone->head);
    ASSERT_NE_PTR(buf->data, clone->data);
    
    netbuf_free(buf);
    netbuf_free(clone);
}

/**
 * 测试克隆 NULL 缓冲区
 * 应该返回 NULL
 */
TEST_CASE(test_netbuf_clone_null) {
    netbuf_t *clone = netbuf_clone(NULL);
    ASSERT_NULL(clone);
}

/**
 * 测试克隆后修改不影响原缓冲区
 * 数据应该是独立的
 */
TEST_CASE(test_netbuf_clone_independence) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 添加数据
    uint8_t *data = netbuf_put(buf, 32);
    memset(data, 0xAA, 32);
    
    // 克隆
    netbuf_t *clone = netbuf_clone(buf);
    ASSERT_NOT_NULL(clone);
    
    // 修改克隆的数据
    memset(clone->data, 0xBB, 32);
    
    // 验证原缓冲区数据未改变
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ_UINT(0xAA, buf->data[i]);
    }
    
    netbuf_free(buf);
    netbuf_free(clone);
}

// ============================================================================
// 测试用例：netbuf_reset() 缓冲区重置
// ============================================================================

/**
 * 测试基本重置操作
 * 应该恢复到初始状态
 */
TEST_CASE(test_netbuf_reset_basic) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 添加一些数据和头部
    netbuf_put(buf, 64);
    netbuf_push(buf, 14);
    buf->mac_header = buf->data;
    buf->network_header = buf->data + 14;
    
    // 重置
    netbuf_reset(buf);
    
    // 验证重置后的状态
    ASSERT_EQ_PTR(buf->head + NETBUF_HEADROOM, buf->data);
    ASSERT_EQ_PTR(buf->data, buf->tail);
    ASSERT_EQ_UINT(0, buf->len);
    ASSERT_NULL(buf->mac_header);
    ASSERT_NULL(buf->network_header);
    ASSERT_NULL(buf->transport_header);
    
    netbuf_free(buf);
}

/**
 * 测试重置 NULL 缓冲区
 * 应该安全处理
 */
TEST_CASE(test_netbuf_reset_null) {
    // 不应崩溃
    netbuf_reset(NULL);
    ASSERT_TRUE(true);
}

// ============================================================================
// 测试用例：netbuf_headroom() 和 netbuf_tailroom()
// ============================================================================

/**
 * 测试初始 headroom
 * 应该等于 NETBUF_HEADROOM
 */
TEST_CASE(test_netbuf_headroom_initial) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    uint32_t headroom = netbuf_headroom(buf);
    ASSERT_EQ_UINT(NETBUF_HEADROOM, headroom);
    
    netbuf_free(buf);
}

/**
 * 测试 push 后 headroom 减少
 */
TEST_CASE(test_netbuf_headroom_after_push) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    uint32_t initial_headroom = netbuf_headroom(buf);
    
    // push 一些数据
    netbuf_push(buf, 20);
    
    uint32_t new_headroom = netbuf_headroom(buf);
    ASSERT_EQ_UINT(initial_headroom - 20, new_headroom);
    
    netbuf_free(buf);
}

/**
 * 测试初始 tailroom
 */
TEST_CASE(test_netbuf_tailroom_initial) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    uint32_t tailroom = netbuf_tailroom(buf);
    // tailroom 应该是 total_size - NETBUF_HEADROOM
    ASSERT_TRUE(tailroom > 0);
    
    netbuf_free(buf);
}

/**
 * 测试 put 后 tailroom 减少
 */
TEST_CASE(test_netbuf_tailroom_after_put) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    uint32_t initial_tailroom = netbuf_tailroom(buf);
    
    // put 一些数据
    netbuf_put(buf, 64);
    
    uint32_t new_tailroom = netbuf_tailroom(buf);
    ASSERT_EQ_UINT(initial_tailroom - 64, new_tailroom);
    
    netbuf_free(buf);
}

/**
 * 测试 NULL 缓冲区的 headroom/tailroom
 */
TEST_CASE(test_netbuf_room_null) {
    ASSERT_EQ_UINT(0, netbuf_headroom(NULL));
    ASSERT_EQ_UINT(0, netbuf_tailroom(NULL));
}

// ============================================================================
// 测试用例：数据读写完整性
// ============================================================================

/**
 * 测试数据写入和读取
 * 写入的数据应该能正确读取
 */
TEST_CASE(test_netbuf_data_integrity) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 写入测试数据
    uint8_t *data = netbuf_put(buf, 128);
    ASSERT_NOT_NULL(data);
    
    for (int i = 0; i < 128; i++) {
        data[i] = (uint8_t)(i ^ 0x55);
    }
    
    // 验证数据完整性
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ_UINT((uint8_t)(i ^ 0x55), buf->data[i]);
    }
    
    netbuf_free(buf);
}

/**
 * 测试模拟网络包构建
 * 模拟构建一个以太网帧
 */
TEST_CASE(test_netbuf_packet_build) {
    netbuf_t *buf = netbuf_alloc(1500);  // MTU 大小
    ASSERT_NOT_NULL(buf);
    
    // 1. 添加应用数据（payload）
    uint8_t *payload = netbuf_put(buf, 100);
    ASSERT_NOT_NULL(payload);
    memset(payload, 'A', 100);
    ASSERT_EQ_UINT(100, buf->len);
    
    // 2. 添加 UDP 头（8 字节）
    uint8_t *udp_hdr = netbuf_push(buf, 8);
    ASSERT_NOT_NULL(udp_hdr);
    buf->transport_header = udp_hdr;
    ASSERT_EQ_UINT(108, buf->len);
    
    // 3. 添加 IP 头（20 字节）
    uint8_t *ip_hdr = netbuf_push(buf, 20);
    ASSERT_NOT_NULL(ip_hdr);
    buf->network_header = ip_hdr;
    ASSERT_EQ_UINT(128, buf->len);
    
    // 4. 添加以太网头（14 字节）
    uint8_t *eth_hdr = netbuf_push(buf, 14);
    ASSERT_NOT_NULL(eth_hdr);
    buf->mac_header = eth_hdr;
    ASSERT_EQ_UINT(142, buf->len);
    
    // 验证头部指针关系
    ASSERT_EQ_PTR(buf->data, buf->mac_header);
    ASSERT_EQ_PTR(buf->data + 14, buf->network_header);
    ASSERT_EQ_PTR(buf->data + 34, buf->transport_header);
    
    netbuf_free(buf);
}

/**
 * 测试模拟网络包解析
 * 模拟解析一个以太网帧
 */
TEST_CASE(test_netbuf_packet_parse) {
    netbuf_t *buf = netbuf_alloc(256);
    ASSERT_NOT_NULL(buf);
    
    // 模拟接收到的数据包（142 字节）
    uint8_t *data = netbuf_put(buf, 142);
    ASSERT_NOT_NULL(data);
    
    // 1. 解析以太网头
    buf->mac_header = buf->data;
    netbuf_pull(buf, 14);
    ASSERT_EQ_UINT(128, buf->len);
    
    // 2. 解析 IP 头
    buf->network_header = buf->data;
    netbuf_pull(buf, 20);
    ASSERT_EQ_UINT(108, buf->len);
    
    // 3. 解析 UDP 头
    buf->transport_header = buf->data;
    netbuf_pull(buf, 8);
    ASSERT_EQ_UINT(100, buf->len);
    
    // 现在 buf->data 指向 payload
    // 验证头部指针仍然有效
    ASSERT_NOT_NULL(buf->mac_header);
    ASSERT_NOT_NULL(buf->network_header);
    ASSERT_NOT_NULL(buf->transport_header);
    
    netbuf_free(buf);
}


// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(netbuf_alloc_tests) {
    RUN_TEST(test_netbuf_alloc_basic);
    RUN_TEST(test_netbuf_alloc_initial_state);
    RUN_TEST(test_netbuf_alloc_zero_size);
    RUN_TEST(test_netbuf_alloc_large_size);
    RUN_TEST(test_netbuf_alloc_headers_null);
}

TEST_SUITE(netbuf_free_tests) {
    RUN_TEST(test_netbuf_free_null);
    RUN_TEST(test_netbuf_free_normal);
}

TEST_SUITE(netbuf_put_tests) {
    RUN_TEST(test_netbuf_put_basic);
    RUN_TEST(test_netbuf_put_multiple);
    RUN_TEST(test_netbuf_put_overflow);
}

TEST_SUITE(netbuf_push_tests) {
    RUN_TEST(test_netbuf_push_basic);
    RUN_TEST(test_netbuf_push_overflow);
    RUN_TEST(test_netbuf_push_multiple);
}

TEST_SUITE(netbuf_pull_tests) {
    RUN_TEST(test_netbuf_pull_basic);
    RUN_TEST(test_netbuf_pull_overflow);
}

TEST_SUITE(netbuf_clone_tests) {
    RUN_TEST(test_netbuf_clone_basic);
    RUN_TEST(test_netbuf_clone_null);
    RUN_TEST(test_netbuf_clone_independence);
}

TEST_SUITE(netbuf_reset_tests) {
    RUN_TEST(test_netbuf_reset_basic);
    RUN_TEST(test_netbuf_reset_null);
}

TEST_SUITE(netbuf_room_tests) {
    RUN_TEST(test_netbuf_headroom_initial);
    RUN_TEST(test_netbuf_headroom_after_push);
    RUN_TEST(test_netbuf_tailroom_initial);
    RUN_TEST(test_netbuf_tailroom_after_put);
    RUN_TEST(test_netbuf_room_null);
}

TEST_SUITE(netbuf_data_tests) {
    RUN_TEST(test_netbuf_data_integrity);
    RUN_TEST(test_netbuf_packet_build);
    RUN_TEST(test_netbuf_packet_parse);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_netbuf_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(netbuf_alloc_tests);
    RUN_SUITE(netbuf_free_tests);
    RUN_SUITE(netbuf_put_tests);
    RUN_SUITE(netbuf_push_tests);
    RUN_SUITE(netbuf_pull_tests);
    RUN_SUITE(netbuf_clone_tests);
    RUN_SUITE(netbuf_reset_tests);
    RUN_SUITE(netbuf_room_tests);
    RUN_SUITE(netbuf_data_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
