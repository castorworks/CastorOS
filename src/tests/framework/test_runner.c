// ============================================================================
// test_runner.c - 统一测试运行器
// ============================================================================
// 
// 运行所有注册的单元测试套件
// 支持模块化注册和传统数组两种方式管理测试
// 
// **Feature: test-refactor**
// **Validates: Requirements 10.2, 12.2, 12.3, 13.1**
// ============================================================================

#include <tests/ktest.h>
#include <tests/test_runner.h>
#include <tests/test_module.h>

// 子系统测试头文件 (x86 only for now)
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <tests/lib/string_test.h>
#include <tests/lib/kprintf_test.h>
#include <tests/lib/klog_test.h>
#include <tests/mm/pmm_test.h>
#include <tests/mm/vmm_test.h>
#include <tests/mm/heap_test.h>
#include <tests/mm/mm_types_test.h>
#include <tests/mm/pgtable_test.h>
#include <tests/mm/cow_flag_test.h>
#include <tests/mm/dma_test.h>
#include <tests/fs/vfs_test.h>
#include <tests/fs/ramfs_test.h>
#include <tests/fs/fat32_test.h>
#include <tests/fs/devfs_test.h>
#include <tests/net/checksum_test.h>
#include <tests/net/netbuf_test.h>
#include <tests/net/arp_test.h>
#include <tests/net/tcp_test.h>
#include <tests/kernel/task_test.h>
#include <tests/kernel/sync_test.h>
#include <tests/kernel/syscall_test.h>
#include <tests/kernel/syscall_error_test.h>
#include <tests/kernel/fork_exec_test.h>
#include <tests/kernel/usermode_test.h>
#include <tests/drivers/pci_test.h>
#include <tests/drivers/timer_test.h>
#include <tests/drivers/serial_test.h>
#include <tests/arch/hal_test.h>
#include <tests/arch/arch_types_test.h>
#include <tests/arch/interrupt_handler_test.h>
#include <tests/arch/userlib_syscall_test.h>
#include <tests/pbt/pbt.h>
#include <tests/examples/ktest_example.h>
#endif

#ifdef ARCH_X86_64
#include <tests/arch/x86_64/isr64_test.h>
#include <tests/arch/x86_64/paging64_test.h>
#endif

#ifdef ARCH_ARM64
#include <tests/arch/arm64/arm64_mmu_test.h>
#include <tests/arch/arm64/arm64_exception_test.h>
#include <tests/arch/arm64/arm64_fault_test.h>
#include <tests/arch/arm64/arm64_syscall_test.h>
#endif
#include <lib/kprintf.h>

// Architecture-specific constants
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

const arch_info_t* test_get_arch_info(void) {
    return &g_arch_info;
}


void test_print_arch_info(void) {
    const arch_info_t *info = &g_arch_info;
    
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("Architecture Information:\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("  Name:              %s\n", info->name);
    kprintf("  Bits:              %u-bit\n", info->bits);
    kprintf("  Page Size:         %u bytes\n", info->page_size);
    kprintf("  Page Table Levels: %u\n", info->page_table_levels);
    
#if defined(ARCH_I686)
    kprintf("  Kernel Base:       0x%08x\n", (uint32_t)info->kernel_base);
#else
    kprintf("  Kernel Base:       0x%016llx\n", (uint64_t)info->kernel_base);
#endif
    
    kprintf("  GPR Count:         %u\n", info->gpr_count);
    kprintf("  GPR Size:          %u bytes\n", info->gpr_size);
    
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

void test_print_failure_diagnostics(const char *test_name, 
                                     const char *file, 
                                     int line) {
    const arch_info_t *info = &g_arch_info;
    
    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("\n================================================================================\n");
    kprintf("TEST FAILURE DIAGNOSTICS\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("Test:     %s\n", test_name ? test_name : "(unknown)");
    kprintf("Location: %s:%d\n", file ? file : "(unknown)", line);
    
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
#elif defined(ARCH_ARM64)
    kprintf("  - Check TTBR0/TTBR1 address space split\n");
    kprintf("  - Verify 4-level translation table operations\n");
    kprintf("  - Check memory attribute settings (MAIR)\n");
#endif

    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("================================================================================\n\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}


// ============================================================================
// 测试用例数组
// ============================================================================

typedef struct {
    const char *name;
    void (*test_func)(void);
} test_entry_t;

#define TEST_ENTRY(name, func) { name, func }

static const test_entry_t test_suite[] = {
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    // 基础库测试 (lib/)
    TEST_ENTRY("String Library Tests", run_string_tests),
    TEST_ENTRY("kprintf Module Tests", run_kprintf_tests),
    TEST_ENTRY("klog Module Tests", run_klog_tests),
    
    // 内存管理测试 (mm/)
    TEST_ENTRY("Physical Memory Manager Tests", run_pmm_tests),
#ifndef ARCH_X86_64
    TEST_ENTRY("Virtual Memory Manager Tests", run_vmm_tests),
#endif
    TEST_ENTRY("Heap Allocator Tests", run_heap_tests),
#ifndef ARCH_X86_64
    TEST_ENTRY("Task Manager Tests", run_task_tests),
#endif
    TEST_ENTRY("Memory Management Type Tests", run_mm_types_tests),
    TEST_ENTRY("Page Table Abstraction Tests", run_pgtable_tests),
    TEST_ENTRY("COW Flag Correctness Tests", run_cow_flag_tests),
    TEST_ENTRY("DMA Cache Coherency Tests", run_dma_tests),
    
    // 架构测试 (arch/)
    TEST_ENTRY("Architecture Type Size Tests", run_arch_types_tests),
    TEST_ENTRY("System Call Property Tests", run_syscall_tests),
    TEST_ENTRY("HAL Property Tests", run_hal_tests),
    TEST_ENTRY("Interrupt Handler Registration Tests", run_interrupt_handler_tests),
    TEST_ENTRY("User Library Syscall Instruction Tests", run_userlib_syscall_tests),
    
#ifdef ARCH_I686
    TEST_ENTRY("i686 User Mode Transition Tests", run_usermode_tests),
#endif

#ifdef ARCH_X86_64
    TEST_ENTRY("x86_64 ISR Register Preservation Tests", run_isr64_tests),
    TEST_ENTRY("x86_64 Paging Property Tests", run_paging64_tests),
    TEST_ENTRY("x86_64 User Mode Transition Tests", run_usermode_tests),
#endif
    
    // 内核核心测试 (kernel/)
    TEST_ENTRY("Fork/Exec Verification Tests", run_fork_exec_tests),
    TEST_ENTRY("System Call Error Consistency Tests", run_syscall_error_tests),
    
    // 文件系统测试 (fs/)
    TEST_ENTRY("VFS Tests", run_vfs_tests),
    TEST_ENTRY("Ramfs Tests", run_ramfs_tests),
    TEST_ENTRY("FAT32 Tests", run_fat32_tests),
    TEST_ENTRY("Devfs Tests", run_devfs_tests),
    
    // 网络测试 (net/)
    TEST_ENTRY("Checksum Tests", run_checksum_tests),
    TEST_ENTRY("Netbuf Tests", run_netbuf_tests),
    TEST_ENTRY("ARP Tests", run_arp_tests),
    TEST_ENTRY("TCP Tests", run_tcp_tests),
    
    // 驱动测试 (drivers/)
    TEST_ENTRY("PCI Tests", run_pci_tests),
    TEST_ENTRY("Timer Tests", run_timer_tests),
    TEST_ENTRY("Serial Tests", run_serial_tests),
#endif /* ARCH_I686 || ARCH_X86_64 */

#ifdef ARCH_ARM64
    // ARM64-specific tests only
    TEST_ENTRY("ARM64 MMU Property Tests", run_arm64_mmu_tests),
    TEST_ENTRY("ARM64 Exception Register Preservation Tests", run_arm64_exception_tests),
    TEST_ENTRY("ARM64 Page Fault Interpretation Tests", run_arm64_fault_tests),
    TEST_ENTRY("ARM64 System Call Integration Tests", run_arm64_syscall_tests),
#endif
};

#define TEST_COUNT (sizeof(test_suite) / sizeof(test_suite[0]))

void run_all_tests(void) {
    const arch_info_t *arch = test_get_arch_info();
    
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| CastorOS Unit Test Suite\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
    
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
    
    if (test_count == 0) {
        kconsole_set_color(KCOLOR_YELLOW, KCOLOR_BLACK);
        kprintf("No test modules registered.\n");
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        return;
    }
    
    for (size_t i = 0; i < test_count; i++) {
        if (i > 0) {
            kprintf("\n\n");
        }
        
        kprintf("[Test Module %u/%u] %s\n", (unsigned int)(i + 1), 
                (unsigned int)test_count, test_suite[i].name);
        
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


// ============================================================================
// 模块化测试运行器支持
// ============================================================================

static test_registry_t g_test_registry;
static bool g_registry_initialized = false;

void test_runner_init_registry(void) {
    if (!g_registry_initialized) {
        test_registry_init(&g_test_registry);
        g_registry_initialized = true;
    }
}

test_registry_t* test_runner_get_registry(void) {
    if (!g_registry_initialized) {
        test_runner_init_registry();
    }
    return &g_test_registry;
}

bool test_runner_register_module(const test_module_t *module) {
    if (!g_registry_initialized) {
        test_runner_init_registry();
    }
    return test_registry_add(&g_test_registry, module);
}

void run_subsystem_tests(const char *subsystem) {
    if (!g_registry_initialized) {
        kprintf("Warning: Test registry not initialized\n");
        return;
    }
    test_run_subsystem(&g_test_registry, subsystem);
}

void run_module_tests(const char *module_name) {
    if (!g_registry_initialized) {
        kprintf("Warning: Test registry not initialized\n");
        return;
    }
    test_run_module(&g_test_registry, module_name);
}

void run_tests_with_options(const test_run_options_t *options) {
    if (!g_registry_initialized) {
        kprintf("Warning: Test registry not initialized\n");
        return;
    }
    test_run_with_options(&g_test_registry, options);
}

void test_runner_list_modules(void) {
    if (!g_registry_initialized) {
        kprintf("Test registry not initialized\n");
        return;
    }
    
    kprintf("\nRegistered Test Modules:\n");
    kprintf("========================\n");
    
    for (uint32_t i = 0; i < g_test_registry.count; i++) {
        const test_module_t *mod = g_test_registry.modules[i];
        if (mod != NULL) {
            kprintf("  [%u] %s (%s)", i + 1, mod->name, 
                    test_subsystem_name(mod->subsystem));
            if (mod->is_slow) {
                kprintf(" [slow]");
            }
            if (mod->is_arch_specific) {
                kprintf(" [arch-specific]");
            }
            kprintf("\n");
            if (mod->description != NULL) {
                kprintf("      %s\n", mod->description);
            }
        }
    }
    
    kprintf("\nTotal: %u modules\n", g_test_registry.count);
}
