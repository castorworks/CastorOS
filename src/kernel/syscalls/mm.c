/**
 * 内存管理相关系统调用实现
 * 
 * 实现堆内存管理系统调用：
 * - brk(2)
 */

#include <kernel/syscalls/mm.h>
#include <kernel/task.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>

/**
 * sys_brk - 调整堆边界
 * @param addr 新的堆结束地址（0 表示查询当前值）
 * @return 成功返回新的堆结束地址，失败返回 (uint32_t)-1
 */
uint32_t sys_brk(uint32_t addr) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_brk: no current task\n");
        return (uint32_t)-1;
    }
    
    // 如果不是用户进程，返回错误
    if (!current->is_user_process) {
        LOG_ERROR_MSG("sys_brk: not a user process\n");
        return (uint32_t)-1;
    }
    
    // 如果 addr 为 0，返回当前堆结束地址
    if (addr == 0) {
        LOG_DEBUG_MSG("sys_brk: returning current heap_end=0x%x\n", current->heap_end);
        return current->heap_end;
    }
    
    // 验证地址范围
    if (addr < current->heap_start) {
        LOG_ERROR_MSG("sys_brk: addr 0x%x below heap_start 0x%x\n", 
                      addr, current->heap_start);
        return (uint32_t)-1;
    }
    
    if (addr > current->heap_max) {
        LOG_ERROR_MSG("sys_brk: addr 0x%x exceeds heap_max 0x%x\n", 
                      addr, current->heap_max);
        return (uint32_t)-1;
    }
    
    uint32_t old_end = current->heap_end;
    uint32_t old_end_aligned = PAGE_ALIGN_UP(old_end);
    uint32_t new_end_aligned = PAGE_ALIGN_UP(addr);
    
    LOG_DEBUG_MSG("sys_brk: old_end=0x%x (aligned 0x%x), new_end=0x%x (aligned 0x%x)\n",
                  old_end, old_end_aligned, addr, new_end_aligned);
    
    if (new_end_aligned > old_end_aligned) {
        // 扩展堆：分配并映射新页面
        LOG_DEBUG_MSG("sys_brk: expanding heap from 0x%x to 0x%x\n", 
                      old_end_aligned, new_end_aligned);
        
        for (uint32_t page = old_end_aligned; page < new_end_aligned; page += PAGE_SIZE) {
            // 分配物理页
            uint32_t phys = pmm_alloc_frame();
            if (!phys) {
                LOG_ERROR_MSG("sys_brk: out of memory at page 0x%x\n", page);
                // 不回滚，保持已分配的页面
                // 返回实际达到的地址
                current->heap_end = page;
                return current->heap_end;
            }
            
            // 映射到用户空间（可读写）
            if (!vmm_map_page_in_directory(current->page_dir_phys, page, phys,
                                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
                pmm_free_frame(phys);
                LOG_ERROR_MSG("sys_brk: failed to map page 0x%x\n", page);
                // 返回实际达到的地址
                current->heap_end = page;
                return current->heap_end;
            }
            
            // 清零新页面
            // 注意：需要临时切换到目标页目录来访问用户空间地址
            // 由于当前进程就是目标进程，页面已经映射到当前地址空间
            memset((void *)page, 0, PAGE_SIZE);
            
            LOG_DEBUG_MSG("sys_brk: mapped page 0x%x -> phys 0x%x\n", page, phys);
        }
    } else if (new_end_aligned < old_end_aligned) {
        // 收缩堆：取消映射并释放页面
        LOG_DEBUG_MSG("sys_brk: shrinking heap from 0x%x to 0x%x\n", 
                      old_end_aligned, new_end_aligned);
        
        for (uint32_t page = new_end_aligned; page < old_end_aligned; page += PAGE_SIZE) {
            uint32_t phys = vmm_unmap_page_in_directory(current->page_dir_phys, page);
            if (phys) {
                pmm_free_frame(phys);
                LOG_DEBUG_MSG("sys_brk: unmapped page 0x%x (phys 0x%x)\n", page, phys);
            }
        }
    }
    
    current->heap_end = addr;
    
    LOG_DEBUG_MSG("sys_brk: heap extended to 0x%x\n", addr);
    
    return addr;
}

