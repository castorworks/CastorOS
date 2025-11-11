// ============================================================================
// ktest.h - 单元测试框架
// ============================================================================
// 
// 一个轻量级的内核测试框架，参考 KUnit 设计
// 
// 功能特性：
//   - 简单的测试用例定义和注册
//   - 多种断言宏（ASSERT_EQ, ASSERT_TRUE, ASSERT_NULL 等）
//   - 测试套件管理
//   - 测试结果统计和彩色报告
//   - 自动运行所有注册的测试
// 
// 使用示例：
//   TEST_CASE(test_string_length) {
//       ASSERT_EQ(strlen("hello"), 5);
//       ASSERT_NE(strlen("world"), 0);
//   }
//
//   TEST_SUITE(string_tests) {
//       RUN_TEST(test_string_length);
//   }
// ============================================================================

#ifndef _TESTS_KTEST_H_
#define _TESTS_KTEST_H_

#include <types.h>

// ============================================================================
// 测试结果统计
// ============================================================================

typedef struct {
    uint32_t total;       // 总测试数
    uint32_t passed;      // 通过的测试数
    uint32_t failed;      // 失败的测试数
    uint32_t assertions;  // 总断言数
} test_stats_t;

// ============================================================================
// 测试用例函数类型
// ============================================================================

typedef void (*test_func_t)(void);

// ============================================================================
// 测试上下文（用于跟踪当前测试状态）
// ============================================================================

typedef struct {
    const char*  current_test_name;    // 当前测试名称
    const char*  current_suite_name;   // 当前测试套件名称
    bool         test_failed;          // 当前测试是否失败
    test_stats_t stats;                // 测试统计
} test_context_t;

// ============================================================================
// 核心函数
// ============================================================================

// 初始化测试框架
void unittest_init(void);

// 开始一个测试套件
void unittest_begin_suite(const char* suite_name);

// 结束一个测试套件
void unittest_end_suite(void);

// 运行一个测试用例
void unittest_run_test(const char* test_name, test_func_t test_func);

// 打印测试结果摘要
void unittest_print_summary(void);

// 获取测试统计信息
test_stats_t unittest_get_stats(void);

// ============================================================================
// 断言函数（内部使用）
// ============================================================================

void _assert_true(bool condition, const char* expr, 
                  const char* file, int line);

void _assert_false(bool condition, const char* expr,
                   const char* file, int line);

void _assert_eq_int(int32_t expected, int32_t actual,
                    const char* file, int line);

void _assert_ne_int(int32_t expected, int32_t actual,
                    const char* file, int line);

void _assert_eq_uint(uint32_t expected, uint32_t actual,
                     const char* file, int line);

void _assert_ne_uint(uint32_t expected, uint32_t actual,
                     const char* file, int line);

void _assert_eq_ptr(void* expected, void* actual,
                    const char* file, int line);

void _assert_ne_ptr(void* expected, void* actual,
                    const char* file, int line);

void _assert_null(void* ptr, const char* file, int line);

void _assert_not_null(void* ptr, const char* file, int line);

void _assert_eq_str(const char* expected, const char* actual,
                    const char* file, int line);

void _assert_ne_str(const char* expected, const char* actual,
                    const char* file, int line);

// ============================================================================
// 便捷宏定义
// ============================================================================

// 定义一个测试用例
#define TEST_CASE(name) \
    static void test_##name(void)

// 定义一个测试套件
#define TEST_SUITE(name) \
    static void suite_##name(void)

// 在测试套件中运行一个测试
#define RUN_TEST(name) \
    unittest_run_test(#name, test_##name)

// 运行一个测试套件
#define RUN_SUITE(name) \
    do { \
        unittest_begin_suite(#name); \
        suite_##name(); \
        unittest_end_suite(); \
    } while (0)

// ============================================================================
// 断言宏
// ============================================================================

// 断言条件为真
#define ASSERT_TRUE(condition) \
    _assert_true(!!(condition), #condition, __FILE__, __LINE__)

// 断言条件为假
#define ASSERT_FALSE(condition) \
    _assert_false(!!(condition), #condition, __FILE__, __LINE__)

// 断言两个整数相等
#define ASSERT_EQ(expected, actual) \
    _assert_eq_int((int32_t)(expected), (int32_t)(actual), __FILE__, __LINE__)

// 断言两个整数不相等
#define ASSERT_NE(expected, actual) \
    _assert_ne_int((int32_t)(expected), (int32_t)(actual), __FILE__, __LINE__)

// 断言两个无符号整数相等
#define ASSERT_EQ_U(expected, actual) \
    _assert_eq_uint((uint32_t)(expected), (uint32_t)(actual), __FILE__, __LINE__)

// 断言两个无符号整数不相等
#define ASSERT_NE_U(expected, actual) \
    _assert_ne_uint((uint32_t)(expected), (uint32_t)(actual), __FILE__, __LINE__)

// 断言两个指针相等
#define ASSERT_EQ_PTR(expected, actual) \
    _assert_eq_ptr((void*)(expected), (void*)(actual), __FILE__, __LINE__)

// 断言两个指针不相等
#define ASSERT_NE_PTR(expected, actual) \
    _assert_ne_ptr((void*)(expected), (void*)(actual), __FILE__, __LINE__)

// 断言指针为 NULL
#define ASSERT_NULL(ptr) \
    _assert_null((void*)(ptr), __FILE__, __LINE__)

// 断言指针不为 NULL
#define ASSERT_NOT_NULL(ptr) \
    _assert_not_null((void*)(ptr), __FILE__, __LINE__)

// 断言两个字符串相等
#define ASSERT_STR_EQ(expected, actual) \
    _assert_eq_str((expected), (actual), __FILE__, __LINE__)

// 断言两个字符串不相等
#define ASSERT_STR_NE(expected, actual) \
    _assert_ne_str((expected), (actual), __FILE__, __LINE__)

// ============================================================================
// 辅助宏
// ============================================================================

// 测试失败时跳过剩余断言（手动标记测试失败）
#define TEST_FAIL(msg) \
    do { \
        kprintf("  [FAIL] %s\n", (msg)); \
        return; \
    } while (0)

// 手动标记测试通过（可选）
#define TEST_PASS() \
    return

#endif // _TESTS_KTEST_H_

