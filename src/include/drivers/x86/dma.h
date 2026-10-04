/**
 * @file dma.h
 * @brief 设备 DMA 内存分配
 *
 * 设备（网卡、USB 主机控制器）按物理地址线性读写内存，不经过页表。
 * 内核堆只保证虚拟地址连续：i686 的堆逐页向 PMM 要帧，相邻虚拟页的物理帧
 * 不一定相邻，所以堆上跨页的缓冲区不能直接交给设备。
 *
 * 这里分配的内存物理连续、按页对齐、已清零，并且位于 4GB 以下
 * （UHCI 和 e1000 legacy 描述符里的指针是 32 位或由 32 位寄存器给出）。
 */

#ifndef _DRIVERS_X86_DMA_H_
#define _DRIVERS_X86_DMA_H_

#include <types.h>
#include <mm/mm_types.h>

namespace drivers {

class Dma {
public:
    /**
     * @brief 分配 DMA 内存
     * @param size 字节数（向上取整到整页）
     * @param[out] phys 起始物理地址
     * @return 内核虚拟地址，失败返回 NULL
     */
    static void *alloc(size_t size, paddr_t *phys);

    /**
     * @brief 释放 alloc() 返回的内存
     * @param virt alloc() 的返回值
     * @param size 分配时传入的字节数
     */
    static void free(void *virt, size_t size);
};

} // namespace drivers

#endif // _DRIVERS_X86_DMA_H_
