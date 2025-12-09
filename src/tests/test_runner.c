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
#include <tests/arch_types_test.h>
#include <tests/syscall_test.h>
#include <tests/hal_test.h>
#include <tests/isr64_test.h>
#include <tests/paging64_test.h>
#include <tests/usermode_test.h>
#include <lib/kprintf.h>

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
    TEST_ENTRY("String Library Tests", run_string_tests),
    
    // 输出系统测试
    TEST_ENTRY("kprintf Module Tests", run_kprintf_tests),
    TEST_ENTRY("klog Module Tests", run_klog_tests),
    
    // 内存管理测试
    TEST_ENTRY("Physical Memory Manager Tests", run_pmm_tests),
#ifndef ARCH_X86_64
    // VMM tests use 32-bit page table structures, skip on x86_64
    TEST_ENTRY("Virtual Memory Manager Tests", run_vmm_tests),
#endif
    TEST_ENTRY("Heap Allocator Tests", run_heap_tests),
#ifndef ARCH_X86_64
    // Task tests depend on VMM page directory operations
    TEST_ENTRY("Task Manager Tests", run_task_tests),
#endif
    
    // 架构类型测试 (Property 17: User Library Data Type Size Correctness)
    TEST_ENTRY("Architecture Type Size Tests", run_arch_types_tests),
    
    // 系统调用测试 (Property 12: System Call Round-Trip Correctness)
    TEST_ENTRY("System Call Property Tests", run_syscall_tests),
    
    // HAL 测试 (Property 1: HAL Initialization Dispatch, Property 14: MMIO Memory Barrier)
    TEST_ENTRY("HAL Property Tests", run_hal_tests),
    
#ifdef ARCH_X86_64
    // x86_64 ISR 测试 (Property 7: Interrupt Register State Preservation)
    TEST_ENTRY("x86_64 ISR Register Preservation Tests", run_isr64_tests),
    
    // x86_64 分页测试 (Property 4: VMM Kernel Mapping Range, Property 5: Page Fault Interpretation)
    TEST_ENTRY("x86_64 Paging Property Tests", run_paging64_tests),
    
    // x86_64 用户模式切换测试 (Property 11: User Mode Transition Correctness)
    TEST_ENTRY("x86_64 User Mode Transition Tests", run_usermode_tests),
#endif
    
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
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| CastorOS Unit Test Suite\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
    
    size_t test_count = TEST_COUNT;
    kprintf("Total test modules: %u\n\n", (unsigned int)test_count);
    
    // 如果没有测试用例，直接返回
    if (test_count == 0) {
        kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
        kprintf("No test modules registered.\n");
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
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
            kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
            kprintf("Warning: Test function is NULL\n");
            kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        }
    }
    
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| All Tests Completed\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
}

