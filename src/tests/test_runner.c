// ============================================================================
// test_runner.c - 统一测试运行器
// ============================================================================
// 
// 运行所有注册的单元测试套件
// 使用数组管理测试，可以通过注释屏蔽某个测试
// ============================================================================

#include <tests/ktest.h>
#include <tests/ktest_example.h>
#include <tests/string_test.h>
#include <tests/kprintf_test.h>
#include <tests/klog_test.h>
#include <tests/pmm_test.h>
#include <tests/vmm_test.h>
#include <tests/task_test.h>
#include <tests/heap_test.h>
#include <tests/sync_test.h>
#include <lib/kprintf.h>
#include <drivers/vga.h>

// ============================================================================
// 测试用例结构体定义
// ============================================================================

typedef struct {
    const char *name;        // 测试模块名称
    void (*test_func)(void); // 测试函数指针
} test_entry_t;

// ============================================================================
// 测试用例数组 - 在此处添加或注释测试
// ============================================================================
// 使用方法：
//   - 添加测试：在数组中添加新的 TEST_ENTRY 行
//   - 禁用测试：注释掉对应的 TEST_ENTRY 行
//   - 调整顺序：直接移动 TEST_ENTRY 行的位置
// ============================================================================

#define TEST_ENTRY(name, func) { name, func }

static const test_entry_t test_suite[] = {
    // 示例测试（可以注释掉）
    // TEST_ENTRY("Example Unit Tests", run_all_example_unit_tests),

    // 基础库测试
    // TEST_ENTRY("String Library Tests", run_string_tests),
    
    // 输出系统测试
    // TEST_ENTRY("kprintf Module Tests", run_kprintf_tests),
    // TEST_ENTRY("klog Module Tests", run_klog_tests),
    
    // 内存管理测试
    // TEST_ENTRY("Physical Memory Manager Tests", run_pmm_tests),
    // TEST_ENTRY("Virtual Memory Manager Tests", run_vmm_tests),
    // TEST_ENTRY("Heap Allocator Tests", run_heap_tests),
    TEST_ENTRY("Task Manager Tests", run_task_tests),
    
    // ========== 在下方添加新的测试 ==========
    // TEST_ENTRY("Synchronization Primitive Tests", run_sync_tests),
    
};

// 计算测试用例总数
#define TEST_COUNT (sizeof(test_suite) / sizeof(test_suite[0]))

// ============================================================================
// 运行所有测试套件
// ============================================================================

/**
 * 运行所有测试套件
 */
void run_all_tests(void) {
    kprintf("\n");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| CastorOS Unit Test Suite\n");
    kprintf("================================================================================\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("\n");
    
    size_t test_count = TEST_COUNT;
    kprintf("Total test modules: %u\n\n", (unsigned int)test_count);
    
    // 如果没有测试用例，直接返回
    if (test_count == 0) {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        kprintf("No test modules registered.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }
    
    // 遍历并执行所有测试
    for (size_t i = 0; i < test_count; i++) {
        if (i > 0) {
            kprintf("\n\n");
        }
        
        kprintf("[Test Module %u/%u] %s\n", (unsigned int)(i + 1), (unsigned int)test_count, test_suite[i].name);
        
        // 执行测试函数
        if (test_suite[i].test_func != NULL) {
            test_suite[i].test_func();
        } else {
            vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            kprintf("Warning: Test function is NULL\n");
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    }
    
    kprintf("\n");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| All Tests Completed\n");
    kprintf("================================================================================\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("\n");
}

