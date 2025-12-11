// ============================================================================
// test_runner.h - 统一测试运行器
// ============================================================================
// 
// 支持多架构测试运行，提供架构特定诊断信息
// Requirements: 11.3, 11.4
// ============================================================================

#ifndef _TESTS_TEST_RUNNER_H_
#define _TESTS_TEST_RUNNER_H_

#include <types.h>

// ============================================================================
// 架构信息结构
// ============================================================================

/**
 * @brief 架构信息结构体
 * 
 * 包含当前运行架构的详细信息，用于测试诊断
 */
typedef struct {
    const char *name;           // 架构名称 (i686, x86_64, arm64)
    uint32_t bits;              // 位宽 (32 或 64)
    uint32_t page_size;         // 页大小
    uint32_t page_table_levels; // 页表级数
    uintptr_t kernel_base;      // 内核虚拟基址
    uint32_t gpr_count;         // 通用寄存器数量
    uint32_t gpr_size;          // 通用寄存器大小（字节）
} arch_info_t;

// ============================================================================
// 核心函数
// ============================================================================

/**
 * @brief 运行所有测试套件
 * 
 * 执行所有注册的测试模块，并输出架构信息和测试结果
 */
void run_all_tests(void);

/**
 * @brief 获取当前架构信息
 * 
 * @return 指向架构信息结构的指针
 */
const arch_info_t* test_get_arch_info(void);

/**
 * @brief 打印架构诊断信息
 * 
 * 输出当前架构的详细信息，用于测试诊断
 */
void test_print_arch_info(void);

/**
 * @brief 打印测试失败时的架构诊断信息
 * 
 * 在测试失败时调用，输出有助于调试的架构特定信息
 * 
 * @param test_name 失败的测试名称
 * @param file 源文件名
 * @param line 行号
 */
void test_print_failure_diagnostics(const char *test_name, 
                                     const char *file, 
                                     int line);

// ============================================================================
// 模块化测试运行器支持
// ============================================================================
// 
// 以下函数支持新的模块化测试注册机制
// **Feature: test-refactor**
// **Validates: Requirements 10.2, 12.2, 12.3, 13.1**
// ============================================================================

// 前向声明
struct test_registry;
struct test_module;
struct test_run_options;

/**
 * @brief 初始化全局测试注册表
 */
void test_runner_init_registry(void);

/**
 * @brief 获取全局测试注册表
 * @return 指向全局注册表的指针
 */
struct test_registry* test_runner_get_registry(void);

/**
 * @brief 注册一个测试模块到全局注册表
 * @param module 模块指针
 * @return 成功返回 true，失败返回 false
 */
bool test_runner_register_module(const struct test_module *module);

/**
 * @brief 运行指定子系统的测试
 * 
 * Requirements: 12.2 - 支持运行子系统内所有模块
 * 
 * @param subsystem 子系统名称 (mm, fs, net, drivers, kernel, arch, lib)
 */
void run_subsystem_tests(const char *subsystem);

/**
 * @brief 运行指定模块的测试
 * 
 * Requirements: 12.3 - 支持运行单个模块
 * 
 * @param module_name 模块名称
 */
void run_module_tests(const char *module_name);

/**
 * @brief 使用选项运行所有注册的模块化测试
 * 
 * Requirements: 13.1 - 支持选择性测试执行
 * 
 * @param options 运行选项
 */
void run_tests_with_options(const struct test_run_options *options);

/**
 * @brief 打印已注册的测试模块列表
 */
void test_runner_list_modules(void);

#endif // _TESTS_TEST_RUNNER_H_

