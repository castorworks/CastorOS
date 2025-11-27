/**
 * 内存管理相关系统调用实现
 * 
 * 实现内存管理系统调用：
 * - brk(2)
 * - mmap(2) - 支持匿名映射和文件映射
 * - munmap(2)
 */

#include <kernel/syscalls/mm.h>
#include <kernel/syscalls/fs.h>  // O_RDONLY, O_WRONLY, O_RDWR
#include <kernel/task.h>
#include <kernel/fd_table.h>
#include <fs/vfs.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>

/* mmap 区域的起始和结束地址（在堆和栈之间） */
#define MMAP_REGION_START   0x40000000  /* 1GB 起始 */
#define MMAP_REGION_END     0x70000000  /* 1.75GB 结束 */

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
            
            // 先通过内核地址清零物理页（在映射之前）
            memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
            
            // 映射到用户空间（可读写）
            if (!vmm_map_page_in_directory(current->page_dir_phys, page, phys,
                                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
                pmm_free_frame(phys);
                LOG_ERROR_MSG("sys_brk: failed to map page 0x%x\n", page);
                // 返回实际达到的地址
                current->heap_end = page;
                return current->heap_end;
            }
            
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

/* ============================================================================
 * mmap/munmap 实现
 * ============================================================================ */

/**
 * 检查虚拟地址范围是否空闲（未映射）
 * @param task 目标任务
 * @param start 起始虚拟地址（页对齐）
 * @param length 长度（页对齐）
 * @return 如果整个范围都未映射返回 true
 */
static bool is_vaddr_range_free(task_t *task, uint32_t start, uint32_t length) {
    // 获取页目录虚拟地址
    page_directory_t *pd = (page_directory_t *)PHYS_TO_VIRT(task->page_dir_phys);
    
    for (uint32_t addr = start; addr < start + length; addr += PAGE_SIZE) {
        uint32_t pd_idx = addr >> 22;
        uint32_t pt_idx = (addr >> 12) & 0x3FF;
        
        // 检查页目录项是否存在
        if (!(pd->entries[pd_idx] & PAGE_PRESENT)) {
            continue;  // 页目录项不存在，该区域未映射
        }
        
        // 获取页表
        uint32_t pt_phys = pd->entries[pd_idx] & PAGE_MASK;
        page_table_t *pt = (page_table_t *)PHYS_TO_VIRT(pt_phys);
        
        // 检查页表项是否存在
        if (pt->entries[pt_idx] & PAGE_PRESENT) {
            return false;  // 页面已映射
        }
    }
    
    return true;
}

/**
 * 在 mmap 区域查找空闲的虚拟地址空间
 * @param task 目标任务
 * @param hint 建议地址（0 表示由内核选择）
 * @param length 需要的长度（已页对齐）
 * @return 找到的虚拟地址，失败返回 0
 */
static uint32_t find_free_vaddr(task_t *task, uint32_t hint, uint32_t length) {
    uint32_t start;
    
    // 如果提供了 hint 且在有效范围内，先尝试 hint 地址
    if (hint != 0) {
        start = PAGE_ALIGN_UP(hint);
        if (start >= MMAP_REGION_START && start + length <= MMAP_REGION_END) {
            if (is_vaddr_range_free(task, start, length)) {
                return start;
            }
        }
    }
    
    // 从 mmap 区域开始线性搜索
    for (start = MMAP_REGION_START; start + length <= MMAP_REGION_END; start += PAGE_SIZE) {
        if (is_vaddr_range_free(task, start, length)) {
            return start;
        }
    }
    
    return 0;  // 没有找到足够大的空闲区域
}

/**
 * 执行匿名映射
 */
static uint32_t do_mmap_anonymous(task_t *current, uint32_t vaddr, uint32_t length,
                                   uint32_t page_flags) {
    uint32_t pages_allocated = 0;
    
    for (uint32_t page = vaddr; page < vaddr + length; page += PAGE_SIZE) {
        // 分配物理页
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            LOG_ERROR_MSG("sys_mmap: out of memory at page 0x%x\n", page);
            // 回滚已分配的页面
            for (uint32_t p = vaddr; p < page; p += PAGE_SIZE) {
                uint32_t pf = vmm_unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) {
                    pmm_free_frame(pf);
                }
            }
            return (uint32_t)-1;
        }
        
        // 先通过内核地址清零物理页（在映射之前）
        // 这样可以避免内核访问用户空间地址的问题
        memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        
        // 映射到用户空间
        if (!vmm_map_page_in_directory(current->page_dir_phys, page, phys, page_flags)) {
            pmm_free_frame(phys);
            LOG_ERROR_MSG("sys_mmap: failed to map page 0x%x\n", page);
            // 回滚已分配的页面
            for (uint32_t p = vaddr; p < page; p += PAGE_SIZE) {
                uint32_t pf = vmm_unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) {
                    pmm_free_frame(pf);
                }
            }
            return (uint32_t)-1;
        }
        
        pages_allocated++;
    }
    
    LOG_DEBUG_MSG("sys_mmap: anonymous mapped 0x%x bytes at 0x%x (%u pages)\n", 
                  length, vaddr, pages_allocated);
    
    return vaddr;
}

/**
 * 执行文件映射
 * @param current 当前进程
 * @param vaddr 映射的虚拟地址
 * @param length 映射长度（已页对齐）
 * @param page_flags 页面标志
 * @param node 文件节点
 * @param offset 文件偏移
 * @param is_private 是否为私有映射（MAP_PRIVATE）
 * @return 成功返回虚拟地址，失败返回 -1
 */
static uint32_t do_mmap_file(task_t *current, uint32_t vaddr, uint32_t length,
                              uint32_t page_flags, fs_node_t *node, uint32_t offset,
                              bool is_private) {
    uint32_t pages_allocated = 0;
    uint32_t file_offset = offset;
    uint32_t file_size = node->size;
    
    (void)is_private;  // 目前简化实现，所有文件映射都当作私有处理
    
    for (uint32_t page = vaddr; page < vaddr + length; page += PAGE_SIZE) {
        // 分配物理页
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            LOG_ERROR_MSG("sys_mmap: out of memory at page 0x%x\n", page);
            // 回滚
            for (uint32_t p = vaddr; p < page; p += PAGE_SIZE) {
                uint32_t pf = vmm_unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) {
                    pmm_free_frame(pf);
                }
            }
            return (uint32_t)-1;
        }
        
        // 获取内核地址以便操作物理页
        uint8_t *kernel_ptr = (uint8_t *)PHYS_TO_VIRT(phys);
        
        // 先清零页面（通过内核地址）
        memset(kernel_ptr, 0, PAGE_SIZE);
        
        // 读取文件内容到页面（通过内核地址）
        if (file_offset < file_size) {
            uint32_t read_size = PAGE_SIZE;
            if (file_offset + read_size > file_size) {
                read_size = file_size - file_offset;
            }
            
            // 使用 VFS 读取文件内容
            uint32_t bytes_read = vfs_read(node, file_offset, read_size, kernel_ptr);
            if (bytes_read == 0 && read_size > 0) {
                LOG_WARN_MSG("sys_mmap: failed to read file at offset 0x%x\n", file_offset);
            }
            
            LOG_DEBUG_MSG("sys_mmap: read %u bytes from file offset 0x%x to page 0x%x\n",
                          bytes_read, file_offset, page);
        }
        // 超出文件大小的部分保持为 0
        
        // 映射到用户空间（在填充数据之后）
        if (!vmm_map_page_in_directory(current->page_dir_phys, page, phys, page_flags)) {
            pmm_free_frame(phys);
            LOG_ERROR_MSG("sys_mmap: failed to map page 0x%x\n", page);
            // 回滚
            for (uint32_t p = vaddr; p < page; p += PAGE_SIZE) {
                uint32_t pf = vmm_unmap_page_in_directory(current->page_dir_phys, p);
                if (pf) {
                    pmm_free_frame(pf);
                }
            }
            return (uint32_t)-1;
        }
        
        file_offset += PAGE_SIZE;
        pages_allocated++;
    }
    
    LOG_DEBUG_MSG("sys_mmap: file mapped 0x%x bytes at 0x%x (%u pages)\n", 
                  length, vaddr, pages_allocated);
    
    return vaddr;
}

/**
 * sys_mmap - 内存映射（支持匿名映射和文件映射）
 * @param addr 建议的映射地址（0 表示由内核选择）
 * @param length 映射长度
 * @param prot 保护标志
 * @param flags 映射标志
 * @param fd 文件描述符（匿名映射时应为 -1）
 * @param offset 文件偏移（匿名映射时忽略）
 * @return 成功返回映射的虚拟地址，失败返回 (uint32_t)-1
 */
uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot,
                  uint32_t flags, int32_t fd, uint32_t offset) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_mmap: no current task\n");
        return (uint32_t)-1;
    }
    
    // 检查是否为用户进程
    if (!current->is_user_process) {
        LOG_ERROR_MSG("sys_mmap: not a user process\n");
        return (uint32_t)-1;
    }
    
    // 对齐长度
    length = PAGE_ALIGN_UP(length);
    if (length == 0) {
        LOG_ERROR_MSG("sys_mmap: invalid length 0\n");
        return (uint32_t)-1;
    }
    
    // 检查长度是否超出限制
    if (length > MMAP_REGION_END - MMAP_REGION_START) {
        LOG_ERROR_MSG("sys_mmap: length 0x%x too large\n", length);
        return (uint32_t)-1;
    }
    
    // 检查 offset 是否页对齐
    if (offset & (PAGE_SIZE - 1)) {
        LOG_ERROR_MSG("sys_mmap: offset 0x%x not page aligned\n", offset);
        return (uint32_t)-1;
    }
    
    bool is_anonymous = (flags & MAP_ANONYMOUS) != 0;
    bool is_private = (flags & MAP_PRIVATE) != 0;
    
    LOG_DEBUG_MSG("sys_mmap: addr=0x%x, length=0x%x, prot=0x%x, flags=0x%x, fd=%d, offset=0x%x\n",
                  addr, length, prot, flags, fd, offset);
    
    // 文件映射需要有效的 fd
    fs_node_t *file_node = NULL;
    if (!is_anonymous) {
        if (fd < 0) {
            LOG_ERROR_MSG("sys_mmap: file mapping requires valid fd (got %d)\n", fd);
            return (uint32_t)-1;
        }
        
        // 获取文件节点
        fd_entry_t *entry = fd_table_get(current->fd_table, fd);
        if (!entry || !entry->node) {
            LOG_ERROR_MSG("sys_mmap: invalid fd %d\n", fd);
            return (uint32_t)-1;
        }
        
        file_node = entry->node;
        
        // 检查文件类型（只能映射普通文件）
        if (file_node->type != FS_FILE) {
            LOG_ERROR_MSG("sys_mmap: can only map regular files (type=%d)\n", file_node->type);
            return (uint32_t)-1;
        }
        
        // 检查文件权限
        // 注意：O_RDONLY = 0，所以需要用 (flags & 3) 来获取访问模式
        uint32_t access_mode = entry->flags & 3;
        bool can_read = (access_mode == O_RDONLY) || (access_mode == O_RDWR);
        bool can_write = (access_mode == O_WRONLY) || (access_mode == O_RDWR);
        
        if ((prot & PROT_READ) && !can_read) {
            LOG_ERROR_MSG("sys_mmap: file not opened for reading\n");
            return (uint32_t)-1;
        }
        
        if ((prot & PROT_WRITE) && !is_private && !can_write) {
            // 共享可写映射需要文件以写模式打开
            LOG_ERROR_MSG("sys_mmap: shared writable mapping requires write access\n");
            return (uint32_t)-1;
        }
    }
    
    // 查找空闲虚拟地址空间
    uint32_t vaddr = find_free_vaddr(current, addr, length);
    if (vaddr == 0) {
        LOG_ERROR_MSG("sys_mmap: no free virtual address space for length 0x%x\n", length);
        return (uint32_t)-1;
    }
    
    // 确定页面标志
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if (prot & PROT_WRITE) {
        page_flags |= PAGE_WRITE;
    }
    
    // 执行映射
    if (is_anonymous) {
        return do_mmap_anonymous(current, vaddr, length, page_flags);
    } else {
        return do_mmap_file(current, vaddr, length, page_flags, file_node, offset, is_private);
    }
}

/**
 * sys_munmap - 取消内存映射
 * @param addr 映射起始地址
 * @param length 取消映射的长度
 * @return 成功返回 0，失败返回 (uint32_t)-1
 */
uint32_t sys_munmap(uint32_t addr, uint32_t length) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_munmap: no current task\n");
        return (uint32_t)-1;
    }
    
    // 检查是否为用户进程
    if (!current->is_user_process) {
        LOG_ERROR_MSG("sys_munmap: not a user process\n");
        return (uint32_t)-1;
    }
    
    // 对齐地址和长度
    uint32_t aligned_addr = PAGE_ALIGN_DOWN(addr);
    length = PAGE_ALIGN_UP(length + (addr - aligned_addr));
    
    if (length == 0) {
        return 0;  // 无需操作
    }
    
    // 检查地址范围是否有效（在用户空间内）
    if (aligned_addr >= USER_SPACE_END || aligned_addr + length > USER_SPACE_END) {
        LOG_ERROR_MSG("sys_munmap: address 0x%x out of user space\n", aligned_addr);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_munmap: addr=0x%x, length=0x%x\n", aligned_addr, length);
    
    // 取消映射并释放物理页
    uint32_t pages_freed = 0;
    for (uint32_t page = aligned_addr; page < aligned_addr + length; page += PAGE_SIZE) {
        uint32_t phys = vmm_unmap_page_in_directory(current->page_dir_phys, page);
        if (phys) {
            pmm_free_frame(phys);
            pages_freed++;
        }
    }
    
    LOG_DEBUG_MSG("sys_munmap: unmapped %u pages\n", pages_freed);
    
    return 0;
}

