#ifndef _KERNEL_SYSCALLS_PROCESS_H_
#define _KERNEL_SYSCALLS_PROCESS_H_

#include <types.h>

/**
 * 进程管理系统调用
 */

/**
 * sys_exit - 退出当前进程
 * @param code 退出码
 * @note 此函数不会返回
 */
void sys_exit(uint32_t code) __attribute__((noreturn));

/**
 * sys_fork - 创建子进程
 * @return 父进程返回子进程 PID，子进程返回 0，失败返回 -1
 */
uint32_t sys_fork(uint32_t *frame);

/**
 * sys_execve - 执行新程序（替换当前进程）
 * @param path 程序路径
 * @return 成功则不返回（进程被替换），失败返回 -1
 */
uint32_t sys_execve(const char *path);

/**
 * sys_getpid - 获取当前进程 PID
 * @return 当前进程的 PID
 */
uint32_t sys_getpid(void);

/**
 * sys_yield - 主动让出 CPU
 * @return 总是返回 0
 */
uint32_t sys_yield(void);

/**
 * sys_nanosleep - 睡眠指定时间
 * @param nanoseconds 睡眠纳秒数
 * @return 0 成功，(uint32_t)-1 失败
 */
uint32_t sys_nanosleep(uint32_t nanoseconds);

#endif // _KERNEL_SYSCALLS_PROCESS_H_
