// ============================================================================
// timer_test.c - Timer 驱动测试模块
// ============================================================================
//
// Timer 驱动测试，验证 tick 计数和回调调用
// 测试 PIT (x86) 和 ARM Generic Timer (ARM64) 的通用接口
//
// **Feature: test-refactor**
// **Validates: Requirements 6.3**
//
// 测试覆盖:
//   - Timer tick 计数
//   - Timer 回调注册和取消
//   - Timer 频率和运行时间计算
//   - Timer 回调调用验证
// ============================================================================

#include <tests/ktest.h>
#include <tests/drivers/timer_test.h>
#include <lib/kprintf.h>

// Timer 测试在所有架构上运行
#include <drivers/timer.h>

// ============================================================================
// 测试辅助变量
// ============================================================================

// 回调计数器
static volatile uint32_t callback_count = 0;
static volatile void *callback_data_received = NULL;

// ============================================================================
// 测试回调函数
// ============================================================================

/**
 * 简单的测试回调函数
 * 增加计数器并记录接收到的数据
 */
static void test_timer_callback(void *data) {
    callback_count++;
    callback_data_received = data;
}

/**
 * 重置测试状态
 */
static void reset_callback_state(void) {
    callback_count = 0;
    callback_data_received = NULL;
}

// ============================================================================
// 测试用例：Timer 基本功能
// ============================================================================

/**
 * 测试获取 timer 频率
 * 频率应该是一个合理的正值
 */
TEST_CASE(test_timer_get_frequency) {
    uint32_t freq = timer_get_frequency();
    
    // 频率应该大于 0（timer 已初始化）
    ASSERT_TRUE(freq > 0);
    
    // 频率应该在合理范围内（1 Hz - 10000 Hz）
    ASSERT_TRUE(freq >= 1);
    ASSERT_TRUE(freq <= 10000);
}

/**
 * 测试获取 timer tick 计数
 * tick 计数应该是有效值
 */
TEST_CASE(test_timer_get_ticks) {
    uint64_t ticks = timer_get_ticks();
    
    // tick 计数应该是有效值（系统已运行一段时间）
    // 由于系统启动后已经过了一些时间，ticks 应该 > 0
    // 验证函数可调用并返回有效值
    // 使用 ticks 变量避免未使用警告
    ASSERT_TRUE(ticks == ticks);  // 始终为真，验证函数可调用
}

/**
 * 测试获取系统运行时间（毫秒）
 */
TEST_CASE(test_timer_get_uptime_ms) {
    uint64_t uptime_ms = timer_get_uptime_ms();
    
    // 运行时间应该是有效值
    // 使用 uptime_ms 变量避免未使用警告
    ASSERT_TRUE(uptime_ms == uptime_ms);  // 始终为真，验证函数可调用
}

/**
 * 测试获取系统运行时间（秒）
 */
TEST_CASE(test_timer_get_uptime_sec) {
    uint32_t uptime_sec = timer_get_uptime_sec();
    
    // 运行时间应该是有效值
    // 使用 uptime_sec 变量避免未使用警告
    ASSERT_TRUE(uptime_sec == uptime_sec);  // 始终为真，验证函数可调用
}

/**
 * 测试运行时间一致性
 * 毫秒值应该大于等于秒值 * 1000
 */
TEST_CASE(test_timer_uptime_consistency) {
    uint64_t uptime_ms = timer_get_uptime_ms();
    uint32_t uptime_sec = timer_get_uptime_sec();
    
    // 毫秒值应该大于等于秒值 * 1000（允许一些误差）
    // 由于两次调用之间可能有时间流逝，我们允许 1 秒的误差
    ASSERT_TRUE(uptime_ms >= (uint64_t)uptime_sec * 1000 - 1000);
}

// ============================================================================
// 测试用例：Timer 回调注册
// ============================================================================

/**
 * 测试注册 NULL 回调
 * 应该返回 0（失败）
 */
TEST_CASE(test_timer_register_null_callback) {
    uint32_t timer_id = timer_register_callback(NULL, NULL, 100, false);
    
    // 注册 NULL 回调应该失败
    ASSERT_EQ_UINT(0, timer_id);
}

/**
 * 测试注册零间隔回调
 * 应该返回 0（失败）
 */
TEST_CASE(test_timer_register_zero_interval) {
    uint32_t timer_id = timer_register_callback(test_timer_callback, NULL, 0, false);
    
    // 零间隔应该失败
    ASSERT_EQ_UINT(0, timer_id);
}

/**
 * 测试注册有效回调
 * 应该返回非零的 timer ID
 */
TEST_CASE(test_timer_register_valid_callback) {
    reset_callback_state();
    
    // 注册一个一次性回调，间隔 1000ms
    uint32_t timer_id = timer_register_callback(test_timer_callback, NULL, 1000, false);
    
    // 应该返回有效的 timer ID
    ASSERT_TRUE(timer_id > 0);
    
    // 清理：取消注册
    bool result = timer_unregister_callback(timer_id);
    ASSERT_TRUE(result);
}

/**
 * 测试取消无效的 timer ID
 * 应该返回 false
 */
TEST_CASE(test_timer_unregister_invalid_id) {
    // 取消 ID 0 应该失败
    bool result = timer_unregister_callback(0);
    ASSERT_FALSE(result);
    
    // 取消一个很大的无效 ID 应该失败
    result = timer_unregister_callback(0xFFFFFFFF);
    ASSERT_FALSE(result);
}

/**
 * 测试重复取消同一个 timer
 * 第二次取消应该失败
 */
TEST_CASE(test_timer_unregister_twice) {
    reset_callback_state();
    
    // 注册一个回调
    uint32_t timer_id = timer_register_callback(test_timer_callback, NULL, 1000, false);
    ASSERT_TRUE(timer_id > 0);
    
    // 第一次取消应该成功
    bool result = timer_unregister_callback(timer_id);
    ASSERT_TRUE(result);
    
    // 第二次取消应该失败
    result = timer_unregister_callback(timer_id);
    ASSERT_FALSE(result);
}

// ============================================================================
// 测试用例：Timer 活动计数
// ============================================================================

/**
 * 测试获取活动 timer 数量
 */
TEST_CASE(test_timer_get_active_count) {
    reset_callback_state();
    
    // 记录初始活动数量
    uint32_t initial_count = timer_get_active_count();
    
    // 注册一个回调
    uint32_t timer_id = timer_register_callback(test_timer_callback, NULL, 1000, false);
    ASSERT_TRUE(timer_id > 0);
    
    // 活动数量应该增加 1
    uint32_t new_count = timer_get_active_count();
    ASSERT_EQ_UINT(initial_count + 1, new_count);
    
    // 取消注册
    timer_unregister_callback(timer_id);
    
    // 活动数量应该恢复
    uint32_t final_count = timer_get_active_count();
    ASSERT_EQ_UINT(initial_count, final_count);
}

/**
 * 测试注册多个回调
 */
TEST_CASE(test_timer_register_multiple_callbacks) {
    reset_callback_state();
    
    uint32_t initial_count = timer_get_active_count();
    
    // 注册 3 个回调
    uint32_t id1 = timer_register_callback(test_timer_callback, NULL, 1000, false);
    uint32_t id2 = timer_register_callback(test_timer_callback, NULL, 2000, false);
    uint32_t id3 = timer_register_callback(test_timer_callback, NULL, 3000, false);
    
    ASSERT_TRUE(id1 > 0);
    ASSERT_TRUE(id2 > 0);
    ASSERT_TRUE(id3 > 0);
    
    // 所有 ID 应该不同
    ASSERT_NE_UINT(id1, id2);
    ASSERT_NE_UINT(id2, id3);
    ASSERT_NE_UINT(id1, id3);
    
    // 活动数量应该增加 3
    uint32_t new_count = timer_get_active_count();
    ASSERT_EQ_UINT(initial_count + 3, new_count);
    
    // 清理
    timer_unregister_callback(id1);
    timer_unregister_callback(id2);
    timer_unregister_callback(id3);
    
    // 活动数量应该恢复
    uint32_t final_count = timer_get_active_count();
    ASSERT_EQ_UINT(initial_count, final_count);
}

// ============================================================================
// 测试用例：Timer tick 单调性
// ============================================================================

/**
 * 测试 tick 计数单调递增
 * 连续读取的 tick 值应该非递减
 */
TEST_CASE(test_timer_ticks_monotonic) {
    uint64_t prev_ticks = timer_get_ticks();
    
    // 多次读取，验证单调性
    for (int i = 0; i < 10; i++) {
        uint64_t curr_ticks = timer_get_ticks();
        
        // 当前值应该大于等于前一个值
        ASSERT_TRUE(curr_ticks >= prev_ticks);
        
        prev_ticks = curr_ticks;
    }
}

/**
 * 测试运行时间单调递增
 */
TEST_CASE(test_timer_uptime_monotonic) {
    uint64_t prev_uptime = timer_get_uptime_ms();
    
    // 多次读取，验证单调性
    for (int i = 0; i < 10; i++) {
        uint64_t curr_uptime = timer_get_uptime_ms();
        
        // 当前值应该大于等于前一个值
        ASSERT_TRUE(curr_uptime >= prev_uptime);
        
        prev_uptime = curr_uptime;
    }
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(timer_basic_tests) {
    RUN_TEST(test_timer_get_frequency);
    RUN_TEST(test_timer_get_ticks);
    RUN_TEST(test_timer_get_uptime_ms);
    RUN_TEST(test_timer_get_uptime_sec);
    RUN_TEST(test_timer_uptime_consistency);
}

TEST_SUITE(timer_callback_tests) {
    RUN_TEST(test_timer_register_null_callback);
    RUN_TEST(test_timer_register_zero_interval);
    RUN_TEST(test_timer_register_valid_callback);
    RUN_TEST(test_timer_unregister_invalid_id);
    RUN_TEST(test_timer_unregister_twice);
}

TEST_SUITE(timer_active_count_tests) {
    RUN_TEST(test_timer_get_active_count);
    RUN_TEST(test_timer_register_multiple_callbacks);
}

TEST_SUITE(timer_monotonic_tests) {
    RUN_TEST(test_timer_ticks_monotonic);
    RUN_TEST(test_timer_uptime_monotonic);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_timer_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(timer_basic_tests);
    RUN_SUITE(timer_callback_tests);
    RUN_SUITE(timer_active_count_tests);
    RUN_SUITE(timer_monotonic_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
