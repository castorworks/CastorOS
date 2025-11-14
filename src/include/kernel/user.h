#ifndef _KERNEL_USER_H_
#define _KERNEL_USER_H_

#include <types.h>

/**
 * 用户模式支持
 * 
 * 实现从内核态（Ring 0）切换到用户态（Ring 3）
 */

/**
 * 进入用户模式
 * @param entry_point 用户程序入口地址（虚拟地址）
 * @param user_stack 用户栈顶地址（虚拟地址）
 * 
 * 此函数通过构造一个"伪造的"中断返回栈帧，
 * 使用 IRET 指令实现特权级切换。
 * 
 * 注意：此函数不会返回！
 */
void enter_usermode(uint32_t entry_point, uint32_t user_stack) __attribute__((noreturn));

/**
 * 获取用户模式包装器的地址
 * 用于设置用户进程的初始 EIP
 */
uint32_t get_usermode_wrapper(void);

#endif // _KERNEL_USER_H_
