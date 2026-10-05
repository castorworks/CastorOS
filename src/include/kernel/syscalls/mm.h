#ifndef _KERNEL_SYSCALLS_MM_H_
#define _KERNEL_SYSCALLS_MM_H_

#include <types.h>

namespace syscall {

/**
 * @brief 内存管理相关系统调用
 */
class Mm {
public:
    /**
     * 内存管理系统调用
     */

    /**
     * syscall::Mm::brk - 调整堆边界
     * @param addr 新的堆结束地址（0 表示查询当前值）
     * @return 成功返回新的堆结束地址，失败返回 (uintptr_t)-1
     * 
     * 用法：
     * - 调用 brk(0) 获取当前堆结束地址
     * - 调用 brk(new_addr) 扩展或收缩堆到 new_addr
     * - 堆只能在 heap_start 和 heap_max 之间调整
     */
    static uintptr_t brk(uint32_t addr);

    /**
     * syscall::Mm::mmap - 内存映射（匿名映射和私有文件映射）
     * @param addr 建议的映射地址（0 表示由内核选择）
     * @param length 映射长度（字节，会被页对齐）
     * @param prot 保护标志（PROT_READ, PROT_WRITE, PROT_EXEC）
     * @param flags 映射标志（必须包含 MAP_ANONYMOUS）
     * @param fd 忽略，应传 -1
     * @param offset 忽略，应传 0
     * @return 成功返回映射的虚拟地址，失败返回 (uintptr_t)-1 (MAP_FAILED)
     * 
     * 当前限制：
     * - 只支持匿名映射（内核里没有文件）
     * - 映射地址由内核在各架构的 mmap 区域内选择，addr 只是建议
     * 
     * 用法示例：
     *   void *p = mmap(NULL, 4096, PROT_READ|PROT_WRITE, 
     *                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
     */
    static uintptr_t mmap(uintptr_t addr, size_t length, uint32_t prot,
                      uint32_t flags, int32_t fd, uint32_t offset);

    /**
     * syscall::Mm::munmap - 取消内存映射
     * @param addr 映射起始地址（必须页对齐）
     * @param length 取消映射的长度（字节，会被页对齐）
     * @return 成功返回 0，失败返回 (uintptr_t)-1
     * 
     * 注意：
     * - addr 必须是页对齐的地址
     * - 会释放指定范围内的所有物理页
     */
    static uintptr_t munmap(uintptr_t addr, size_t length);

    /**
     * 把设备内存 [phys, phys+length) 映射进当前进程（调用者已确认是特权进程）
     * @param phys 设备内存的物理地址，必须页对齐，且整个区间不属于普通内存
     * @return 映射的虚拟地址，失败返回 (uintptr_t)-1
     */
    static uintptr_t map_device(uint64_t phys, size_t length);

    /**
     * 把当前进程的 [addr, addr+length) 共享给进程 pid：同一批物理页同时映射在
     * 两个进程里，地址由内核在对方的 mmap 区域里选择
     * @param addr 必须页对齐，区间内的页必须已映射且可写
     * @return 这段内存在对方地址空间里的虚拟地址，失败返回 (uintptr_t)-1
     */
    static uintptr_t grant(uint32_t pid, uintptr_t addr, size_t length);
};

} // namespace syscall

#endif // _KERNEL_SYSCALLS_MM_H_
