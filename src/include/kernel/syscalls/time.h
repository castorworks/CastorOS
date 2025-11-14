#ifndef _KERNEL_SYSCALLS_TIME_H_
#define _KERNEL_SYSCALLS_TIME_H_

#include <types.h>

/**
 * 时间相关系统调用
 */

/**
 * sys_time - 获取系统运行时间（秒）
 * @return 自系统启动以来的秒数
 */
uint32_t sys_time(void);

#endif // _KERNEL_SYSCALLS_TIME_H_

