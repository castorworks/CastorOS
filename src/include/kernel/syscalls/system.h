#ifndef _KERNEL_SYSCALLS_SYSTEM_H_
#define _KERNEL_SYSCALLS_SYSTEM_H_

#include <types.h>

struct utsname;  // 前向声明

/**
 * sys_reboot - 重启系统
 * @return 不返回（成功时系统重启）
 */
uint32_t sys_reboot(void);

/**
 * sys_poweroff - 关闭系统
 * @return 不返回（成功时系统关机）
 */
uint32_t sys_poweroff(void);

/**
 * sys_uname - 获取系统信息
 * @param buf 用户空间 utsname 结构体指针
 * @return 0 成功，(uint32_t)-1 失败
 */
uint32_t sys_uname(struct utsname *buf);

#endif /* _KERNEL_SYSCALLS_SYSTEM_H_ */


