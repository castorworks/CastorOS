// ============================================================================
// test_module.h - 测试模块接口定义
// ============================================================================
//
// 定义测试模块的元数据结构和注册宏，支持模块化测试组织
//
// **Feature: test-refactor**
// **Validates: Requirements 10.1, 10.2, 11.1**
//
// 功能特性：
//   - 测试模块元数据结构 (test_module_t)
//   - 模块注册宏 (TEST_MODULE, TEST_MODULE_WITH_DEPS)
//   - 子系统分组支持
//   - 依赖声明支持
//   - 慢测试标记支持
//
// 使用示例：
//   // 简单模块注册
//   TEST_MODULE(pmm, mm, run_pmm_tests);
//
//   // 带依赖的模块注册
//   static const char *vmm_deps[] = {"pmm"};
//   TEST_MODULE_WITH_DEPS(vmm, mm, run_vmm_tests, vmm_deps, 1);
//
//   // 标记为慢测试
//   TEST_MODULE_SLOW(stress, mm, run_stress_tests);
// ============================================================================

#ifndef _TESTS_TEST_MODULE_H_
#define _TESTS_TEST_MODULE_H_

#include <types.h>

// ============================================================================
// 子系统定义
// ============================================================================

/**
 * @brief 测试子系统枚举
 * 
 * 用于按子系统组织测试模块
 */
typedef enum {
    TEST_SUBSYSTEM_MM,       // 内存管理 (pmm, vmm, heap, cow)
    TEST_SUBSYSTEM_FS,       // 文件系统 (vfs, fat32, ramfs, devfs)
    TEST_SUBSYSTEM_NET,      // 网络栈 (ip, tcp, arp, checksum)
    TEST_SUBSYSTEM_DRIVERS,  // 驱动 (pci, timer, serial)
    TEST_SUBSYSTEM_KERNEL,   // 内核核心 (task, sync, syscall)
    TEST_SUBSYSTEM_ARCH,     // 架构相关 (hal, pgtable, context)
    TEST_SUBSYSTEM_LIB,      // 库函数 (string, kprintf, klog)
    TEST_SUBSYSTEM_COUNT     // 子系统总数
} test_subsystem_t;

/**
 * @brief 子系统名称字符串
 */
static const char * const TEST_SUBSYSTEM_NAMES[] = {
    "mm",
    "fs",
    "net",
    "drivers",
    "kernel",
    "arch",
    "lib"
};

// ============================================================================
// 测试模块元数据结构
// ============================================================================

/**
 * @brief 测试模块元数据结构
 * 
 * 包含测试模块的完整描述信息，用于模块化注册和管理
 * 
 * Requirements: 10.1 - 测试模块自包含
 */
typedef struct test_module {
    const char *name;              // 模块名称 (如 "pmm", "vmm")
    const char *description;       // 模块描述
    test_subsystem_t subsystem;    // 所属子系统
    void (*run_func)(void);        // 运行函数
    const char **dependencies;     // 依赖的其他模块名称数组
    uint32_t dep_count;            // 依赖数量
    bool is_slow;                  // 是否为慢测试
    bool is_arch_specific;         // 是否为架构特定测试
    uint32_t arch_mask;            // 支持的架构掩码
} test_module_t;

// ============================================================================
// 架构掩码定义
// ============================================================================

#define TEST_ARCH_I686    (1 << 0)
#define TEST_ARCH_X86_64  (1 << 1)
#define TEST_ARCH_ARM64   (1 << 2)
#define TEST_ARCH_ALL     (TEST_ARCH_I686 | TEST_ARCH_X86_64 | TEST_ARCH_ARM64)

// 获取当前架构掩码
#if defined(ARCH_I686)
    #define TEST_CURRENT_ARCH TEST_ARCH_I686
#elif defined(ARCH_X86_64)
    #define TEST_CURRENT_ARCH TEST_ARCH_X86_64
#elif defined(ARCH_ARM64)
    #define TEST_CURRENT_ARCH TEST_ARCH_ARM64
#else
    #define TEST_CURRENT_ARCH 0
#endif

// ============================================================================
// 模块注册宏
// ============================================================================

/**
 * @brief 定义一个测试模块（基本版本）
 * 
 * @param mod_name 模块名称（标识符）
 * @param subsys 子系统枚举值（不带 TEST_SUBSYSTEM_ 前缀）
 * @param func 运行函数
 * 
 * 使用示例：
 *   TEST_MODULE(pmm, MM, run_pmm_tests);
 */
#define TEST_MODULE(mod_name, subsys, func) \
    static const test_module_t __test_module_##mod_name \
    __attribute__((used, section(".test_modules"))) = { \
        .name = #mod_name, \
        .description = #mod_name " tests", \
        .subsystem = TEST_SUBSYSTEM_##subsys, \
        .run_func = func, \
        .dependencies = NULL, \
        .dep_count = 0, \
        .is_slow = false, \
        .is_arch_specific = false, \
        .arch_mask = TEST_ARCH_ALL \
    }

/**
 * @brief 定义一个测试模块（带描述）
 * 
 * @param mod_name 模块名称（标识符）
 * @param subsys 子系统枚举值
 * @param func 运行函数
 * @param desc 模块描述字符串
 */
#define TEST_MODULE_DESC(mod_name, subsys, func, desc) \
    static const test_module_t __test_module_##mod_name \
    __attribute__((used, section(".test_modules"))) = { \
        .name = #mod_name, \
        .description = desc, \
        .subsystem = TEST_SUBSYSTEM_##subsys, \
        .run_func = func, \
        .dependencies = NULL, \
        .dep_count = 0, \
        .is_slow = false, \
        .is_arch_specific = false, \
        .arch_mask = TEST_ARCH_ALL \
    }

/**
 * @brief 定义一个带依赖的测试模块
 * 
 * @param mod_name 模块名称（标识符）
 * @param subsys 子系统枚举值
 * @param func 运行函数
 * @param deps 依赖模块名称数组
 * @param count 依赖数量
 * 
 * 使用示例：
 *   static const char *vmm_deps[] = {"pmm"};
 *   TEST_MODULE_WITH_DEPS(vmm, MM, run_vmm_tests, vmm_deps, 1);
 */
#define TEST_MODULE_WITH_DEPS(mod_name, subsys, func, deps, count) \
    static const test_module_t __test_module_##mod_name \
    __attribute__((used, section(".test_modules"))) = { \
        .name = #mod_name, \
        .description = #mod_name " tests", \
        .subsystem = TEST_SUBSYSTEM_##subsys, \
        .run_func = func, \
        .dependencies = deps, \
        .dep_count = count, \
        .is_slow = false, \
        .is_arch_specific = false, \
        .arch_mask = TEST_ARCH_ALL \
    }

/**
 * @brief 定义一个慢测试模块
 * 
 * 慢测试默认不运行，需要显式启用
 * 
 * @param mod_name 模块名称（标识符）
 * @param subsys 子系统枚举值
 * @param func 运行函数
 */
#define TEST_MODULE_SLOW(mod_name, subsys, func) \
    static const test_module_t __test_module_##mod_name \
    __attribute__((used, section(".test_modules"))) = { \
        .name = #mod_name, \
        .description = #mod_name " tests (slow)", \
        .subsystem = TEST_SUBSYSTEM_##subsys, \
        .run_func = func, \
        .dependencies = NULL, \
        .dep_count = 0, \
        .is_slow = true, \
        .is_arch_specific = false, \
        .arch_mask = TEST_ARCH_ALL \
    }

/**
 * @brief 定义一个架构特定的测试模块
 * 
 * @param mod_name 模块名称（标识符）
 * @param subsys 子系统枚举值
 * @param func 运行函数
 * @param archs 支持的架构掩码
 * 
 * 使用示例：
 *   TEST_MODULE_ARCH(isr64, ARCH, run_isr64_tests, TEST_ARCH_X86_64);
 */
#define TEST_MODULE_ARCH(mod_name, subsys, func, archs) \
    static const test_module_t __test_module_##mod_name \
    __attribute__((used, section(".test_modules"))) = { \
        .name = #mod_name, \
        .description = #mod_name " tests", \
        .subsystem = TEST_SUBSYSTEM_##subsys, \
        .run_func = func, \
        .dependencies = NULL, \
        .dep_count = 0, \
        .is_slow = false, \
        .is_arch_specific = true, \
        .arch_mask = archs \
    }

/**
 * @brief 定义一个完整配置的测试模块
 * 
 * @param mod_name 模块名称
 * @param subsys 子系统
 * @param func 运行函数
 * @param desc 描述
 * @param deps 依赖数组
 * @param count 依赖数量
 * @param slow 是否慢测试
 * @param archs 架构掩码
 */
#define TEST_MODULE_FULL(mod_name, subsys, func, desc, deps, count, slow, archs) \
    static const test_module_t __test_module_##mod_name \
    __attribute__((used, section(".test_modules"))) = { \
        .name = #mod_name, \
        .description = desc, \
        .subsystem = TEST_SUBSYSTEM_##subsys, \
        .run_func = func, \
        .dependencies = deps, \
        .dep_count = count, \
        .is_slow = slow, \
        .is_arch_specific = ((archs) != TEST_ARCH_ALL), \
        .arch_mask = archs \
    }

// ============================================================================
// 模块注册表操作
// ============================================================================

/**
 * @brief 最大注册模块数
 */
#define TEST_MODULE_MAX_COUNT 64

/**
 * @brief 测试模块注册表
 */
typedef struct test_registry {
    const test_module_t *modules[TEST_MODULE_MAX_COUNT];  // 模块指针数组
    uint32_t count;                                        // 已注册模块数
} test_registry_t;

/**
 * @brief 初始化测试模块注册表
 * @param registry 注册表指针
 */
void test_registry_init(test_registry_t *registry);

/**
 * @brief 注册一个测试模块
 * @param registry 注册表指针
 * @param module 模块指针
 * @return 成功返回 true，失败返回 false
 */
bool test_registry_add(test_registry_t *registry, const test_module_t *module);

/**
 * @brief 按名称查找模块
 * @param registry 注册表指针
 * @param name 模块名称
 * @return 找到返回模块指针，否则返回 NULL
 */
const test_module_t* test_registry_find(const test_registry_t *registry, 
                                         const char *name);

/**
 * @brief 获取指定子系统的所有模块
 * @param registry 注册表指针
 * @param subsystem 子系统枚举值
 * @param out_modules 输出模块指针数组
 * @param max_count 数组最大容量
 * @return 找到的模块数量
 */
uint32_t test_registry_get_by_subsystem(const test_registry_t *registry,
                                         test_subsystem_t subsystem,
                                         const test_module_t **out_modules,
                                         uint32_t max_count);

// ============================================================================
// 运行选项
// ============================================================================

/**
 * @brief 测试运行选项
 * 
 * Requirements: 13.1 - 支持选择性测试执行
 */
typedef struct test_run_options {
    const char *filter_module;     // 只运行指定模块（NULL 表示全部）
    const char *filter_subsystem;  // 只运行指定子系统（NULL 表示全部）
    bool include_slow;             // 包含慢测试
    bool stop_on_failure;          // 失败时停止
    bool verbose;                  // 详细输出
} test_run_options_t;

/**
 * @brief 默认运行选项
 */
#define TEST_RUN_OPTIONS_DEFAULT { \
    .filter_module = NULL, \
    .filter_subsystem = NULL, \
    .include_slow = false, \
    .stop_on_failure = false, \
    .verbose = false \
}

// ============================================================================
// 模块化运行函数
// ============================================================================

/**
 * @brief 使用选项运行测试
 * @param registry 注册表指针
 * @param options 运行选项
 */
void test_run_with_options(const test_registry_t *registry,
                           const test_run_options_t *options);

/**
 * @brief 运行指定子系统的所有测试
 * @param registry 注册表指针
 * @param subsystem 子系统名称字符串
 */
void test_run_subsystem(const test_registry_t *registry, const char *subsystem);

/**
 * @brief 运行指定模块的测试
 * @param registry 注册表指针
 * @param module_name 模块名称
 */
void test_run_module(const test_registry_t *registry, const char *module_name);

/**
 * @brief 检查模块是否应该在当前架构上运行
 * @param module 模块指针
 * @return 应该运行返回 true，否则返回 false
 */
static inline bool test_module_should_run(const test_module_t *module) {
    if (module == NULL || module->run_func == NULL) {
        return false;
    }
    // 检查架构兼容性
    if (module->is_arch_specific && 
        !(module->arch_mask & TEST_CURRENT_ARCH)) {
        return false;
    }
    return true;
}

/**
 * @brief 获取子系统名称
 * @param subsystem 子系统枚举值
 * @return 子系统名称字符串
 */
static inline const char* test_subsystem_name(test_subsystem_t subsystem) {
    if (subsystem < TEST_SUBSYSTEM_COUNT) {
        return TEST_SUBSYSTEM_NAMES[subsystem];
    }
    return "unknown";
}

/**
 * @brief 从字符串解析子系统枚举
 * @param name 子系统名称字符串
 * @return 子系统枚举值，未找到返回 TEST_SUBSYSTEM_COUNT
 */
test_subsystem_t test_subsystem_from_string(const char *name);

#endif // _TESTS_TEST_MODULE_H_
