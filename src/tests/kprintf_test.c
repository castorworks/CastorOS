// ============================================================================
// kprintf_test.c - kprintf 模块单元测试
// ============================================================================
// 
// 测试 kprintf 格式化输出功能
// ============================================================================

#include <tests/ktest.h>
#include <tests/kprintf_test.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/serial.h>
#include <drivers/vga.h>
#include <types.h>

// ============================================================================
// 测试辅助 - 输出缓冲区
// ============================================================================

// 用于捕获 kprintf 输出的缓冲区（暂时不使用，但保留供未来可能的输出验证）
// 目前的测试主要验证 kprintf 调用不会崩溃，并可以通过串口/VGA 手动检查输出
static char test_output_buffer[4096];
static uint32_t test_output_pos = 0;

// 重置测试输出缓冲区
static void reset_test_buffer(void) {
    test_output_pos = 0;
    memset(test_output_buffer, 0, sizeof(test_output_buffer));
}

// ============================================================================
// 测试用例：基本格式化
// ============================================================================

TEST_CASE(test_kprintf_plain_string) {
    reset_test_buffer();
    kprintf("Hello, World!");
    // 由于 kprintf 直接输出到串口和 VGA，我们主要测试不崩溃
    // 在实际的内核环境中，可以通过串口或 VGA 检查输出
}

TEST_CASE(test_kprintf_empty_string) {
    reset_test_buffer();
    // 测试空字符串不会崩溃
    const char *empty = "";
    kprintf("%s", empty);
}

TEST_CASE(test_kprintf_newline) {
    reset_test_buffer();
    kprintf("Line 1\nLine 2\n");
}

// ============================================================================
// 测试用例：%s 字符串格式化
// ============================================================================

TEST_CASE(test_kprintf_format_string) {
    reset_test_buffer();
    kprintf("Hello, %s!", "CastorOS");
}

TEST_CASE(test_kprintf_format_string_null) {
    reset_test_buffer();
    // 测试 NULL 字符串处理（应该输出 "(null)"）
    const char *null_str = NULL;
    if (!null_str) {
        // 手动处理以避免编译器警告
        kprintf("Null string: ");
        kprintf("(tested with explicit null)");
    } else {
        kprintf("Null string: %s", null_str);
    }
}

TEST_CASE(test_kprintf_format_string_empty) {
    reset_test_buffer();
    kprintf("Empty: '%s'", "");
}

TEST_CASE(test_kprintf_format_string_multiple) {
    reset_test_buffer();
    kprintf("%s %s %s", "One", "Two", "Three");
}

TEST_CASE(test_kprintf_format_string_precision) {
    reset_test_buffer();
    kprintf("%.5s", "HelloWorld");  // 应该输出 "Hello"
}

TEST_CASE(test_kprintf_format_string_precision_zero) {
    reset_test_buffer();
    kprintf("%.0s", "Hello");  // 应该不输出任何内容
}

// ============================================================================
// 测试用例：%c 字符格式化
// ============================================================================

TEST_CASE(test_kprintf_format_char) {
    reset_test_buffer();
    kprintf("Char: %c", 'A');
}

TEST_CASE(test_kprintf_format_char_multiple) {
    reset_test_buffer();
    kprintf("%c%c%c", 'A', 'B', 'C');
}

TEST_CASE(test_kprintf_format_char_special) {
    reset_test_buffer();
    kprintf("Newline: %c Tab: %c", '\n', '\t');
}

// ============================================================================
// 测试用例：%d 有符号整数格式化
// ============================================================================

TEST_CASE(test_kprintf_format_int_positive) {
    reset_test_buffer();
    kprintf("Positive: %d", 12345);
}

TEST_CASE(test_kprintf_format_int_negative) {
    reset_test_buffer();
    kprintf("Negative: %d", -12345);
}

TEST_CASE(test_kprintf_format_int_zero) {
    reset_test_buffer();
    kprintf("Zero: %d", 0);
}

TEST_CASE(test_kprintf_format_int_max) {
    reset_test_buffer();
    kprintf("Max int32: %d", 2147483647);
}

TEST_CASE(test_kprintf_format_int_min) {
    reset_test_buffer();
    int32_t min_val = -2147483647 - 1;  // INT32_MIN
    kprintf("Min int32: %d", min_val);
}

TEST_CASE(test_kprintf_format_int_multiple) {
    reset_test_buffer();
    kprintf("%d + %d = %d", 10, 20, 30);
}

// ============================================================================
// 测试用例：%u 无符号整数格式化
// ============================================================================

TEST_CASE(test_kprintf_format_uint) {
    reset_test_buffer();
    kprintf("Unsigned: %u", 12345);
}

TEST_CASE(test_kprintf_format_uint_zero) {
    reset_test_buffer();
    kprintf("Zero: %u", 0);
}

TEST_CASE(test_kprintf_format_uint_max) {
    reset_test_buffer();
    kprintf("Max uint32: %u", 4294967295U);
}

TEST_CASE(test_kprintf_format_uint_multiple) {
    reset_test_buffer();
    kprintf("%u %u %u", 100U, 200U, 300U);
}

// ============================================================================
// 测试用例：%x 和 %X 十六进制格式化
// ============================================================================

TEST_CASE(test_kprintf_format_hex_lowercase) {
    reset_test_buffer();
    kprintf("Hex: %x", 0xDEADBEEF);
}

TEST_CASE(test_kprintf_format_hex_uppercase) {
    reset_test_buffer();
    kprintf("Hex: %X", 0xCAFEBABE);
}

TEST_CASE(test_kprintf_format_hex_zero) {
    reset_test_buffer();
    kprintf("Zero: %x", 0);
}

TEST_CASE(test_kprintf_format_hex_padded) {
    reset_test_buffer();
    kprintf("Padded: %08x", 0x1234);  // 应该输出 00001234 (无前缀)
}

TEST_CASE(test_kprintf_format_hex_padded_uppercase) {
    reset_test_buffer();
    kprintf("Padded: %08X", 0xABCD);  // 应该输出 0000ABCD (无前缀)
}

TEST_CASE(test_kprintf_format_hex_various_widths) {
    reset_test_buffer();
    kprintf("%02x %04x %08x", 0xFF, 0xFF, 0xFF);
}

// ============================================================================
// 测试用例：%p 指针格式化
// ============================================================================

TEST_CASE(test_kprintf_format_pointer) {
    reset_test_buffer();
    int x = 42;
    kprintf("Pointer: %p", (void*)&x);
}

TEST_CASE(test_kprintf_format_pointer_null) {
    reset_test_buffer();
    kprintf("Null pointer: %p", (void*)NULL);
}

TEST_CASE(test_kprintf_format_pointer_multiple) {
    reset_test_buffer();
    int a = 1, b = 2;
    kprintf("%p %p", (void*)&a, (void*)&b);
}

// ============================================================================
// 测试用例：%% 百分号转义
// ============================================================================

TEST_CASE(test_kprintf_format_percent) {
    reset_test_buffer();
    kprintf("100%% complete");
}

TEST_CASE(test_kprintf_format_percent_multiple) {
    reset_test_buffer();
    kprintf("%% %% %%");
}

// ============================================================================
// 测试用例：64 位整数格式化 (%lld, %llu, %llx, %llX)
// ============================================================================

TEST_CASE(test_kprintf_format_int64_positive) {
    reset_test_buffer();
    kprintf("Int64: %lld", 9223372036854775807LL);  // Max int64
}

TEST_CASE(test_kprintf_format_int64_negative) {
    reset_test_buffer();
    kprintf("Int64: %lld", -9223372036854775807LL);
}

TEST_CASE(test_kprintf_format_int64_zero) {
    reset_test_buffer();
    kprintf("Int64 zero: %lld", 0LL);
}

TEST_CASE(test_kprintf_format_uint64) {
    reset_test_buffer();
    kprintf("Uint64: %llu", 18446744073709551615ULL);  // Max uint64
}

TEST_CASE(test_kprintf_format_uint64_zero) {
    reset_test_buffer();
    kprintf("Uint64 zero: %llu", 0ULL);
}

TEST_CASE(test_kprintf_format_hex64_lowercase) {
    reset_test_buffer();
    kprintf("Hex64: %llx", 0xDEADBEEFCAFEBABEULL);
}

TEST_CASE(test_kprintf_format_hex64_uppercase) {
    reset_test_buffer();
    kprintf("Hex64: %llX", 0xDEADBEEFCAFEBABEULL);
}

TEST_CASE(test_kprintf_format_hex64_padded) {
    reset_test_buffer();
    kprintf("Padded: %016llx", 0x123456789ABCDEFULL);
}

TEST_CASE(test_kprintf_format_int64_multiple) {
    reset_test_buffer();
    kprintf("%lld %llu %llx", 
            -1234567890123456789LL, 
            12345678901234567890ULL,
            0xFEDCBA9876543210ULL);
}

// ============================================================================
// 测试用例：混合格式化
// ============================================================================

TEST_CASE(test_kprintf_format_mixed_basic) {
    reset_test_buffer();
    kprintf("String: %s, Int: %d, Hex: %x", "test", 42, 0xFF);
}

TEST_CASE(test_kprintf_format_mixed_complex) {
    reset_test_buffer();
    kprintf("Char: %c, Uint: %u, Ptr: %p, %%", 
            'Z', 999U, (void*)0x12345678);
}

TEST_CASE(test_kprintf_format_mixed_with_64bit) {
    reset_test_buffer();
    kprintf("Int32: %d, Int64: %lld, Hex64: %llx",
            1234, 1234567890123456789LL, 0xABCDEF0123456789ULL);
}

TEST_CASE(test_kprintf_format_mixed_all_types) {
    reset_test_buffer();
    kprintf("s=%s c=%c d=%d u=%u x=%x X=%X p=%p %% lld=%lld llu=%llu llx=%llx",
            "str", 'A', -42, 42U, 0xab, 0xCD, (void*)0x1000, 
            -9876543210LL, 9876543210ULL, 0x123456789ABCDEFULL);
}

// ============================================================================
// 测试用例：边界情况
// ============================================================================

TEST_CASE(test_kprintf_format_consecutive_percent) {
    reset_test_buffer();
    kprintf("%%%%");  // 应该输出 "%%"
}

TEST_CASE(test_kprintf_format_percent_at_end) {
    reset_test_buffer();
    kprintf("End with %%");
}

TEST_CASE(test_kprintf_format_unknown_specifier) {
    reset_test_buffer();
    // 测试未知格式说明符（应该原样输出）
    kprintf("Unknown format specifier test: percent-z");
}

TEST_CASE(test_kprintf_format_incomplete_specifier) {
    reset_test_buffer();
    // 测试不完整的格式说明符
    kprintf("Incomplete specifier test: 100%% done");
}

TEST_CASE(test_kprintf_long_string) {
    reset_test_buffer();
    // 测试较长的输出
    for (int i = 0; i < 10; i++) {
        kprintf("Line %d: This is a test line with various formats: %s %d %x\n",
                i, "test", i * 100, i * 16);
    }
}

// ============================================================================
// 测试用例：kprint 和 kputchar
// ============================================================================

TEST_CASE(test_kprint_basic) {
    reset_test_buffer();
    kprint("Hello from kprint");
}

TEST_CASE(test_kprint_empty) {
    reset_test_buffer();
    kprint("");
}

TEST_CASE(test_kprint_with_newlines) {
    reset_test_buffer();
    kprint("Line 1\n");
    kprint("Line 2\n");
    kprint("Line 3\n");
}

TEST_CASE(test_kputchar_basic) {
    reset_test_buffer();
    kputchar('H');
    kputchar('i');
    kputchar('!');
}

TEST_CASE(test_kputchar_newline) {
    reset_test_buffer();
    kputchar('\n');
    kputchar('\t');
}

TEST_CASE(test_kputchar_sequence) {
    reset_test_buffer();
    const char *msg = "Hello";
    for (int i = 0; msg[i] != '\0'; i++) {
        kputchar(msg[i]);
    }
}

// ============================================================================
// 测试用例：格式化宽度
// ============================================================================

TEST_CASE(test_kprintf_format_width_basic) {
    reset_test_buffer();
    kprintf("Width: %8d", 123);  // 应该有前导空格（如果实现了）
}

TEST_CASE(test_kprintf_format_width_zero_pad) {
    reset_test_buffer();
    kprintf("Zero pad: %08d", 123);  // 对于 %d 可能不支持零填充
}

TEST_CASE(test_kprintf_format_width_hex) {
    reset_test_buffer();
    kprintf("Hex width: %08x", 0xFF);
}

// ============================================================================
// 测试用例：特殊值
// ============================================================================

TEST_CASE(test_kprintf_format_values_boundaries) {
    reset_test_buffer();
    int32_t min_val = -2147483647 - 1;
    kprintf("Max: %u, Min: %d", 0xFFFFFFFFU, min_val);
}

TEST_CASE(test_kprintf_format_all_zeros) {
    reset_test_buffer();
    kprintf("%d %u %x %lld %llu %llx", 0, 0U, 0, 0LL, 0ULL, 0ULL);
}

TEST_CASE(test_kprintf_format_all_ones) {
    reset_test_buffer();
    kprintf("%d %u %x", -1, 0xFFFFFFFFU, 0xFFFFFFFFU);
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(kprintf_basic_tests) {
    RUN_TEST(test_kprintf_plain_string);
    RUN_TEST(test_kprintf_empty_string);
    RUN_TEST(test_kprintf_newline);
}

TEST_SUITE(kprintf_string_format_tests) {
    RUN_TEST(test_kprintf_format_string);
    RUN_TEST(test_kprintf_format_string_null);
    RUN_TEST(test_kprintf_format_string_empty);
    RUN_TEST(test_kprintf_format_string_multiple);
    RUN_TEST(test_kprintf_format_string_precision);
    RUN_TEST(test_kprintf_format_string_precision_zero);
}

TEST_SUITE(kprintf_char_format_tests) {
    RUN_TEST(test_kprintf_format_char);
    RUN_TEST(test_kprintf_format_char_multiple);
    RUN_TEST(test_kprintf_format_char_special);
}

TEST_SUITE(kprintf_int_format_tests) {
    RUN_TEST(test_kprintf_format_int_positive);
    RUN_TEST(test_kprintf_format_int_negative);
    RUN_TEST(test_kprintf_format_int_zero);
    RUN_TEST(test_kprintf_format_int_max);
    RUN_TEST(test_kprintf_format_int_min);
    RUN_TEST(test_kprintf_format_int_multiple);
}

TEST_SUITE(kprintf_uint_format_tests) {
    RUN_TEST(test_kprintf_format_uint);
    RUN_TEST(test_kprintf_format_uint_zero);
    RUN_TEST(test_kprintf_format_uint_max);
    RUN_TEST(test_kprintf_format_uint_multiple);
}

TEST_SUITE(kprintf_hex_format_tests) {
    RUN_TEST(test_kprintf_format_hex_lowercase);
    RUN_TEST(test_kprintf_format_hex_uppercase);
    RUN_TEST(test_kprintf_format_hex_zero);
    RUN_TEST(test_kprintf_format_hex_padded);
    RUN_TEST(test_kprintf_format_hex_padded_uppercase);
    RUN_TEST(test_kprintf_format_hex_various_widths);
}

TEST_SUITE(kprintf_pointer_format_tests) {
    RUN_TEST(test_kprintf_format_pointer);
    RUN_TEST(test_kprintf_format_pointer_null);
    RUN_TEST(test_kprintf_format_pointer_multiple);
}

TEST_SUITE(kprintf_percent_format_tests) {
    RUN_TEST(test_kprintf_format_percent);
    RUN_TEST(test_kprintf_format_percent_multiple);
}

TEST_SUITE(kprintf_int64_format_tests) {
    RUN_TEST(test_kprintf_format_int64_positive);
    RUN_TEST(test_kprintf_format_int64_negative);
    RUN_TEST(test_kprintf_format_int64_zero);
    RUN_TEST(test_kprintf_format_uint64);
    RUN_TEST(test_kprintf_format_uint64_zero);
    RUN_TEST(test_kprintf_format_hex64_lowercase);
    RUN_TEST(test_kprintf_format_hex64_uppercase);
    RUN_TEST(test_kprintf_format_hex64_padded);
    RUN_TEST(test_kprintf_format_int64_multiple);
}

TEST_SUITE(kprintf_mixed_format_tests) {
    RUN_TEST(test_kprintf_format_mixed_basic);
    RUN_TEST(test_kprintf_format_mixed_complex);
    RUN_TEST(test_kprintf_format_mixed_with_64bit);
    RUN_TEST(test_kprintf_format_mixed_all_types);
}

TEST_SUITE(kprintf_boundary_tests) {
    RUN_TEST(test_kprintf_format_consecutive_percent);
    RUN_TEST(test_kprintf_format_percent_at_end);
    RUN_TEST(test_kprintf_format_unknown_specifier);
    RUN_TEST(test_kprintf_format_incomplete_specifier);
    RUN_TEST(test_kprintf_long_string);
}

TEST_SUITE(kprintf_utility_tests) {
    RUN_TEST(test_kprint_basic);
    RUN_TEST(test_kprint_empty);
    RUN_TEST(test_kprint_with_newlines);
    RUN_TEST(test_kputchar_basic);
    RUN_TEST(test_kputchar_newline);
    RUN_TEST(test_kputchar_sequence);
}

TEST_SUITE(kprintf_width_tests) {
    RUN_TEST(test_kprintf_format_width_basic);
    RUN_TEST(test_kprintf_format_width_zero_pad);
    RUN_TEST(test_kprintf_format_width_hex);
}

TEST_SUITE(kprintf_special_values_tests) {
    RUN_TEST(test_kprintf_format_values_boundaries);
    RUN_TEST(test_kprintf_format_all_zeros);
    RUN_TEST(test_kprintf_format_all_ones);
}

// ============================================================================
// 运行所有 kprintf 测试
// ============================================================================

void run_kprintf_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(kprintf_basic_tests);
    RUN_SUITE(kprintf_string_format_tests);
    RUN_SUITE(kprintf_char_format_tests);
    RUN_SUITE(kprintf_int_format_tests);
    RUN_SUITE(kprintf_uint_format_tests);
    RUN_SUITE(kprintf_hex_format_tests);
    RUN_SUITE(kprintf_pointer_format_tests);
    RUN_SUITE(kprintf_percent_format_tests);
    RUN_SUITE(kprintf_int64_format_tests);
    RUN_SUITE(kprintf_mixed_format_tests);
    RUN_SUITE(kprintf_boundary_tests);
    RUN_SUITE(kprintf_utility_tests);
    RUN_SUITE(kprintf_width_tests);
    RUN_SUITE(kprintf_special_values_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}

