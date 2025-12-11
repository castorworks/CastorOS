/**
 * @file tcp_test.h
 * @brief TCP 协议模块单元测试头文件
 * 
 * 声明 TCP 协议测试函数和相关工具
 */

#ifndef _TESTS_TCP_TEST_H_
#define _TESTS_TCP_TEST_H_

/**
 * @brief 运行所有 TCP 协议测试
 * 
 * 执行 TCP 头部构造、标志位提取、序列号处理、校验和计算等测试
 */
void run_tcp_tests(void);

#endif // _TESTS_TCP_TEST_H_
