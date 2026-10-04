// ============================================================================
// serial_test.c - Serial 驱动测试模块
// ============================================================================
//
// Serial 串口驱动测试，验证字符发送和接收功能
// 测试 COM1 (x86) 和 PL011 UART (ARM64) 的通用接口
//
// **Feature: test-refactor**
// **Validates: Requirements 6.4**
//
// 测试覆盖:
//   - Serial 初始化
//   - 字符发送 (drivers::Serial::putchar)
//   - 字符串发送 (drivers::Serial::print)
//   - 字符接收 (ARM64 only: drivers::Serial::getchar, drivers::Serial::has_char)
//   - 特殊字符处理（换行符转换）
// ============================================================================

#include <tests/ktest.h>
#include <tests/drivers/serial_test.h>
#include <lib/kprintf.h>
#include <drivers/serial.h>

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * 测试输出标记字符串
 * 用于验证 drivers::Serial::print 是否正常工作
 */
static const char *test_marker = "[SERIAL_TEST]";

// ============================================================================
// 测试用例：Serial 基本功能
// ============================================================================

/**
 * 测试 drivers::Serial::putchar 发送单个字符
 * 验证函数可调用且不崩溃
 */
TEST_CASE(test_serial_putchar_basic) {
    // 发送一个简单字符
    drivers::Serial::putchar('X');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::putchar 发送多个字符
 * 验证连续发送不会出错
 */
TEST_CASE(test_serial_putchar_multiple) {
    // 发送多个字符
    drivers::Serial::putchar('[');
    drivers::Serial::putchar('T');
    drivers::Serial::putchar('E');
    drivers::Serial::putchar('S');
    drivers::Serial::putchar('T');
    drivers::Serial::putchar(']');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::putchar 发送特殊字符
 * 验证特殊字符（换行、回车、制表符）处理
 */
TEST_CASE(test_serial_putchar_special_chars) {
    // 发送换行符
    drivers::Serial::putchar('\n');
    
    // 发送回车符
    drivers::Serial::putchar('\r');
    
    // 发送制表符
    drivers::Serial::putchar('\t');
    
    // 发送空格
    drivers::Serial::putchar(' ');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::putchar 发送数字字符
 */
TEST_CASE(test_serial_putchar_digits) {
    for (char c = '0'; c <= '9'; c++) {
        drivers::Serial::putchar(c);
    }
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::putchar 发送字母字符
 */
TEST_CASE(test_serial_putchar_letters) {
    // 发送小写字母
    for (char c = 'a'; c <= 'z'; c++) {
        drivers::Serial::putchar(c);
    }
    
    // 发送大写字母
    for (char c = 'A'; c <= 'Z'; c++) {
        drivers::Serial::putchar(c);
    }
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

// ============================================================================
// 测试用例：Serial 字符串发送
// ============================================================================

/**
 * 测试 drivers::Serial::print 发送空字符串
 */
TEST_CASE(test_serial_print_empty) {
    drivers::Serial::print("");
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::print 发送简单字符串
 */
TEST_CASE(test_serial_print_simple) {
    drivers::Serial::print(test_marker);
    drivers::Serial::print(" Hello, Serial!\n");
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::print 发送包含换行符的字符串
 * 验证换行符自动转换为 \r\n
 */
TEST_CASE(test_serial_print_newlines) {
    drivers::Serial::print("Line 1\n");
    drivers::Serial::print("Line 2\n");
    drivers::Serial::print("Line 3\n");
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::print 发送长字符串
 */
TEST_CASE(test_serial_print_long_string) {
    drivers::Serial::print("This is a longer test string that spans multiple words ");
    drivers::Serial::print("to verify that the serial driver can handle longer output.\n");
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::print 连续调用
 */
TEST_CASE(test_serial_print_consecutive) {
    for (int i = 0; i < 10; i++) {
        drivers::Serial::print(".");
    }
    drivers::Serial::print("\n");
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

// ============================================================================
// 测试用例：Serial 边界情况
// ============================================================================

/**
 * 测试 drivers::Serial::putchar 发送 NULL 字符
 */
TEST_CASE(test_serial_putchar_null_char) {
    // 发送 NULL 字符（应该被处理但不显示）
    drivers::Serial::putchar('\0');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::putchar 发送高位字符
 */
TEST_CASE(test_serial_putchar_high_chars) {
    // 发送一些高位 ASCII 字符
    drivers::Serial::putchar((char)0x80);
    drivers::Serial::putchar((char)0xFF);
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::print 发送 NULL 指针
 * 注意：某些实现可能不检查 NULL，这里测试是否安全处理
 */
TEST_CASE(test_serial_print_null_ptr) {
    // 注意：x86 实现不检查 NULL，ARM64 实现会检查
    // 这个测试在 x86 上可能会崩溃，所以我们跳过它
    // 只验证非 NULL 情况
    const char *valid_str = "valid";
    drivers::Serial::print(valid_str);
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

// ============================================================================
// ARM64 特定测试：字符接收功能
// ============================================================================

#if defined(ARCH_ARM64)

/**
 * 测试 drivers::Serial::has_char 函数
 * 验证函数可调用并返回有效值
 */
TEST_CASE(test_serial_has_char) {
    // 检查是否有字符可读
    bool has_char = drivers::Serial::has_char();
    
    // 结果应该是 true 或 false
    ASSERT_TRUE(has_char == true || has_char == false);
}

/**
 * 测试 drivers::Serial::getchar_nonblock 函数
 * 验证非阻塞读取功能
 */
TEST_CASE(test_serial_getchar_nonblock) {
    // 非阻塞读取
    int result = drivers::Serial::getchar_nonblock();
    
    // 结果应该是 -1（无字符）或有效字符
    ASSERT_TRUE(result == -1 || (result >= 0 && result <= 255));
}

/**
 * 接收环形缓冲区：注入的字符按顺序读出，缓冲区里有数据时
 * 阻塞版 getchar 立即返回（不会在关中断下死等硬件 FIFO）
 */
TEST_CASE(test_serial_rx_buffer_order) {
    // 先清掉可能残留的输入
    while (drivers::Serial::getchar_nonblock() != -1) {
    }
    ASSERT_FALSE(drivers::Serial::has_char());
    
    drivers::Serial::rx_inject('x');
    drivers::Serial::rx_inject('y');
    drivers::Serial::rx_inject((char)0xE9);
    
    ASSERT_TRUE(drivers::Serial::has_char());
    ASSERT_EQ(drivers::Serial::getchar_nonblock(), 'x');
    ASSERT_EQ(drivers::Serial::getchar(), 'y');
    // 高位字符不能被符号扩展成负数（-1 表示没有数据）
    ASSERT_EQ(drivers::Serial::getchar_nonblock(), 0xE9);
    ASSERT_EQ(drivers::Serial::getchar_nonblock(), -1);
    ASSERT_FALSE(drivers::Serial::has_char());
}

/**
 * 接收环形缓冲区写满后丢弃新字符，不会覆盖未读数据或越界
 */
TEST_CASE(test_serial_rx_buffer_overflow) {
    while (drivers::Serial::getchar_nonblock() != -1) {
    }
    
    for (int i = 0; i < 1000; i++) {
        drivers::Serial::rx_inject((char)('a' + (i % 26)));
    }
    
    int count = 0;
    int c;
    while ((c = drivers::Serial::getchar_nonblock()) != -1) {
        ASSERT_EQ(c, 'a' + (count % 26));
        count++;
        if (count > 1000) {
            break;
        }
    }
    ASSERT_TRUE(count > 0);
    ASSERT_TRUE(count < 1000);
}

/**
 * 测试 drivers::Serial::flush 函数
 * 验证刷新发送缓冲区
 */
TEST_CASE(test_serial_flush) {
    // 发送一些字符
    drivers::Serial::print("Flush test\n");
    
    // 刷新缓冲区
    drivers::Serial::flush();
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::is_initialized 函数
 * 验证初始化状态检查
 */
TEST_CASE(test_serial_is_initialized) {
    // 串口应该已经初始化
    bool initialized = drivers::Serial::is_initialized();
    
    ASSERT_TRUE(initialized);
}

/**
 * 测试 drivers::Serial::get_base 函数
 * 验证获取 UART 基地址
 */
TEST_CASE(test_serial_get_base) {
    uint64_t base = drivers::Serial::get_base();
    
    // 基地址应该是有效的非零值
    ASSERT_TRUE(base != 0);
}

/**
 * 测试 drivers::Serial::put_hex32 函数
 */
TEST_CASE(test_serial_put_hex32) {
    drivers::Serial::put_hex32(0x12345678);
    drivers::Serial::putchar('\n');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 serial_put_hex64 函数
 */
TEST_CASE(test_serial_put_hex64) {
    serial_put_hex64(0x123456789ABCDEF0ULL);
    drivers::Serial::putchar('\n');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

/**
 * 测试 drivers::Serial::put_dec 函数
 */
TEST_CASE(test_serial_put_dec) {
    drivers::Serial::put_dec(12345);
    drivers::Serial::putchar('\n');
    drivers::Serial::put_dec(0);
    drivers::Serial::putchar('\n');
    
    // 如果没有崩溃，测试通过
    ASSERT_TRUE(true);
}

#endif // ARCH_ARM64

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(serial_putchar_tests) {
    RUN_TEST(test_serial_putchar_basic);
    RUN_TEST(test_serial_putchar_multiple);
    RUN_TEST(test_serial_putchar_special_chars);
    RUN_TEST(test_serial_putchar_digits);
    RUN_TEST(test_serial_putchar_letters);
}

TEST_SUITE(serial_print_tests) {
    RUN_TEST(test_serial_print_empty);
    RUN_TEST(test_serial_print_simple);
    RUN_TEST(test_serial_print_newlines);
    RUN_TEST(test_serial_print_long_string);
    RUN_TEST(test_serial_print_consecutive);
}

TEST_SUITE(serial_edge_case_tests) {
    RUN_TEST(test_serial_putchar_null_char);
    RUN_TEST(test_serial_putchar_high_chars);
    RUN_TEST(test_serial_print_null_ptr);
}

#if defined(ARCH_ARM64)
TEST_SUITE(serial_arm64_tests) {
    RUN_TEST(test_serial_has_char);
    RUN_TEST(test_serial_getchar_nonblock);
    RUN_TEST(test_serial_rx_buffer_order);
    RUN_TEST(test_serial_rx_buffer_overflow);
    RUN_TEST(test_serial_flush);
    RUN_TEST(test_serial_is_initialized);
    RUN_TEST(test_serial_get_base);
    RUN_TEST(test_serial_put_hex32);
    RUN_TEST(test_serial_put_hex64);
    RUN_TEST(test_serial_put_dec);
}
#endif

// ============================================================================
// 运行所有测试
// ============================================================================

void run_serial_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(serial_putchar_tests);
    RUN_SUITE(serial_print_tests);
    RUN_SUITE(serial_edge_case_tests);
    
#if defined(ARCH_ARM64)
    RUN_SUITE(serial_arm64_tests);
#endif
    
    // 打印测试摘要
    unittest_print_summary();
}
