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

#endif // _TESTS_TEST_RUNNER_H_

