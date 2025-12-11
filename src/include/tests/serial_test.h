// ============================================================================
// serial_test.h - Serial 驱动测试模块头文件
// ============================================================================
//
// Serial 串口驱动测试，验证字符发送和接收功能
//
// **Feature: test-refactor**
// **Validates: Requirements 6.4**
//
// 测试覆盖:
//   - Serial 初始化
//   - 字符发送 (serial_putchar)
//   - 字符串发送 (serial_print)
//   - 字符接收 (ARM64 only)
//   - 特殊字符处理
// ============================================================================

#ifndef _TESTS_SERIAL_TEST_H_
#define _TESTS_SERIAL_TEST_H_

/**
 * @brief 运行所有 Serial 驱动测试
 * 
 * 测试串口字符发送和接收功能
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 6.4**
 */
void run_serial_tests(void);

#endif // _TESTS_SERIAL_TEST_H_
