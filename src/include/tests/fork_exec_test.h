// ============================================================================
// fork_exec_test.h - Fork/Exec 系统调用验证测试头文件
// ============================================================================

#ifndef _FORK_EXEC_TEST_H_
#define _FORK_EXEC_TEST_H_

/**
 * @brief 运行所有 fork/exec 验证测试
 * 
 * 包括：
 *   - Task 36.1: Fork 系统调用测试 (hal_mmu_clone_space COW)
 *   - Task 36.2: Exec 系统调用测试 (程序加载)
 * 
 * **Feature: multi-arch-support**
 * **Validates: Requirements 5.5, 7.4, mm-refactor 4.4, 5.3**
 */
void run_fork_exec_tests(void);

#endif // _FORK_EXEC_TEST_H_
