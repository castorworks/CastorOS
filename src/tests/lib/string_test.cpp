// ============================================================================
// string_test.c - string 模块完整单元测试
// ============================================================================
// 
// 这个文件包含了对 lib/string.c 中所有函数的完整单元测试
// ============================================================================

#include <tests/ktest.h>
#include <tests/lib/string_test.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试用例：strlen 函数
// ============================================================================

TEST_CASE(test_strlen_empty) {
    ASSERT_EQ(strlen(""), 0);
}

TEST_CASE(test_strlen_single_char) {
    ASSERT_EQ(strlen("a"), 1);
}

TEST_CASE(test_strlen_normal) {
    ASSERT_EQ(strlen("hello"), 5);
    ASSERT_EQ(strlen("world"), 5);
    ASSERT_EQ(strlen("CastorOS"), 8);
}

TEST_CASE(test_strlen_long) {
    const char* long_str = "This is a very long string for testing strlen function";
    ASSERT_EQ(strlen(long_str), 54);
}

// ============================================================================
// 测试用例：strcmp 函数
// ============================================================================

TEST_CASE(test_strcmp_equal) {
    ASSERT_EQ(strcmp("hello", "hello"), 0);
    ASSERT_EQ(strcmp("", ""), 0);
    ASSERT_EQ(strcmp("a", "a"), 0);
}

TEST_CASE(test_strcmp_different) {
    ASSERT_NE(strcmp("hello", "world"), 0);
    ASSERT_TRUE(strcmp("abc", "abd") < 0);
    ASSERT_TRUE(strcmp("xyz", "abc") > 0);
}

TEST_CASE(test_strcmp_prefix) {
    ASSERT_NE(strcmp("hello", "hell"), 0);
    ASSERT_TRUE(strcmp("hello", "helloworld") < 0);
    ASSERT_TRUE(strcmp("helloworld", "hello") > 0);
}

TEST_CASE(test_strcmp_case_sensitive) {
    ASSERT_NE(strcmp("Hello", "hello"), 0);
    ASSERT_TRUE(strcmp("ABC", "abc") < 0);  // 'A' < 'a' in ASCII
}

// ============================================================================
// 测试用例：strncmp 函数
// ============================================================================

TEST_CASE(test_strncmp_equal) {
    ASSERT_EQ(strncmp("hello", "hello", 5), 0);
    ASSERT_EQ(strncmp("hello", "help", 2), 0);  // 只比较前2个字符 "he"
    ASSERT_EQ(strncmp("hello", "help", 3), 0);  // 只比较前3个字符 "hel"
}

TEST_CASE(test_strncmp_different) {
    ASSERT_NE(strncmp("hello", "world", 5), 0);
    ASSERT_NE(strncmp("hello", "help", 4), 0);  // "hell" vs "help"
}

TEST_CASE(test_strncmp_zero_length) {
    ASSERT_EQ(strncmp("hello", "world", 0), 0);  // 比较0个字符总是相等
    ASSERT_EQ(strncmp("", "abc", 0), 0);
}

TEST_CASE(test_strncmp_partial_match) {
    ASSERT_EQ(strncmp("helloworld", "hello", 5), 0);
    ASSERT_NE(strncmp("helloworld", "hello", 10), 0);
}

// ============================================================================
// 测试用例：strcasecmp 函数（不区分大小写比较）
// ============================================================================

TEST_CASE(test_strcasecmp_equal) {
    ASSERT_EQ(strcasecmp("hello", "HELLO"), 0);
    ASSERT_EQ(strcasecmp("Hello", "hello"), 0);
    ASSERT_EQ(strcasecmp("WORLD", "world"), 0);
    ASSERT_EQ(strcasecmp("", ""), 0);
}

TEST_CASE(test_strcasecmp_mixed_case) {
    ASSERT_EQ(strcasecmp("HeLLo", "hEllO"), 0);
    ASSERT_EQ(strcasecmp("CastorOS", "castoRos"), 0);
}

TEST_CASE(test_strcasecmp_different) {
    ASSERT_NE(strcasecmp("hello", "world"), 0);
    ASSERT_TRUE(strcasecmp("abc", "abd") < 0);
    ASSERT_TRUE(strcasecmp("xyz", "abc") > 0);
}

TEST_CASE(test_strcasecmp_numbers_special) {
    ASSERT_EQ(strcasecmp("test123", "TEST123"), 0);
    ASSERT_EQ(strcasecmp("hello-world", "HELLO-WORLD"), 0);
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

TEST_CASE(test_strcpy_long_string) {
    char dest[100];
    const char *src = "This is a long string to test strcpy";
    strcpy(dest, src);
    ASSERT_STR_EQ(dest, src);
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
    memset(dest, 'x', sizeof(dest));  // 填充垃圾值
    strncpy(dest, "hello", 5);
    dest[5] = '\0';
    ASSERT_STR_EQ(dest, "hello");
}

TEST_CASE(test_strncpy_truncate) {
    char dest[20];
    strncpy(dest, "helloworld", 5);
    dest[5] = '\0';
    ASSERT_STR_EQ(dest, "hello");
}

TEST_CASE(test_strncpy_padding) {
    char dest[20];
    memset(dest, 'x', sizeof(dest));
    strncpy(dest, "hi", 10);
    // strncpy 会用 '\0' 填充剩余空间
    ASSERT_EQ(dest[0], 'h');
    ASSERT_EQ(dest[1], 'i');
    ASSERT_EQ(dest[2], '\0');
    ASSERT_EQ(dest[3], '\0');
    ASSERT_EQ(dest[9], '\0');
}

TEST_CASE(test_strncpy_return_value) {
    char dest[20];
    char* result = strncpy(dest, "test", 4);
    ASSERT_EQ_PTR(result, dest);
}

// ============================================================================
// 测试用例：strtok 函数
// ============================================================================

TEST_CASE(test_strtok_simple) {
    char str[] = "hello world test";
    char *token;
    
    token = strtok(str, " ");
    ASSERT_STR_EQ(token, "hello");
    
    token = strtok(NULL, " ");
    ASSERT_STR_EQ(token, "world");
    
    token = strtok(NULL, " ");
    ASSERT_STR_EQ(token, "test");
    
    token = strtok(NULL, " ");
    ASSERT_NULL(token);
}

TEST_CASE(test_strtok_multiple_delimiters) {
    char str[] = "apple,banana;orange:grape";
    char *token;
    
    token = strtok(str, ",;:");
    ASSERT_STR_EQ(token, "apple");
    
    token = strtok(NULL, ",;:");
    ASSERT_STR_EQ(token, "banana");
    
    token = strtok(NULL, ",;:");
    ASSERT_STR_EQ(token, "orange");
    
    token = strtok(NULL, ",;:");
    ASSERT_STR_EQ(token, "grape");
    
    token = strtok(NULL, ",;:");
    ASSERT_NULL(token);
}

TEST_CASE(test_strtok_consecutive_delimiters) {
    char str[] = "a,,b,,c";
    char *token;
    
    token = strtok(str, ",");
    ASSERT_STR_EQ(token, "a");
    
    token = strtok(NULL, ",");
    ASSERT_STR_EQ(token, "b");
    
    token = strtok(NULL, ",");
    ASSERT_STR_EQ(token, "c");
    
    token = strtok(NULL, ",");
    ASSERT_NULL(token);
}

TEST_CASE(test_strtok_leading_trailing_delimiters) {
    char str[] = "  hello  world  ";
    char *token;
    
    token = strtok(str, " ");
    ASSERT_STR_EQ(token, "hello");
    
    token = strtok(NULL, " ");
    ASSERT_STR_EQ(token, "world");
    
    token = strtok(NULL, " ");
    ASSERT_NULL(token);
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

TEST_CASE(test_memset_single_byte) {
    uint8_t buffer[10];
    memset(buffer, 0xFF, 1);
    ASSERT_EQ(buffer[0], 0xFF);
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

TEST_CASE(test_memcpy_large_buffer) {
    uint8_t src[100];
    uint8_t dest[100];
    
    // 填充源缓冲区
    for (int i = 0; i < 100; i++) {
        src[i] = (uint8_t)i;
    }
    
    memcpy(dest, src, 100);
    
    // 验证复制
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(dest[i], (uint8_t)i);
    }
}

TEST_CASE(test_memcpy_return_value) {
    uint8_t src[5] = {1, 2, 3, 4, 5};
    uint8_t dest[5];
    void* result = memcpy(dest, src, 5);
    ASSERT_EQ_PTR(result, dest);
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
    ASSERT_TRUE(memcmp(a, b, 5) < 0);  // a[2]=3 < b[2]=9
}

TEST_CASE(test_memcmp_partial) {
    uint8_t a[5] = {1, 2, 3, 4, 5};
    uint8_t b[5] = {1, 2, 9, 4, 5};
    
    ASSERT_EQ(memcmp(a, b, 2), 0);   // 前2个字节相同
    ASSERT_NE(memcmp(a, b, 3), 0);   // 前3个字节不同
}

TEST_CASE(test_memcmp_zero_length) {
    uint8_t a[5] = {1, 2, 3, 4, 5};
    uint8_t b[5] = {9, 8, 7, 6, 5};
    
    ASSERT_EQ(memcmp(a, b, 0), 0);  // 比较0字节总是相等
}

// ============================================================================
// 测试用例：int32_to_str 函数
// ============================================================================

TEST_CASE(test_int32_to_str_zero) {
    char buffer[12];
    int32_to_str(0, buffer);
    ASSERT_STR_EQ(buffer, "0");
}

TEST_CASE(test_int32_to_str_positive) {
    char buffer[12];
    int32_to_str(12345, buffer);
    ASSERT_STR_EQ(buffer, "12345");
    
    int32_to_str(1, buffer);
    ASSERT_STR_EQ(buffer, "1");
    
    int32_to_str(999999, buffer);
    ASSERT_STR_EQ(buffer, "999999");
}

TEST_CASE(test_int32_to_str_negative) {
    char buffer[12];
    int32_to_str(-12345, buffer);
    ASSERT_STR_EQ(buffer, "-12345");
    
    int32_to_str(-1, buffer);
    ASSERT_STR_EQ(buffer, "-1");
}

TEST_CASE(test_int32_to_str_max_min) {
    char buffer[12];
    int32_to_str(2147483647, buffer);  // INT32_MAX
    ASSERT_STR_EQ(buffer, "2147483647");
    
    int32_to_str(-2147483648, buffer);  // INT32_MIN
    ASSERT_STR_EQ(buffer, "-2147483648");
}

// ============================================================================
// 测试用例：uint32_to_str 函数
// ============================================================================

TEST_CASE(test_uint32_to_str_zero) {
    char buffer[12];
    uint32_to_str(0, buffer);
    ASSERT_STR_EQ(buffer, "0");
}

TEST_CASE(test_uint32_to_str_normal) {
    char buffer[12];
    uint32_to_str(12345, buffer);
    ASSERT_STR_EQ(buffer, "12345");
    
    uint32_to_str(1, buffer);
    ASSERT_STR_EQ(buffer, "1");
}

TEST_CASE(test_uint32_to_str_max) {
    char buffer[12];
    uint32_to_str(4294967295U, buffer);  // UINT32_MAX
    ASSERT_STR_EQ(buffer, "4294967295");
}

// ============================================================================
// 测试用例：int32_to_hex 函数
// ============================================================================

TEST_CASE(test_int32_to_hex_lowercase) {
    char buffer[11];
    int32_to_hex(0xDEADBEEF, buffer, false);
    ASSERT_STR_EQ(buffer, "deadbeef");
    
    int32_to_hex(0x12345678, buffer, false);
    ASSERT_STR_EQ(buffer, "12345678");
}

TEST_CASE(test_int32_to_hex_uppercase) {
    char buffer[11];
    int32_to_hex(0xDEADBEEF, buffer, true);
    ASSERT_STR_EQ(buffer, "DEADBEEF");
    
    int32_to_hex(0xCAFEBABE, buffer, true);
    ASSERT_STR_EQ(buffer, "CAFEBABE");
}

TEST_CASE(test_int32_to_hex_zero) {
    char buffer[11];
    int32_to_hex(0, buffer, false);
    ASSERT_STR_EQ(buffer, "0");
}

// ============================================================================
// 测试用例：uint32_to_hex 函数
// ============================================================================

TEST_CASE(test_uint32_to_hex_lowercase) {
    char buffer[11];
    uint32_to_hex(0xDEADBEEF, buffer, false);
    ASSERT_STR_EQ(buffer, "deadbeef");
}

TEST_CASE(test_uint32_to_hex_uppercase) {
    char buffer[11];
    uint32_to_hex(0xCAFEBABE, buffer, true);
    ASSERT_STR_EQ(buffer, "CAFEBABE");
}

TEST_CASE(test_uint32_to_hex_max) {
    char buffer[11];
    uint32_to_hex(0xFFFFFFFF, buffer, false);
    ASSERT_STR_EQ(buffer, "ffffffff");
    
    uint32_to_hex(0xFFFFFFFF, buffer, true);
    ASSERT_STR_EQ(buffer, "FFFFFFFF");
}

// ============================================================================
// 测试用例：int64_to_str 函数
// ============================================================================

TEST_CASE(test_int64_to_str_zero) {
    char buffer[21];
    int64_to_str(0, buffer);
    ASSERT_STR_EQ(buffer, "0");
}

TEST_CASE(test_int64_to_str_positive) {
    char buffer[21];
    int64_to_str(123456789012345LL, buffer);
    ASSERT_STR_EQ(buffer, "123456789012345");
}

TEST_CASE(test_int64_to_str_negative) {
    char buffer[21];
    int64_to_str(-123456789012345LL, buffer);
    ASSERT_STR_EQ(buffer, "-123456789012345");
}

TEST_CASE(test_int64_to_str_max_min) {
    char buffer[21];
    int64_to_str(9223372036854775807LL, buffer);  // INT64_MAX
    ASSERT_STR_EQ(buffer, "9223372036854775807");
}

// ============================================================================
// 测试用例：uint64_to_str 函数
// ============================================================================

TEST_CASE(test_uint64_to_str_zero) {
    char buffer[21];
    uint64_to_str(0, buffer);
    ASSERT_STR_EQ(buffer, "0");
}

TEST_CASE(test_uint64_to_str_normal) {
    char buffer[21];
    uint64_to_str(123456789012345ULL, buffer);
    ASSERT_STR_EQ(buffer, "123456789012345");
}

TEST_CASE(test_uint64_to_str_max) {
    char buffer[21];
    uint64_to_str(18446744073709551615ULL, buffer);  // UINT64_MAX
    ASSERT_STR_EQ(buffer, "18446744073709551615");
}

// ============================================================================
// 测试用例：int64_to_hex 函数
// ============================================================================

TEST_CASE(test_int64_to_hex_lowercase) {
    char buffer[19];
    int64_to_hex(0xDEADBEEFCAFEBABELL, buffer, false);
    ASSERT_STR_EQ(buffer, "deadbeefcafebabe");
}

TEST_CASE(test_int64_to_hex_uppercase) {
    char buffer[19];
    int64_to_hex(0xDEADBEEFCAFEBABELL, buffer, true);
    ASSERT_STR_EQ(buffer, "DEADBEEFCAFEBABE");
}

TEST_CASE(test_int64_to_hex_zero) {
    char buffer[19];
    int64_to_hex(0, buffer, false);
    ASSERT_STR_EQ(buffer, "0");
}

// ============================================================================
// 测试用例：uint64_to_hex 函数
// ============================================================================

TEST_CASE(test_uint64_to_hex_lowercase) {
    char buffer[19];
    uint64_to_hex(0xDEADBEEFCAFEBABEULL, buffer, false);
    ASSERT_STR_EQ(buffer, "deadbeefcafebabe");
}

TEST_CASE(test_uint64_to_hex_uppercase) {
    char buffer[19];
    uint64_to_hex(0xDEADBEEFCAFEBABEULL, buffer, true);
    ASSERT_STR_EQ(buffer, "DEADBEEFCAFEBABE");
}

TEST_CASE(test_uint64_to_hex_max) {
    char buffer[19];
    uint64_to_hex(0xFFFFFFFFFFFFFFFFULL, buffer, false);
    ASSERT_STR_EQ(buffer, "ffffffffffffffff");
    
    uint64_to_hex(0xFFFFFFFFFFFFFFFFULL, buffer, true);
    ASSERT_STR_EQ(buffer, "FFFFFFFFFFFFFFFF");
}

// ============================================================================
// 测试用例：snprintf 函数
// ============================================================================

TEST_CASE(test_snprintf_string) {
    char buffer[50];
    int result = snprintf(buffer, sizeof(buffer), "Hello, %s!", "world");
    ASSERT_STR_EQ(buffer, "Hello, world!");
    ASSERT_EQ(result, 13);
}

TEST_CASE(test_snprintf_integer) {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Number: %d", 42);
    ASSERT_STR_EQ(buffer, "Number: 42");
    
    snprintf(buffer, sizeof(buffer), "Negative: %d", -123);
    ASSERT_STR_EQ(buffer, "Negative: -123");
}

TEST_CASE(test_snprintf_unsigned) {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Unsigned: %u", 12345U);
    ASSERT_STR_EQ(buffer, "Unsigned: 12345");
}

TEST_CASE(test_snprintf_hex) {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Hex: %x", 0xABCD);
    ASSERT_STR_EQ(buffer, "Hex: abcd");
    
    snprintf(buffer, sizeof(buffer), "HEX: %X", 0xABCD);
    ASSERT_STR_EQ(buffer, "HEX: ABCD");
}

TEST_CASE(test_snprintf_char) {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Char: %c", 'A');
    ASSERT_STR_EQ(buffer, "Char: A");
}

TEST_CASE(test_snprintf_pointer) {
    char buffer[50];
    void *ptr = (void *)0x12345678;
    snprintf(buffer, sizeof(buffer), "Pointer: %p", ptr);
    ASSERT_STR_EQ(buffer, "Pointer: 12345678");
}

TEST_CASE(test_snprintf_percent) {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Percent: %%");
    ASSERT_STR_EQ(buffer, "Percent: %");
}

TEST_CASE(test_snprintf_mixed) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "String: %s, Int: %d, Hex: %x", "test", 42, 0xFF);
    ASSERT_STR_EQ(buffer, "String: test, Int: 42, Hex: ff");
}

TEST_CASE(test_snprintf_buffer_limit) {
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "This is a very long string");
    ASSERT_EQ(strlen(buffer), 9);  // 应该被截断为 9 个字符 + '\0'
    ASSERT_EQ(buffer[9], '\0');    // 最后一个字符应该是 '\0'
}

TEST_CASE(test_snprintf_empty_buffer) {
    char buffer[1];
    snprintf(buffer, sizeof(buffer), "test");
    ASSERT_EQ(buffer[0], '\0');  // 只能容纳 '\0'
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(string_length_tests) {
    RUN_TEST(test_strlen_empty);
    RUN_TEST(test_strlen_single_char);
    RUN_TEST(test_strlen_normal);
    RUN_TEST(test_strlen_long);
}

TEST_SUITE(string_compare_tests) {
    RUN_TEST(test_strcmp_equal);
    RUN_TEST(test_strcmp_different);
    RUN_TEST(test_strcmp_prefix);
    RUN_TEST(test_strcmp_case_sensitive);
    RUN_TEST(test_strncmp_equal);
    RUN_TEST(test_strncmp_different);
    RUN_TEST(test_strncmp_zero_length);
    RUN_TEST(test_strncmp_partial_match);
}

TEST_SUITE(string_casecmp_tests) {
    RUN_TEST(test_strcasecmp_equal);
    RUN_TEST(test_strcasecmp_mixed_case);
    RUN_TEST(test_strcasecmp_different);
    RUN_TEST(test_strcasecmp_numbers_special);
}

TEST_SUITE(string_copy_tests) {
    RUN_TEST(test_strcpy_normal);
    RUN_TEST(test_strcpy_empty);
    RUN_TEST(test_strcpy_long_string);
    RUN_TEST(test_strcpy_return_value);
    RUN_TEST(test_strncpy_normal);
    RUN_TEST(test_strncpy_truncate);
    RUN_TEST(test_strncpy_padding);
    RUN_TEST(test_strncpy_return_value);
}

TEST_SUITE(string_token_tests) {
    RUN_TEST(test_strtok_simple);
    RUN_TEST(test_strtok_multiple_delimiters);
    RUN_TEST(test_strtok_consecutive_delimiters);
    RUN_TEST(test_strtok_leading_trailing_delimiters);
}

TEST_SUITE(memory_operation_tests) {
    RUN_TEST(test_memset_zero);
    RUN_TEST(test_memset_pattern);
    RUN_TEST(test_memset_single_byte);
    RUN_TEST(test_memset_return_value);
    RUN_TEST(test_memcpy_normal);
    RUN_TEST(test_memcpy_zero_length);
    RUN_TEST(test_memcpy_large_buffer);
    RUN_TEST(test_memcpy_return_value);
    RUN_TEST(test_memcmp_equal);
    RUN_TEST(test_memcmp_different);
    RUN_TEST(test_memcmp_partial);
    RUN_TEST(test_memcmp_zero_length);
}

TEST_SUITE(int32_conversion_tests) {
    RUN_TEST(test_int32_to_str_zero);
    RUN_TEST(test_int32_to_str_positive);
    RUN_TEST(test_int32_to_str_negative);
    RUN_TEST(test_int32_to_str_max_min);
}

TEST_SUITE(uint32_conversion_tests) {
    RUN_TEST(test_uint32_to_str_zero);
    RUN_TEST(test_uint32_to_str_normal);
    RUN_TEST(test_uint32_to_str_max);
}

TEST_SUITE(int32_hex_tests) {
    RUN_TEST(test_int32_to_hex_lowercase);
    RUN_TEST(test_int32_to_hex_uppercase);
    RUN_TEST(test_int32_to_hex_zero);
}

TEST_SUITE(uint32_hex_tests) {
    RUN_TEST(test_uint32_to_hex_lowercase);
    RUN_TEST(test_uint32_to_hex_uppercase);
    RUN_TEST(test_uint32_to_hex_max);
}

TEST_SUITE(int64_conversion_tests) {
    RUN_TEST(test_int64_to_str_zero);
    RUN_TEST(test_int64_to_str_positive);
    RUN_TEST(test_int64_to_str_negative);
    RUN_TEST(test_int64_to_str_max_min);
}

TEST_SUITE(uint64_conversion_tests) {
    RUN_TEST(test_uint64_to_str_zero);
    RUN_TEST(test_uint64_to_str_normal);
    RUN_TEST(test_uint64_to_str_max);
}

TEST_SUITE(int64_hex_tests) {
    RUN_TEST(test_int64_to_hex_lowercase);
    RUN_TEST(test_int64_to_hex_uppercase);
    RUN_TEST(test_int64_to_hex_zero);
}

TEST_SUITE(uint64_hex_tests) {
    RUN_TEST(test_uint64_to_hex_lowercase);
    RUN_TEST(test_uint64_to_hex_uppercase);
    RUN_TEST(test_uint64_to_hex_max);
}

TEST_SUITE(snprintf_tests) {
    RUN_TEST(test_snprintf_string);
    RUN_TEST(test_snprintf_integer);
    RUN_TEST(test_snprintf_unsigned);
    RUN_TEST(test_snprintf_hex);
    RUN_TEST(test_snprintf_char);
    RUN_TEST(test_snprintf_pointer);
    RUN_TEST(test_snprintf_percent);
    RUN_TEST(test_snprintf_mixed);
    RUN_TEST(test_snprintf_buffer_limit);
    RUN_TEST(test_snprintf_empty_buffer);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_string_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(string_length_tests);
    RUN_SUITE(string_compare_tests);
    RUN_SUITE(string_casecmp_tests);
    RUN_SUITE(string_copy_tests);
    RUN_SUITE(string_token_tests);
    RUN_SUITE(memory_operation_tests);
    RUN_SUITE(int32_conversion_tests);
    RUN_SUITE(uint32_conversion_tests);
    RUN_SUITE(int32_hex_tests);
    RUN_SUITE(uint32_hex_tests);
    RUN_SUITE(int64_conversion_tests);
    RUN_SUITE(uint64_conversion_tests);
    RUN_SUITE(int64_hex_tests);
    RUN_SUITE(uint64_hex_tests);
    RUN_SUITE(snprintf_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}

