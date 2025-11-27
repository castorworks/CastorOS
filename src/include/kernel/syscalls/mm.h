#ifndef _KERNEL_SYSCALLS_MM_H_
#define _KERNEL_SYSCALLS_MM_H_

#include <types.h>

/**
 * 内存管理系统调用
 */

/**
 * sys_brk - 调整堆边界
 * @param addr 新的堆结束地址（0 表示查询当前值）
 * @return 成功返回新的堆结束地址，失败返回 (uint32_t)-1
 * 
 * 用法：
 * - 调用 brk(0) 获取当前堆结束地址
 * - 调用 brk(new_addr) 扩展或收缩堆到 new_addr
 * - 堆只能在 heap_start 和 heap_max 之间调整
 */
uint32_t sys_brk(uint32_t addr);

#endif // _KERNEL_SYSCALLS_MM_H_

