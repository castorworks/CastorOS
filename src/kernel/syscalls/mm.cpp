/**
 * 内存管理相关系统调用实现
 * 
 * 实现内存管理系统调用：
 * - brk(2)
 * - mmap(2) - 只支持匿名映射
 * - munmap(2)
 */

#include <kernel/syscalls/mm.h>
#include <kernel/task.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <hal/hal.h>

/**
 * 取消当前进程一个用户页的映射并释放其物理帧。
 *
 * 只处理以用户权限映射的页：用户地址范围内也可能存在内核自己的映射
 * （arm64 的内核恒等映射块就在 TTBR0 一侧），那些页帧不属于进程，不能释放。
 *
 * @return 是否释放了一页
 */
static bool unmap_user_page(task_t *task, uintptr_t page) {
    uint32_t flags = 0;
    if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)page, NULL, &flags) ||
        !(flags & HAL_PAGE_USER)) {
        return false;
    }
    uintptr_t phys = mm::Vmm::unmap_page_in_directory(task->page_dir_phys, page);
    if (!phys) {
        return false;
    }
    mm::Pmm::free_frame(phys);
    return true;
}

/* mmap 区域的起始和结束地址（在堆和栈之间） */
#if defined(ARCH_ARM64)
/* arm64：每个用户地址空间的 0x40000000-0xFFFFFFFF 是内核 RAM 的 1GB 块映射，
 * 4GB 以下还有设备块，所以 mmap 区域放在 4GB 以上 */
#define MMAP_REGION_START   ((uintptr_t)0x100000000ULL)  /* 4GB 起始 */
#define MMAP_REGION_END     ((uintptr_t)0x140000000ULL)  /* 5GB 结束 */
#else
#define MMAP_REGION_START   ((uintptr_t)0x40000000)  /* 1GB 起始 */
#define MMAP_REGION_END     ((uintptr_t)0x70000000)  /* 1.75GB 结束 */
#endif

/** 当前进程的地址空间里 page 这一页是否已有映射（任何大小、任何权限） */
static bool is_page_mapped(uintptr_t page) {
    return hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)page, NULL, NULL);
}

/**
 * syscall::Mm::brk - 调整堆边界
 * @param addr 新的堆结束地址（0 表示查询当前值）
 * @return 成功返回新的堆结束地址，失败返回 (uintptr_t)-1
 */
uintptr_t syscall::Mm::brk(uint32_t addr) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Mm::brk: no current task\n");
        return (uintptr_t)-1;
    }
    
    // 如果不是用户进程，返回错误
    if (!current->is_user_process) {
        LOG_ERROR_MSG("syscall::Mm::brk: not a user process\n");
        return (uintptr_t)-1;
    }
    
    // 如果 addr 为 0，返回当前堆结束地址
    if (addr == 0) {
        LOG_DEBUG_MSG("syscall::Mm::brk: returning current heap_end=0x%x\n", current->heap_end);
        return current->heap_end;
    }
    
    // 验证地址范围
    if (addr < current->heap_start) {
        LOG_ERROR_MSG("syscall::Mm::brk: addr 0x%x below heap_start 0x%x\n", 
                      addr, current->heap_start);
        return (uintptr_t)-1;
    }
    
    if (addr > current->heap_max) {
        LOG_ERROR_MSG("syscall::Mm::brk: addr 0x%x exceeds heap_max 0x%x\n", 
                      addr, current->heap_max);
        return (uintptr_t)-1;
    }
    
    uint32_t old_end = current->heap_end;
    uint32_t old_end_aligned = PAGE_ALIGN_UP(old_end);
    uint32_t new_end_aligned = PAGE_ALIGN_UP(addr);
    
    LOG_DEBUG_MSG("syscall::Mm::brk: old_end=0x%x (aligned 0x%x), new_end=0x%x (aligned 0x%x)\n",
                  old_end, old_end_aligned, addr, new_end_aligned);
    
    if (new_end_aligned > old_end_aligned) {
        // 扩展堆：分配并映射新页面
        LOG_DEBUG_MSG("syscall::Mm::brk: expanding heap from 0x%x to 0x%x\n", 
                      old_end_aligned, new_end_aligned);
        
        for (uint32_t page = old_end_aligned; page < new_end_aligned; page += PAGE_SIZE) {
            // 堆不能长进已有的映射（mmap 区域、内核块映射）：直接映射会覆盖
            // 原有页表项并泄漏那一页
            if (is_page_mapped(page)) {
                LOG_ERROR_MSG("syscall::Mm::brk: page 0x%x is already mapped\n", page);
                current->heap_end = page;
                return current->heap_end;
            }

            // 分配物理页
            paddr_t phys = mm::Pmm::alloc_frame();
            if (phys == PADDR_INVALID) {
                LOG_ERROR_MSG("syscall::Mm::brk: out of memory at page 0x%x\n", page);
                // 不回滚，保持已分配的页面
                // 返回实际达到的地址
                current->heap_end = page;
                return current->heap_end;
            }
            
            // 先通过内核地址清零物理页（在映射之前）
            memset((void *)PHYS_TO_VIRT((uintptr_t)phys), 0, PAGE_SIZE);
            
            // 映射到用户空间（可读写）
            if (!mm::Vmm::map_page_in_directory(current->page_dir_phys, page, (uintptr_t)phys,
                                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
                mm::Pmm::free_frame(phys);
                LOG_ERROR_MSG("syscall::Mm::brk: failed to map page 0x%x\n", page);
                // 返回实际达到的地址
                current->heap_end = page;
                return current->heap_end;
            }
            
            LOG_DEBUG_MSG("syscall::Mm::brk: mapped page 0x%x -> phys 0x%llx\n", page, (unsigned long long)phys);
        }
    } else if (new_end_aligned < old_end_aligned) {
        // 收缩堆：取消映射并释放页面
        LOG_DEBUG_MSG("syscall::Mm::brk: shrinking heap from 0x%x to 0x%x\n", 
                      old_end_aligned, new_end_aligned);
        
        for (uint32_t page = new_end_aligned; page < old_end_aligned; page += PAGE_SIZE) {
            if (unmap_user_page(current, page)) {
                LOG_DEBUG_MSG("syscall::Mm::brk: unmapped page 0x%x\n", page);
            }
        }
    }
    
    current->heap_end = addr;
    
    LOG_DEBUG_MSG("syscall::Mm::brk: heap extended to 0x%x\n", addr);
    
    return addr;
}

/* ============================================================================
 * mmap/munmap 实现
 * ============================================================================ */

/**
 * 检查虚拟地址范围是否空闲（未映射）
 * @param start 起始虚拟地址（页对齐）
 * @param length 长度（页对齐）
 * @param mapped_at 输出：范围内最后一个已映射页的地址（返回 false 时有效）
 * @return 如果整个范围都未映射返回 true
 *
 * 通过 HAL 查询当前进程的页表，所以对每种架构的页表格式都成立。
 */
static bool is_vaddr_range_free(uintptr_t start, size_t length, uintptr_t *mapped_at) {
    // 从后往前查：调用者可以直接跳过最后一个已映射页之前的所有起点
    for (uintptr_t addr = start + length; addr > start; ) {
        addr -= PAGE_SIZE;
        if (is_page_mapped(addr)) {
            if (mapped_at) *mapped_at = addr;
            return false;
        }
    }
    return true;
}

/**
 * 在 mmap 区域查找空闲的虚拟地址空间
 * @param hint 建议地址（0 表示由内核选择）
 * @param length 需要的长度（已页对齐，不超过 mmap 区域大小）
 * @return 找到的虚拟地址，失败返回 0
 */
static uintptr_t find_free_vaddr(uintptr_t hint, size_t length) {
    // 如果提供了 hint 且在有效范围内，先尝试 hint 地址
    if (hint != 0 && hint <= MMAP_REGION_END - length) {
        uintptr_t start = PAGE_ALIGN_UP(hint);
        if (start >= MMAP_REGION_START && start <= MMAP_REGION_END - length) {
            if (is_vaddr_range_free(start, length, NULL)) {
                return start;
            }
        }
    }

    // 从 mmap 区域开始线性搜索
    uintptr_t start = MMAP_REGION_START;
    while (start <= MMAP_REGION_END - length) {
        uintptr_t mapped_at = 0;
        if (is_vaddr_range_free(start, length, &mapped_at)) {
            return start;
        }
        start = mapped_at + PAGE_SIZE;
    }

    return 0;  // 没有找到足够大的空闲区域
}

/**
 * 执行匿名映射
 */
static uintptr_t do_mmap_anonymous(task_t *current, uintptr_t vaddr, size_t length,
                                   uint32_t page_flags) {
    uint32_t pages_allocated = 0;
    
    for (uintptr_t page = vaddr; page < vaddr + length; page += PAGE_SIZE) {
        // 分配物理页
        paddr_t phys = mm::Pmm::alloc_frame();
        if (phys == PADDR_INVALID) {
            LOG_ERROR_MSG("syscall::Mm::mmap: out of memory at page 0x%llx\n", (unsigned long long)page);
            // 回滚已分配的页面
            for (uintptr_t p = vaddr; p < page; p += PAGE_SIZE) {
                uintptr_t pf = mm::Vmm::unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) {
                    mm::Pmm::free_frame((paddr_t)pf);
                }
            }
            return (uintptr_t)-1;
        }
        
        // 先通过内核地址清零物理页（在映射之前）
        // 这样可以避免内核访问用户空间地址的问题
        memset((void *)PHYS_TO_VIRT((uintptr_t)phys), 0, PAGE_SIZE);
        
        // 映射到用户空间
        if (!mm::Vmm::map_page_in_directory(current->page_dir_phys, page, (uintptr_t)phys, page_flags)) {
            mm::Pmm::free_frame(phys);
            LOG_ERROR_MSG("syscall::Mm::mmap: failed to map page 0x%llx\n", (unsigned long long)page);
            // 回滚已分配的页面
            for (uintptr_t p = vaddr; p < page; p += PAGE_SIZE) {
                uintptr_t pf = mm::Vmm::unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) {
                    mm::Pmm::free_frame((paddr_t)pf);
                }
            }
            return (uintptr_t)-1;
        }
        
        pages_allocated++;
    }
    
    LOG_DEBUG_MSG("syscall::Mm::mmap: anonymous mapped 0x%llx bytes at 0x%llx (%u pages)\n",
                  (unsigned long long)length, (unsigned long long)vaddr, pages_allocated);
    
    return vaddr;
}

/**
 * syscall::Mm::mmap - 内存映射（只支持匿名映射）
 * @param addr 建议的映射地址（0 表示由内核选择）
 * @param length 映射长度
 * @param prot 保护标志
 * @param flags 映射标志
 * @param fd 文件描述符（匿名映射时应为 -1）
 * @param offset 文件偏移（匿名映射时忽略）
 * @return 成功返回映射的虚拟地址，失败返回 (uintptr_t)-1
 */
uintptr_t syscall::Mm::mmap(uintptr_t addr, size_t length, uint32_t prot,
                  uint32_t flags, int32_t fd, uint32_t offset) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Mm::mmap: no current task\n");
        return (uintptr_t)-1;
    }
    
    // 检查是否为用户进程
    if (!current->is_user_process) {
        LOG_ERROR_MSG("syscall::Mm::mmap: not a user process\n");
        return (uintptr_t)-1;
    }
    
    if (length == 0) {
        LOG_ERROR_MSG("syscall::Mm::mmap: invalid length 0\n");
        return (uintptr_t)-1;
    }

    // 检查长度是否超出限制（在对齐之前检查，对齐不会回绕）
    if (length > MMAP_REGION_END - MMAP_REGION_START) {
        LOG_ERROR_MSG("syscall::Mm::mmap: length 0x%llx too large\n", (unsigned long long)length);
        return (uintptr_t)-1;
    }

    // 对齐长度
    length = PAGE_ALIGN_UP(length);
    
    // 内核里没有文件的概念：只支持匿名映射
    (void)fd; (void)offset;
    if (!(flags & MAP_ANONYMOUS)) {
        LOG_ERROR_MSG("syscall::Mm::mmap: only anonymous mappings are supported\n");
        return (uintptr_t)-1;
    }

    // 查找空闲虚拟地址空间
    uintptr_t vaddr = find_free_vaddr(addr, length);
    if (vaddr == 0) {
        LOG_ERROR_MSG("syscall::Mm::mmap: no free virtual address space for length 0x%llx\n",
                      (unsigned long long)length);
        return (uintptr_t)-1;
    }
    
    // 确定页面标志
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if (prot & PROT_WRITE) {
        page_flags |= PAGE_WRITE;
    }
    
    return do_mmap_anonymous(current, vaddr, length, page_flags);
}

/**
 * syscall::Mm::munmap - 取消内存映射
 * @param addr 映射起始地址
 * @param length 取消映射的长度
 * @return 成功返回 0，失败返回 (uintptr_t)-1
 */
uintptr_t syscall::Mm::munmap(uintptr_t addr, size_t length) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Mm::munmap: no current task\n");
        return (uintptr_t)-1;
    }

    // 检查是否为用户进程
    if (!current->is_user_process) {
        LOG_ERROR_MSG("syscall::Mm::munmap: not a user process\n");
        return (uintptr_t)-1;
    }

    if (length == 0) {
        return 0;  // 无需操作
    }

    // 检查地址范围是否有效（在用户空间内）；先于对齐检查，避免相加回绕
    if (addr >= USER_SPACE_END || length > USER_SPACE_END - addr) {
        LOG_ERROR_MSG("syscall::Mm::munmap: range 0x%llx+0x%llx out of user space\n",
                      (unsigned long long)addr, (unsigned long long)length);
        return (uintptr_t)-1;
    }

    // 逐页处理，长度上限保持为原来 32 位参数能表示的范围，
    // 避免 64 位架构上一次调用在内核里遍历整个用户地址空间
#if !defined(ARCH_I686)
    if (length > 0xFFFFFFFFULL) {
        LOG_ERROR_MSG("syscall::Mm::munmap: length 0x%llx too large\n", (unsigned long long)length);
        return (uintptr_t)-1;
    }
#endif

    // 对齐地址和长度
    uintptr_t aligned_addr = PAGE_ALIGN_DOWN(addr);
    length = PAGE_ALIGN_UP(length + (addr - aligned_addr));

    LOG_DEBUG_MSG("syscall::Mm::munmap: addr=0x%llx, length=0x%llx\n",
                  (unsigned long long)aligned_addr, (unsigned long long)length);

    // 取消映射并释放物理页
    uint32_t pages_freed = 0;
    for (uintptr_t page = aligned_addr; page < aligned_addr + length; page += PAGE_SIZE) {
        if (unmap_user_page(current, page)) {
            pages_freed++;
        }
    }
    
    LOG_DEBUG_MSG("syscall::Mm::munmap: unmapped %u pages\n", pages_freed);
    
    return 0;
}

