// ============================================================================
// ktest_example.c - 单元测试框架使用示例
// ============================================================================
// 
// 这个文件展示了如何使用 CastorOS 的单元测试框架
// 包含了针对 lib/string.c 的测试用例
// ============================================================================

#include <tests/ktest.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试用例：strlen 函数
// ============================================================================

TEST_CASE(test_strlen_empty) {
    ASSERT_EQ(strlen(""), 0);
}

TEST_CASE(test_strlen_normal) {
    ASSERT_EQ(strlen("hello"), 5);
    ASSERT_EQ(strlen("world"), 5);
    ASSERT_EQ(strlen("CastorOS"), 8);
}

TEST_CASE(test_strlen_long) {
    const char* long_str = "This is a very long string for testing";
    ASSERT_EQ(strlen(long_str), 38);
}

// ============================================================================
// 测试用例：strcmp 函数
// ============================================================================

TEST_CASE(test_strcmp_equal) {
    ASSERT_EQ(strcmp("hello", "hello"), 0);
    ASSERT_EQ(strcmp("", ""), 0);
}

TEST_CASE(test_strcmp_different) {
    ASSERT_NE(strcmp("hello", "world"), 0);
    ASSERT_TRUE(strcmp("abc", "abd") < 0);
    ASSERT_TRUE(strcmp("xyz", "abc") > 0);
}

TEST_CASE(test_strcmp_prefix) {
    ASSERT_NE(strcmp("hello", "hell"), 0);
    ASSERT_TRUE(strcmp("hello", "helloworld") < 0);
}

// ============================================================================
// 测试用例：strncmp 函数
// ============================================================================

TEST_CASE(test_strncmp_equal) {
    ASSERT_EQ(strncmp("hello", "hello", 5), 0);
    ASSERT_EQ(strncmp("hello", "help", 2), 0);  // 只比较前2个字符
}

TEST_CASE(test_strncmp_different) {
    ASSERT_NE(strncmp("hello", "world", 5), 0);
    ASSERT_EQ(strncmp("hello", "help", 3), 0);  // "hel" == "hel"
}

TEST_CASE(test_strncmp_zero_length) {
    ASSERT_EQ(strncmp("hello", "world", 0), 0);  // 比较0个字符总是相等
}

// ============================================================================
// 测试用例：strcpy 函数
// ============================================================================

TEST_CASE(test_strcpy_normal) {
    char dest[20];
    strcpy(dest, "hello");
    ASSERT_STR_EQ(dest, "hello");
}

TEST_CASE(test_strcpy_empty) {
    char dest[20] = "original";
    strcpy(dest, "");
    ASSERT_STR_EQ(dest, "");
}

TEST_CASE(test_strcpy_return_value) {
    char dest[20];
    char* result = strcpy(dest, "test");
    ASSERT_EQ_PTR(result, dest);  // strcpy 应该返回 dest
}

// ============================================================================
// 测试用例：strncpy 函数
// ============================================================================

TEST_CASE(test_strncpy_normal) {
    char dest[20];
    strncpy(dest, "hello", 5);
    dest[5] = '\0';  // strncpy 不自动添加 null 终止符
    ASSERT_STR_EQ(dest, "hello");
}

TEST_CASE(test_strncpy_truncate) {
    char dest[20];
    strncpy(dest, "helloworld", 5);
    dest[5] = '\0';
    ASSERT_STR_EQ(dest, "hello");
}

// ============================================================================
// 测试用例：memset 函数
// ============================================================================

TEST_CASE(test_memset_zero) {
    uint8_t buffer[10];
    memset(buffer, 0, 10);
    
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(buffer[i], 0);
    }
}

TEST_CASE(test_memset_pattern) {
    uint8_t buffer[10];
    memset(buffer, 0xAA, 10);
    
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(buffer[i], 0xAA);
    }
}

TEST_CASE(test_memset_return_value) {
    uint8_t buffer[10];
    void* result = memset(buffer, 0, 10);
    ASSERT_EQ_PTR(result, buffer);  // memset 应该返回 buffer
}

// ============================================================================
// 测试用例：memcpy 函数
// ============================================================================

TEST_CASE(test_memcpy_normal) {
    uint8_t src[5] = {1, 2, 3, 4, 5};
    uint8_t dest[5];
    
    memcpy(dest, src, 5);
    
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(dest[i], src[i]);
    }
}

TEST_CASE(test_memcpy_zero_length) {
    uint8_t src[5] = {1, 2, 3, 4, 5};
    uint8_t dest[5] = {0, 0, 0, 0, 0};
    
    memcpy(dest, src, 0);
    
    // dest 应该保持不变
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(dest[i], 0);
    }
}

// ============================================================================
// 测试用例：memcmp 函数
// ============================================================================

TEST_CASE(test_memcmp_equal) {
    uint8_t a[5] = {1, 2, 3, 4, 5};
    uint8_t b[5] = {1, 2, 3, 4, 5};
    
    ASSERT_EQ(memcmp(a, b, 5), 0);
}

TEST_CASE(test_memcmp_different) {
    uint8_t a[5] = {1, 2, 3, 4, 5};
    uint8_t b[5] = {1, 2, 9, 4, 5};
    
    ASSERT_NE(memcmp(a, b, 5), 0);
}

TEST_CASE(test_memcmp_partial) {
    uint8_t a[5] = {1, 2, 3, 4, 5};
    uint8_t b[5] = {1, 2, 9, 4, 5};
    
    ASSERT_EQ(memcmp(a, b, 2), 0);   // 前2个字节相同
    ASSERT_NE(memcmp(a, b, 3), 0);   // 前3个字节不同
}

// ============================================================================
// 测试用例：数字转字符串函数
// ============================================================================

TEST_CASE(test_int32_to_str_positive) {
    char buffer[12];
    int32_to_str(12345, buffer);
    ASSERT_STR_EQ(buffer, "12345");
}

TEST_CASE(test_int32_to_str_negative) {
    char buffer[12];
    int32_to_str(-12345, buffer);
    ASSERT_STR_EQ(buffer, "-12345");
}

TEST_CASE(test_int32_to_str_zero) {
    char buffer[12];
    int32_to_str(0, buffer);
    ASSERT_STR_EQ(buffer, "0");
}

TEST_CASE(test_uint32_to_str) {
    char buffer[12];
    uint32_to_str(4294967295U, buffer);
    ASSERT_STR_EQ(buffer, "4294967295");
}

TEST_CASE(test_uint32_to_hex_lowercase) {
    char buffer[11];
    uint32_to_hex(0xDEADBEEF, buffer, false);
    ASSERT_STR_EQ(buffer, "0xdeadbeef");
}

TEST_CASE(test_uint32_to_hex_uppercase) {
    char buffer[11];
    uint32_to_hex(0xCAFEBABE, buffer, true);
    ASSERT_STR_EQ(buffer, "0xCAFEBABE");
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(string_length_tests) {
    RUN_TEST(test_strlen_empty);
    RUN_TEST(test_strlen_normal);
    RUN_TEST(test_strlen_long);
}

TEST_SUITE(string_compare_tests) {
    RUN_TEST(test_strcmp_equal);
    RUN_TEST(test_strcmp_different);
    RUN_TEST(test_strcmp_prefix);
    RUN_TEST(test_strncmp_equal);
    RUN_TEST(test_strncmp_different);
    RUN_TEST(test_strncmp_zero_length);
}

TEST_SUITE(string_copy_tests) {
    RUN_TEST(test_strcpy_normal);
    RUN_TEST(test_strcpy_empty);
    RUN_TEST(test_strcpy_return_value);
    RUN_TEST(test_strncpy_normal);
    RUN_TEST(test_strncpy_truncate);
}

TEST_SUITE(memory_tests) {
    RUN_TEST(test_memset_zero);
    RUN_TEST(test_memset_pattern);
    RUN_TEST(test_memset_return_value);
    RUN_TEST(test_memcpy_normal);
    RUN_TEST(test_memcpy_zero_length);
    RUN_TEST(test_memcmp_equal);
    RUN_TEST(test_memcmp_different);
    RUN_TEST(test_memcmp_partial);
}

TEST_SUITE(conversion_tests) {
    RUN_TEST(test_int32_to_str_positive);
    RUN_TEST(test_int32_to_str_negative);
    RUN_TEST(test_int32_to_str_zero);
    RUN_TEST(test_uint32_to_str);
    RUN_TEST(test_uint32_to_hex_lowercase);
    RUN_TEST(test_uint32_to_hex_uppercase);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_all_example_unit_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(string_length_tests);
    RUN_SUITE(string_compare_tests);
    RUN_SUITE(string_copy_tests);
    RUN_SUITE(memory_tests);
    RUN_SUITE(conversion_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}

