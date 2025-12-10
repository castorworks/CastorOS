// ============================================================================
// test_runner.c - 统一测试运行器
// ============================================================================
// 
// 运行所有注册的单元测试套件
// 使用数组管理测试，可以通过注释屏蔽某个测试
// 
// 支持多架构测试运行，提供架构特定诊断信息
// Requirements: 11.3, 11.4
// ============================================================================

#include <tests/ktest.h>
#include <tests/test_runner.h>
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
#include <tests/mm_types_test.h>
#include <tests/arm64_mmu_test.h>
#include <tests/arm64_exception_test.h>
#include <tests/arm64_fault_test.h>
#include <tests/interrupt_handler_test.h>
#include <tests/pgtable_test.h>
#include <tests/userlib_syscall_test.h>
#include <tests/dma_test.h>
#include <tests/cow_flag_test.h>
#include <tests/fork_exec_test.h>
#include <tests/syscall_error_test.h>
#include <tests/pbt.h>
#include <lib/kprintf.h>

// Architecture-specific constants are defined in types.h
// Additional arch-specific constants for test diagnostics
#if defined(ARCH_I686)
    #ifndef GPR_COUNT
    #define GPR_COUNT 8
    #endif
    #ifndef GPR_SIZE
    #define GPR_SIZE 4
    #endif
    #ifndef PAGE_TABLE_LEVELS
    #define PAGE_TABLE_LEVELS 2
    #endif
    #ifndef PHYS_ADDR_MAX
    #define PHYS_ADDR_MAX 0xFFFFFFFFUL
    #endif
#elif defined(ARCH_X86_64)
    #ifndef GPR_COUNT
    #define GPR_COUNT 16
    #endif
    #ifndef GPR_SIZE
    #define GPR_SIZE 8
    #endif
    #ifndef PAGE_TABLE_LEVELS
    #define PAGE_TABLE_LEVELS 4
    #endif
    #ifndef PHYS_ADDR_MAX
    #define PHYS_ADDR_MAX 0x0000FFFFFFFFFFFFULL
    #endif
    #ifndef USER_SPACE_END
    #define USER_SPACE_END 0x00007FFFFFFFFFFFULL
    #endif
#elif defined(ARCH_ARM64)
    #ifndef GPR_COUNT
    #define GPR_COUNT 31
    #endif
    #ifndef GPR_SIZE
    #define GPR_SIZE 8
    #endif
    #ifndef PAGE_TABLE_LEVELS
    #define PAGE_TABLE_LEVELS 4
    #endif
    #ifndef PHYS_ADDR_MAX
    #define PHYS_ADDR_MAX 0x0000FFFFFFFFFFFFULL
    #endif
    #ifndef USER_SPACE_END
    #define USER_SPACE_END 0x0000FFFFFFFFFFFFULL
    #endif
#else
    // Default values for unknown architecture
    #ifndef GPR_COUNT
    #define GPR_COUNT 0
    #endif
    #ifndef GPR_SIZE
    #define GPR_SIZE 0
    #endif
    #ifndef PAGE_TABLE_LEVELS
    #define PAGE_TABLE_LEVELS 0
    #endif
    #ifndef PHYS_ADDR_MAX
    #define PHYS_ADDR_MAX 0
    #endif
#endif

// ============================================================================
// 架构信息定义
// ============================================================================

/**
 * @brief 静态架构信息结构
 * 
 * 根据编译时架构宏定义填充
 */
static const arch_info_t g_arch_info = {
#if defined(ARCH_I686)
    .name = "i686",
    .bits = 32,
    .page_size = PAGE_SIZE,
    .page_table_levels = PAGE_TABLE_LEVELS,
    .kernel_base = KERNEL_VIRTUAL_BASE,
    .gpr_count = GPR_COUNT,
    .gpr_size = GPR_SIZE,
#elif defined(ARCH_X86_64)
    .name = "x86_64",
    .bits = 64,
    .page_size = PAGE_SIZE,
    .page_table_levels = PAGE_TABLE_LEVELS,
    .kernel_base = KERNEL_VIRTUAL_BASE,
    .gpr_count = GPR_COUNT,
    .gpr_size = GPR_SIZE,
#elif defined(ARCH_ARM64)
    .name = "arm64",
    .bits = 64,
    .page_size = PAGE_SIZE,
    .page_table_levels = PAGE_TABLE_LEVELS,
    .kernel_base = KERNEL_VIRTUAL_BASE,
    .gpr_count = GPR_COUNT,
    .gpr_size = GPR_SIZE,
#else
    .name = "unknown",
    .bits = 0,
    .page_size = 4096,
    .page_table_levels = 0,
    .kernel_base = 0,
    .gpr_count = 0,
    .gpr_size = 0,
#endif
};

/**
 * @brief 获取当前架构信息
 */
const arch_info_t* test_get_arch_info(void) {
    return &g_arch_info;
}

/**
 * @brief 打印架构诊断信息
 */
void test_print_arch_info(void) {
    const arch_info_t *info = &g_arch_info;
    
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("Architecture Information:\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("  Name:              %s\n", info->name);
    kprintf("  Bits:              %u-bit\n", info->bits);
    kprintf("  Page Size:         %u bytes\n", info->page_size);
    kprintf("  Page Table Levels: %u\n", info->page_table_levels);
    
    // Print kernel base address based on architecture
#if defined(ARCH_I686)
    kprintf("  Kernel Base:       0x%08x\n", (uint32_t)info->kernel_base);
#else
    kprintf("  Kernel Base:       0x%016llx\n", (uint64_t)info->kernel_base);
#endif
    
    kprintf("  GPR Count:         %u\n", info->gpr_count);
    kprintf("  GPR Size:          %u bytes\n", info->gpr_size);
    
    // Architecture-specific additional info
#if defined(ARCH_I686)
    kprintf("  Interrupt Method:  INT 0x80 / SYSENTER\n");
    kprintf("  Interrupt Ctrl:    PIC/APIC\n");
#elif defined(ARCH_X86_64)
    kprintf("  Interrupt Method:  SYSCALL/SYSRET\n");
    kprintf("  Interrupt Ctrl:    APIC\n");
    kprintf("  Address Space:     48-bit virtual, 4-level paging\n");
#elif defined(ARCH_ARM64)
    kprintf("  Interrupt Method:  SVC\n");
    kprintf("  Interrupt Ctrl:    GIC\n");
    kprintf("  Address Space:     48-bit virtual, TTBR0/TTBR1\n");
#endif
}

/**
 * @brief 打印测试失败时的架构诊断信息
 */
void test_print_failure_diagnostics(const char *test_name, 
                                     const char *file, 
                                     int line) {
    const arch_info_t *info = &g_arch_info;
    
    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("\n================================================================================\n");
    kprintf("TEST FAILURE DIAGNOSTICS\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    // Test information
    kprintf("Test:     %s\n", test_name ? test_name : "(unknown)");
    kprintf("Location: %s:%d\n", file ? file : "(unknown)", line);
    
    // Architecture context
    kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
    kprintf("\nArchitecture Context:\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("  Architecture:      %s (%u-bit)\n", info->name, info->bits);
    kprintf("  Page Size:         %u bytes\n", info->page_size);
    kprintf("  Page Table Levels: %u\n", info->page_table_levels);
    
#if defined(ARCH_I686)
    kprintf("  Kernel Base:       0x%08x\n", (uint32_t)info->kernel_base);
    kprintf("  Pointer Size:      4 bytes\n");
    kprintf("  Max Phys Addr:     0x%08x\n", (uint32_t)PHYS_ADDR_MAX);
#elif defined(ARCH_X86_64)
    kprintf("  Kernel Base:       0x%016llx\n", (uint64_t)info->kernel_base);
    kprintf("  Pointer Size:      8 bytes\n");
    kprintf("  Max Phys Addr:     0x%016llx\n", (uint64_t)PHYS_ADDR_MAX);
    kprintf("  User Space End:    0x%016llx\n", (uint64_t)USER_SPACE_END);
#elif defined(ARCH_ARM64)
    kprintf("  Kernel Base:       0x%016llx\n", (uint64_t)info->kernel_base);
    kprintf("  Pointer Size:      8 bytes\n");
    kprintf("  Max Phys Addr:     0x%016llx\n", (uint64_t)PHYS_ADDR_MAX);
    kprintf("  User Space End:    0x%016llx\n", (uint64_t)USER_SPACE_END);
#endif

    // Hints for debugging
    kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
    kprintf("\nDebugging Hints:\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
#if defined(ARCH_I686)
    kprintf("  - Check 32-bit address calculations\n");
    kprintf("  - Verify 2-level page table operations\n");
    kprintf("  - Ensure segment registers are correct\n");
#elif defined(ARCH_X86_64)
    kprintf("  - Check 64-bit address sign extension\n");
    kprintf("  - Verify 4-level page table operations\n");
    kprintf("  - Check canonical address requirements\n");
    kprintf("  - Verify NX bit handling if relevant\n");
#elif defined(ARCH_ARM64)
    kprintf("  - Check TTBR0/TTBR1 address space split\n");
    kprintf("  - Verify 4-level translation table operations\n");
    kprintf("  - Check memory attribute settings (MAIR)\n");
    kprintf("  - Verify exception level (should be EL1)\n");
#endif

    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("================================================================================\n\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

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
    
#ifdef ARCH_I686
    // i686 用户模式切换测试 (Property 11: User Mode Transition Correctness)
    // **Feature: multi-arch-support, Property 11: User Mode Transition Correctness (i686)**
    // **Validates: Requirements 7.4**
    TEST_ENTRY("i686 User Mode Transition Tests", run_usermode_tests),
#endif

#ifdef ARCH_X86_64
    // x86_64 ISR 测试 (Property 7: Interrupt Register State Preservation)
    TEST_ENTRY("x86_64 ISR Register Preservation Tests", run_isr64_tests),
    
    // x86_64 分页测试 (Property 4: VMM Kernel Mapping Range, Property 5: Page Fault Interpretation)
    TEST_ENTRY("x86_64 Paging Property Tests", run_paging64_tests),
    
    // x86_64 用户模式切换测试 (Property 11: User Mode Transition Correctness)
    TEST_ENTRY("x86_64 User Mode Transition Tests", run_usermode_tests),
#endif

#ifdef ARCH_ARM64
    // ARM64 MMU 测试 (Property 4: VMM Kernel Mapping Range Correctness)
    // **Feature: multi-arch-support, Property 4: VMM Kernel Mapping Range Correctness (ARM64)**
    // **Validates: Requirements 5.3**
    TEST_ENTRY("ARM64 MMU Property Tests", run_arm64_mmu_tests),
    
    // ARM64 异常处理测试 (Property 7: Interrupt Register State Preservation)
    // **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
    // **Validates: Requirements 6.2**
    TEST_ENTRY("ARM64 Exception Register Preservation Tests", run_arm64_exception_tests),
    
    // ARM64 页错误解析测试 (Property 5: VMM Page Fault Interpretation)
    // **Feature: multi-arch-support, Property 5: VMM Page Fault Interpretation (ARM64)**
    // **Validates: Requirements 5.4**
    TEST_ENTRY("ARM64 Page Fault Interpretation Tests", run_arm64_fault_tests),
    
    // ARM64 用户模式切换测试 (Property 11: User Mode Transition Correctness)
    // **Feature: multi-arch-support, Property 11: User Mode Transition Correctness (ARM64)**
    // **Validates: Requirements 7.4**
    TEST_ENTRY("ARM64 User Mode Transition Tests", run_usermode_tests),
#endif
    
    // 中断处理程序注册测试 (Property 8: Interrupt Handler Registration API Consistency)
    // **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
    // **Validates: Requirements 6.4**
    TEST_ENTRY("Interrupt Handler Registration Tests", run_interrupt_handler_tests),
    
    // ========== 在下方添加新的测试 ==========
    // TEST_ENTRY("Synchronization Primitive Tests", run_sync_tests),
    
    // 内存管理类型测试 (Property 1, 2: Type Size Correctness)
    // **Feature: mm-refactor, Property 1: Physical Address Type Size**
    // **Feature: mm-refactor, Property 2: Virtual Address Type Size**
    // **Validates: Requirements 1.1, 1.2**
    TEST_ENTRY("Memory Management Type Tests", run_mm_types_tests),
    
    // 页表抽象层测试 (Property 7: PTE Construction Round-Trip)
    // **Feature: mm-refactor, Property 7: PTE Construction Round-Trip**
    // **Validates: Requirements 3.3, 3.4**
    TEST_ENTRY("Page Table Abstraction Tests", run_pgtable_tests),
    
    // 用户库系统调用指令测试 (Property 16: User Library System Call Instruction Correctness)
    // **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
    // **Validates: Requirements 10.2**
    TEST_ENTRY("User Library Syscall Instruction Tests", run_userlib_syscall_tests),
    
    // DMA 缓存一致性测试 (Property 15: DMA Cache Coherency)
    // **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
    // **Validates: Requirements 9.4**
    TEST_ENTRY("DMA Cache Coherency Tests", run_dma_tests),
    
    // COW 标志正确性测试 (Property 6: VMM COW Flag Correctness)
    // **Feature: multi-arch-support, Property 6: VMM COW Flag Correctness**
    // **Validates: Requirements 5.5**
    TEST_ENTRY("COW Flag Correctness Tests", run_cow_flag_tests),
    
    // Fork/Exec 验证测试 (Task 36: 验证 fork/exec 在各架构上工作)
    // **Feature: multi-arch-support**
    // **Validates: Requirements 5.5, 7.4, mm-refactor 4.4, 5.3**
    TEST_ENTRY("Fork/Exec Verification Tests", run_fork_exec_tests),
    
    // 系统调用错误一致性测试 (Property 13: System Call Error Consistency)
    // **Feature: multi-arch-support, Property 13: System Call Error Consistency**
    // **Validates: Requirements 8.4**
    TEST_ENTRY("System Call Error Consistency Tests", run_syscall_error_tests),
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
    const arch_info_t *arch = test_get_arch_info();
    
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| CastorOS Unit Test Suite\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
    
    // Print architecture information at the start of test run
    kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
    kprintf("Target Architecture: ");
    kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
    kprintf("%s (%u-bit)\n", arch->name, arch->bits);
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("Page Size: %u bytes, Page Table Levels: %u\n", 
            arch->page_size, arch->page_table_levels);
    
#if defined(ARCH_I686)
    kprintf("Kernel Base: 0x%08x\n", (uint32_t)arch->kernel_base);
#else
    kprintf("Kernel Base: 0x%016llx\n", (uint64_t)arch->kernel_base);
#endif
    
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
    kprintf("|| All Tests Completed on %s (%u-bit)\n", arch->name, arch->bits);
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
}

