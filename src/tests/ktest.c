// ============================================================================
// ktest.c - 单元测试框架实现
// ============================================================================

#include <tests/ktest.h>
#include <tests/test_runner.h>
#include <lib/kprintf.h>
#include <lib/string.h>

// 全局测试上下文
static test_context_t g_test_ctx;

// Track last assertion failure location for diagnostics
static const char* g_last_failure_file = NULL;
static int g_last_failure_line = 0;

// ============================================================================
// 辅助函数：彩色输出（使用 kconsole_set_color 自动适配图形/文本模式）
// ============================================================================

static void print_pass(const char* msg) {
    kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
    kprintf("%s", msg);
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

static void print_fail(const char* msg) {
    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("%s", msg);
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

static void print_info(const char* msg) {
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("%s", msg);
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

// ============================================================================
// 核心函数实现
// ============================================================================

void unittest_init(void) {
    g_test_ctx.current_test_name = NULL;
    g_test_ctx.current_suite_name = NULL;
    g_test_ctx.test_failed = false;
    g_test_ctx.stats.total = 0;
    g_test_ctx.stats.passed = 0;
    g_test_ctx.stats.failed = 0;
    g_test_ctx.stats.assertions = 0;
}

void unittest_begin_suite(const char* suite_name) {
    g_test_ctx.current_suite_name = suite_name;
    kprintf("\n");
    print_info("================================================================================\n");
    print_info("Test Suite: ");
    kprintf("%s\n", suite_name);
    print_info("================================================================================\n");
}

void unittest_end_suite(void) {
    g_test_ctx.current_suite_name = NULL;
}

void unittest_run_test(const char* test_name, test_func_t test_func) {
    g_test_ctx.current_test_name = test_name;
    g_test_ctx.test_failed = false;
    g_test_ctx.stats.total++;
    
    // Reset failure location tracking
    g_last_failure_file = NULL;
    g_last_failure_line = 0;
    
    kprintf("  [ RUN  ] %s\n", test_name);
    
    // 运行测试
    test_func();
    
    // 检查结果
    if (!g_test_ctx.test_failed) {
        g_test_ctx.stats.passed++;
        kprintf("  ");
        print_pass("[  OK  ]");
        kprintf(" %s\n", test_name);
    } else {
        g_test_ctx.stats.failed++;
        kprintf("  ");
        print_fail("[ FAIL ]");
        kprintf(" %s\n", test_name);
        
        // Print architecture-specific diagnostics on failure
        // Requirements: 11.4 - Report architecture-specific diagnostic information
        test_print_failure_diagnostics(test_name, g_last_failure_file, g_last_failure_line);
    }
    
    g_test_ctx.current_test_name = NULL;
}

void unittest_print_summary(void) {
    const arch_info_t *arch = test_get_arch_info();
    
    kprintf("\n");
    print_info("================================================================================\n");
    print_info("Test Summary\n");
    print_info("================================================================================\n");
    
    // Include architecture in summary
    kprintf("Architecture:     %s (%u-bit)\n", arch->name, arch->bits);
    kprintf("Total tests:      %u\n", g_test_ctx.stats.total);
    
    if (g_test_ctx.stats.passed > 0) {
        kprintf("Passed tests:     ");
        kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
        kprintf("%u", g_test_ctx.stats.passed);
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        kprintf("\n");
    }
    
    if (g_test_ctx.stats.failed > 0) {
        kprintf("Failed tests:     ");
        kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
        kprintf("%u", g_test_ctx.stats.failed);
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        kprintf("\n");
    }
    
    kprintf("Total assertions: %u\n", g_test_ctx.stats.assertions);
    
    kprintf("\nResult: ");
    if (g_test_ctx.stats.failed == 0) {
        kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
        kprintf("ALL TESTS PASSED on %s", arch->name);
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        kprintf("\n");
    } else {
        kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
        kprintf("SOME TESTS FAILED on %s", arch->name);
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        kprintf("\n");
        
        // Print detailed architecture info when tests fail
        kprintf("\n");
        test_print_arch_info();
    }
    print_info("================================================================================\n\n");
}

test_stats_t unittest_get_stats(void) {
    return g_test_ctx.stats;
}

// ============================================================================
// 断言函数实现
// ============================================================================

void _assert_true(bool condition, const char* expr, 
                  const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (!condition) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("%s\n", expr);
        kprintf("    Expected: true\n");
        kprintf("    Actual:   false\n");
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_false(bool condition, const char* expr,
                   const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (condition) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("%s\n", expr);
        kprintf("    Expected: false\n");
        kprintf("    Actual:   true\n");
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_eq_int(int32_t expected, int32_t actual,
                    const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (expected != actual) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected == actual\n");
        kprintf("    Expected: %d\n", expected);
        kprintf("    Actual:   %d\n", actual);
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_ne_int(int32_t expected, int32_t actual,
                    const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (expected == actual) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected != actual\n");
        kprintf("    Expected not: %d\n", expected);
        kprintf("    Actual:       %d\n", actual);
        kprintf("    Location:     %s:%d\n", file, line);
    }
}

void _assert_eq_uint(uint32_t expected, uint32_t actual,
                     const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (expected != actual) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected == actual\n");
        kprintf("    Expected: %u (0x%x)\n", expected, expected);
        kprintf("    Actual:   %u (0x%x)\n", actual, actual);
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_ne_uint(uint32_t expected, uint32_t actual,
                     const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (expected == actual) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected != actual\n");
        kprintf("    Expected not: %u (0x%x)\n", expected, expected);
        kprintf("    Actual:       %u (0x%x)\n", actual, actual);
        kprintf("    Location:     %s:%d\n", file, line);
    }
}

void _assert_eq_ptr(void* expected, void* actual,
                    const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (expected != actual) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected == actual\n");
        kprintf("    Expected: %p\n", expected);
        kprintf("    Actual:   %p\n", actual);
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_ne_ptr(void* expected, void* actual,
                    const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (expected == actual) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected != actual\n");
        kprintf("    Expected not: %p\n", expected);
        kprintf("    Actual:       %p\n", actual);
        kprintf("    Location:     %s:%d\n", file, line);
    }
}

void _assert_null(void* ptr, const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (ptr != NULL) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("ptr == NULL\n");
        kprintf("    Expected: NULL\n");
        kprintf("    Actual:   %p\n", ptr);
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_not_null(void* ptr, const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    if (ptr == NULL) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("ptr != NULL\n");
        kprintf("    Expected: not NULL\n");
        kprintf("    Actual:   NULL\n");
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_eq_str(const char* expected, const char* actual,
                    const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    // 处理 NULL 指针
    if (expected == NULL && actual == NULL) {
        return;
    }
    
    if (expected == NULL || actual == NULL) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected == actual\n");
        kprintf("    Expected: %s\n", expected ? expected : "(null)");
        kprintf("    Actual:   %s\n", actual ? actual : "(null)");
        kprintf("    Location: %s:%d\n", file, line);
        return;
    }
    
    if (strcmp(expected, actual) != 0) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected == actual\n");
        kprintf("    Expected: \"%s\"\n", expected);
        kprintf("    Actual:   \"%s\"\n", actual);
        kprintf("    Location: %s:%d\n", file, line);
    }
}

void _assert_ne_str(const char* expected, const char* actual,
                    const char* file, int line) {
    g_test_ctx.stats.assertions++;
    
    // 处理 NULL 指针
    if ((expected == NULL && actual != NULL) || 
        (expected != NULL && actual == NULL)) {
        return;
    }
    
    if (expected == NULL && actual == NULL) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected != actual\n");
        kprintf("    Both are NULL\n");
        kprintf("    Location: %s:%d\n", file, line);
        return;
    }
    
    if (strcmp(expected, actual) == 0) {
        g_test_ctx.test_failed = true;
        g_last_failure_file = file;
        g_last_failure_line = line;
        kprintf("    ");
        print_fail("Assertion failed: ");
        kprintf("expected != actual\n");
        kprintf("    Expected not: \"%s\"\n", expected);
        kprintf("    Actual:       \"%s\"\n", actual);
        kprintf("    Location:     %s:%d\n", file, line);
    }
}

