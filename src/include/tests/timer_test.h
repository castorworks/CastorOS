// ============================================================================
// timer_test.h - Timer 驱动测试模块头文件
// ============================================================================
//
// Timer 驱动测试，验证 tick 计数和回调调用
//
// **Feature: test-refactor**
// **Validates: Requirements 6.3**
//
// 测试覆盖:
//   - Timer tick 计数
//   - Timer 回调注册和取消
//   - Timer 频率和运行时间计算
//   - Timer 回调调用验证
// ============================================================================

#ifndef _TESTS_TIMER_TEST_H_
#define _TESTS_TIMER_TEST_H_

/**
 * @brief 运行所有 Timer 驱动测试
 * 
 * 测试 Timer tick 计数和回调调用
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 6.3**
 */
void run_timer_tests(void);

#endif // _TESTS_TIMER_TEST_H_
