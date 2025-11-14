/**
 * @file system.h
 * @brief 平台级系统控制（重启 / 关机）
 */

#ifndef _KERNEL_SYSTEM_H_
#define _KERNEL_SYSTEM_H_

#include <types.h>

/**
 * @brief 立即重启整个平台
 *
 * 使用键盘控制器复位并在失败时触发三重故障。
 * 该函数不会返回。
 */
void system_reboot(void) __attribute__((noreturn));

/**
 * @brief 尝试关闭电源
 *
 * 依次尝试多种常见的 QEMU/Bochs ACPI 端口。
 * 如果所有方式都失败，则进入低功耗挂起状态。
 * 该函数不会返回。
 */
void system_poweroff(void) __attribute__((noreturn));

#endif /* _KERNEL_SYSTEM_H_ */


