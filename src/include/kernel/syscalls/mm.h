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

/**
 * sys_mmap - 内存映射（简化版：仅支持匿名映射）
 * @param addr 建议的映射地址（0 表示由内核选择）
 * @param length 映射长度（字节，会被页对齐）
 * @param prot 保护标志（PROT_READ, PROT_WRITE, PROT_EXEC）
 * @param flags 映射标志（必须包含 MAP_ANONYMOUS）
 * @param fd 文件描述符（匿名映射时忽略，应传 -1）
 * @param offset 文件偏移（匿名映射时忽略，应传 0）
 * @return 成功返回映射的虚拟地址，失败返回 (uint32_t)-1 (MAP_FAILED)
 * 
 * 当前限制：
 * - 仅支持匿名映射（flags 必须包含 MAP_ANONYMOUS）
 * - 不支持文件映射
 * - 不支持共享映射（MAP_SHARED）
 * 
 * 用法示例：
 *   void *p = mmap(NULL, 4096, PROT_READ|PROT_WRITE, 
 *                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
 */
uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot,
                  uint32_t flags, int32_t fd, uint32_t offset);

/**
 * sys_munmap - 取消内存映射
 * @param addr 映射起始地址（必须页对齐）
 * @param length 取消映射的长度（字节，会被页对齐）
 * @return 成功返回 0，失败返回 (uint32_t)-1
 * 
 * 注意：
 * - addr 必须是页对齐的地址
 * - 会释放指定范围内的所有物理页
 */
uint32_t sys_munmap(uint32_t addr, uint32_t length);

#endif // _KERNEL_SYSCALLS_MM_H_

