// ============================================================================
// klog_test.c - klog 模块单元测试
// ============================================================================
// 
// 测试内核日志系统功能
// ============================================================================

#include <tests/ktest.h>
#include <tests/lib/klog_test.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/serial.h>

// ============================================================================
// 测试用例：日志等级设置和获取
// ============================================================================

TEST_CASE(test_klog_default_level) {
    // 确保设置为默认等级
    klog_set_level(LOG_INFO);
    
    // 测试默认日志等级应该是 LOG_INFO
    log_level_t level = klog_get_level();
    ASSERT_EQ(LOG_INFO, level);
}

TEST_CASE(test_klog_set_level_debug) {
    klog_set_level(LOG_DEBUG);
    log_level_t level = klog_get_level();
    ASSERT_EQ(LOG_DEBUG, level);
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_set_level_info) {
    klog_set_level(LOG_INFO);
    log_level_t level = klog_get_level();
    ASSERT_EQ(LOG_INFO, level);
}

TEST_CASE(test_klog_set_level_warn) {
    klog_set_level(LOG_WARN);
    log_level_t level = klog_get_level();
    ASSERT_EQ(LOG_WARN, level);
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_set_level_error) {
    klog_set_level(LOG_ERROR);
    log_level_t level = klog_get_level();
    ASSERT_EQ(LOG_ERROR, level);
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_level_sequence) {
    // 测试按顺序设置各个等级
    log_level_t levels[] = {LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR};
    
    for (int i = 0; i < 4; i++) {
        klog_set_level(levels[i]);
        log_level_t current = klog_get_level();
        ASSERT_EQ(levels[i], current);
    }
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

// ============================================================================
// 测试用例：基本日志输出
// ============================================================================

TEST_CASE(test_klog_debug_message) {
    // 设置为 DEBUG 级别以确保消息可以输出
    klog_set_level(LOG_DEBUG);
    
    // 测试 DEBUG 级别日志输出不会崩溃
    klog(LOG_DEBUG, "Debug message: %s\n", "test");
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_info_message) {
    // 测试 INFO 级别日志输出
    klog(LOG_INFO, "Info message: %d\n", 42);
}

TEST_CASE(test_klog_warn_message) {
    // 测试 WARN 级别日志输出
    klog(LOG_WARN, "Warning message: %s\n", "caution");
}

TEST_CASE(test_klog_error_message) {
    // 测试 ERROR 级别日志输出
    klog(LOG_ERROR, "Error message: code=%d\n", 1);
}

TEST_CASE(test_klog_plain_string) {
    // 测试不带格式化的简单字符串
    klog(LOG_INFO, "Plain string without format\n");
}

TEST_CASE(test_klog_empty_message) {
    // 测试空消息
    klog(LOG_INFO, "\n");
}

TEST_CASE(test_klog_multiple_arguments) {
    // 测试多参数格式化
    klog(LOG_INFO, "Multiple args: %s, %d, %x\n", "test", 123, 0xABC);
}

// ============================================================================
// 测试用例：日志过滤功能
// ============================================================================

TEST_CASE(test_klog_filter_debug_when_info) {
    // 设置日志等级为 INFO
    klog_set_level(LOG_INFO);
    
    // DEBUG 级别的日志应该被过滤（不输出）
    // 这个测试主要验证不会崩溃
    klog(LOG_DEBUG, "This DEBUG message should be filtered\n");
    
    // INFO 级别的日志应该输出
    klog(LOG_INFO, "This INFO message should be visible\n");
}

TEST_CASE(test_klog_filter_debug_info_when_warn) {
    // 设置日志等级为 WARN
    klog_set_level(LOG_WARN);
    
    // DEBUG 和 INFO 级别的日志应该被过滤
    klog(LOG_DEBUG, "Filtered DEBUG\n");
    klog(LOG_INFO, "Filtered INFO\n");
    
    // WARN 级别的日志应该输出
    klog(LOG_WARN, "Visible WARN\n");
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_filter_all_except_error) {
    // 设置日志等级为 ERROR
    klog_set_level(LOG_ERROR);
    
    // 只有 ERROR 级别的日志应该输出
    klog(LOG_DEBUG, "Filtered DEBUG\n");
    klog(LOG_INFO, "Filtered INFO\n");
    klog(LOG_WARN, "Filtered WARN\n");
    klog(LOG_ERROR, "Visible ERROR\n");
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_show_all_when_debug) {
    // 设置日志等级为 DEBUG
    klog_set_level(LOG_DEBUG);
    
    // 所有级别的日志都应该输出
    klog(LOG_DEBUG, "Visible DEBUG\n");
    klog(LOG_INFO, "Visible INFO\n");
    klog(LOG_WARN, "Visible WARN\n");
    klog(LOG_ERROR, "Visible ERROR\n");
    
    // 恢复默认等级
    klog_set_level(LOG_INFO);
}

// ============================================================================
// 测试用例：便捷宏
// ============================================================================

TEST_CASE(test_klog_debug_macro) {
    klog_set_level(LOG_DEBUG);
    LOG_DEBUG_MSG("Debug macro test: %d\n", 1);
    klog_set_level(LOG_INFO);
}

TEST_CASE(test_klog_info_macro) {
    LOG_INFO_MSG("Info macro test: %s\n", "working");
}

TEST_CASE(test_klog_warn_macro) {
    LOG_WARN_MSG("Warn macro test: warning=%d\n", 2);
}

TEST_CASE(test_klog_error_macro) {
    LOG_ERROR_MSG("Error macro test: error=%d\n", 3);
}

TEST_CASE(test_klog_all_macros) {
    klog_set_level(LOG_DEBUG);
    
    LOG_DEBUG_MSG("Using all macros\n");
    LOG_INFO_MSG("Testing macros\n");
    LOG_WARN_MSG("Macro warning\n");
    LOG_ERROR_MSG("Macro error\n");
    
    klog_set_level(LOG_INFO);
}

// ============================================================================
// 测试用例：格式化字符串
// ============================================================================

TEST_CASE(test_klog_format_string) {
    klog(LOG_INFO, "String: %s\n", "CastorOS");
}

TEST_CASE(test_klog_format_integer) {
    klog(LOG_INFO, "Integer: %d, %d\n", 42, -42);
}

TEST_CASE(test_klog_format_unsigned) {
    klog(LOG_INFO, "Unsigned: %u\n", 4294967295U);
}

TEST_CASE(test_klog_format_hex) {
    klog(LOG_INFO, "Hex: %x, %X\n", 0xDEADBEEF, 0xCAFEBABE);
}

TEST_CASE(test_klog_format_pointer) {
    int x = 123;
    klog(LOG_INFO, "Pointer: %p\n", (void*)&x);
}

TEST_CASE(test_klog_format_char) {
    klog(LOG_INFO, "Char: %c\n", 'A');
}

TEST_CASE(test_klog_format_mixed) {
    klog(LOG_INFO, "Mixed: %s=%d, hex=%x, char=%c\n", 
         "value", 100, 0xFF, 'X');
}

// ============================================================================
// 测试用例：边界情况
// ============================================================================

TEST_CASE(test_klog_long_message) {
    // 测试长消息
    klog(LOG_INFO, "Long message: This is a very long log message that contains "
                   "multiple words and various formatting to test if the logging "
                   "system can handle longer strings without issues: %d %s %x\n",
                   12345, "test", 0xABCDEF);
}

TEST_CASE(test_klog_consecutive_logs) {
    // 测试连续多次日志输出
    for (int i = 0; i < 5; i++) {
        klog(LOG_INFO, "Consecutive log #%d\n", i);
    }
}

TEST_CASE(test_klog_different_levels_consecutive) {
    // 测试连续输出不同等级的日志
    klog(LOG_INFO, "First INFO\n");
    klog(LOG_WARN, "Then WARN\n");
    klog(LOG_ERROR, "Then ERROR\n");
    klog(LOG_INFO, "Back to INFO\n");
}

TEST_CASE(test_klog_percent_escape) {
    // 测试百分号转义
    klog(LOG_INFO, "Progress: 100%% complete\n");
}

TEST_CASE(test_klog_special_chars) {
    // 测试特殊字符
    klog(LOG_INFO, "Special: tab\there, newline:\n");
    klog(LOG_INFO, "Continue after newline\n");
}

// ============================================================================
// 测试用例：等级边界值
// ============================================================================

TEST_CASE(test_klog_level_boundaries) {
    // 测试日志等级的边界值
    ASSERT_EQ(0, LOG_DEBUG);
    ASSERT_EQ(1, LOG_INFO);
    ASSERT_EQ(2, LOG_WARN);
    ASSERT_EQ(3, LOG_ERROR);
}

TEST_CASE(test_klog_level_ordering) {
    // 验证日志等级的顺序关系
    ASSERT_TRUE(LOG_DEBUG < LOG_INFO);
    ASSERT_TRUE(LOG_INFO < LOG_WARN);
    ASSERT_TRUE(LOG_WARN < LOG_ERROR);
}

// ============================================================================
// 测试用例：颜色保存和恢复
// ============================================================================

TEST_CASE(test_klog_color_preservation) {
    // 输出不同等级的日志（会改变颜色）
    klog(LOG_INFO, "Test color preservation\n");
    klog(LOG_WARN, "Another message\n");
    
    // 由于 klog 会改变颜色并恢复，我们测试它不会崩溃
    // 实际的颜色验证在集成测试中更合适
}

TEST_CASE(test_klog_nested_color_changes) {
    // 测试嵌套的颜色变化场景（使用 kconsole_set_color 兼容图形/文本模式）
    kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
    klog(LOG_INFO, "Log with custom color\n");
    
    kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
    klog(LOG_WARN, "Another log\n");
    
    // 恢复默认颜色
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(klog_level_tests) {
    RUN_TEST(test_klog_default_level);
    RUN_TEST(test_klog_set_level_debug);
    RUN_TEST(test_klog_set_level_info);
    RUN_TEST(test_klog_set_level_warn);
    RUN_TEST(test_klog_set_level_error);
    RUN_TEST(test_klog_level_sequence);
}

TEST_SUITE(klog_output_tests) {
    RUN_TEST(test_klog_debug_message);
    RUN_TEST(test_klog_info_message);
    RUN_TEST(test_klog_warn_message);
    RUN_TEST(test_klog_error_message);
    RUN_TEST(test_klog_plain_string);
    RUN_TEST(test_klog_empty_message);
    RUN_TEST(test_klog_multiple_arguments);
}

TEST_SUITE(klog_filter_tests) {
    RUN_TEST(test_klog_filter_debug_when_info);
    RUN_TEST(test_klog_filter_debug_info_when_warn);
    RUN_TEST(test_klog_filter_all_except_error);
    RUN_TEST(test_klog_show_all_when_debug);
}

TEST_SUITE(klog_macro_tests) {
    RUN_TEST(test_klog_debug_macro);
    RUN_TEST(test_klog_info_macro);
    RUN_TEST(test_klog_warn_macro);
    RUN_TEST(test_klog_error_macro);
    RUN_TEST(test_klog_all_macros);
}

TEST_SUITE(klog_format_tests) {
    RUN_TEST(test_klog_format_string);
    RUN_TEST(test_klog_format_integer);
    RUN_TEST(test_klog_format_unsigned);
    RUN_TEST(test_klog_format_hex);
    RUN_TEST(test_klog_format_pointer);
    RUN_TEST(test_klog_format_char);
    RUN_TEST(test_klog_format_mixed);
}

TEST_SUITE(klog_boundary_tests) {
    RUN_TEST(test_klog_long_message);
    RUN_TEST(test_klog_consecutive_logs);
    RUN_TEST(test_klog_different_levels_consecutive);
    RUN_TEST(test_klog_percent_escape);
    RUN_TEST(test_klog_special_chars);
}

TEST_SUITE(klog_level_property_tests) {
    RUN_TEST(test_klog_level_boundaries);
    RUN_TEST(test_klog_level_ordering);
}

TEST_SUITE(klog_color_tests) {
    RUN_TEST(test_klog_color_preservation);
    RUN_TEST(test_klog_nested_color_changes);
}

// ============================================================================
// 运行所有 klog 测试
// ============================================================================

void run_klog_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(klog_level_tests);
    RUN_SUITE(klog_output_tests);
    RUN_SUITE(klog_filter_tests);
    RUN_SUITE(klog_macro_tests);
    RUN_SUITE(klog_format_tests);
    RUN_SUITE(klog_boundary_tests);
    RUN_SUITE(klog_level_property_tests);
    RUN_SUITE(klog_color_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}

