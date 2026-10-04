/**
 * @file dma.cpp
 * @brief 设备 DMA 内存分配（物理连续页）
 */

#include <drivers/x86/dma.h>
#include <mm/pmm.h>
#include <lib/klog.h>

/* 设备能寻址的上限：32 位物理地址 */
#define DMA_ADDR_LIMIT  0x100000000ULL

static size_t dma_page_count(size_t size) {
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

void *drivers::Dma::alloc(size_t size, paddr_t *phys) {
    if (size == 0 || !phys) {
        return NULL;
    }

    size_t pages = dma_page_count(size);
    if (pages == 0) {
        return NULL;  // size 接近 SIZE_MAX，取整时回绕
    }

    // alloc_frames 返回的帧已清零，并且都在内核的物理内存直接映射范围内
    // （PMM 自己就是通过 PHYS_TO_VIRT 清零的）
    paddr_t addr = mm::Pmm::alloc_frames(pages);
    if (addr == PADDR_INVALID) {
        LOG_ERROR_MSG("dma: Failed to allocate %u contiguous page(s)\n", (unsigned int)pages);
        return NULL;
    }

    if (addr + (paddr_t)pages * PAGE_SIZE > DMA_ADDR_LIMIT) {
        LOG_ERROR_MSG("dma: Frames at 0x%llx are above 4GB\n", (unsigned long long)addr);
        mm::Pmm::free_frames(addr, pages);
        return NULL;
    }

    *phys = addr;
    return (void *)PHYS_TO_VIRT((uintptr_t)addr);
}

void drivers::Dma::free(void *virt, size_t size) {
    if (!virt || size == 0) {
        return;
    }
    mm::Pmm::free_frames((paddr_t)VIRT_TO_PHYS((uintptr_t)virt), dma_page_count(size));
}
