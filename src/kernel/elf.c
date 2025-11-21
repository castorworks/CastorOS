// ============================================================================
// elf.c - ELF 可执行文件加载器
// ============================================================================

#include <kernel/elf.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/string.h>

/**
 * 验证 ELF 文件头
 */
bool elf_validate_header(const void *elf_data) {
    if (!elf_data) return false;
    
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    
    // 检查魔数
    if (ehdr->e_ident[0] != 0x7F || 
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        LOG_ERROR_MSG("ELF: Invalid magic number\n");
        return false;
    }
    
    // 检查类型（32位）
    if (ehdr->e_ident[4] != ELF_CLASS_32) {
        LOG_ERROR_MSG("ELF: Not 32-bit\n");
        return false;
    }
    
    // 检查字节序（小端）
    if (ehdr->e_ident[5] != ELF_DATA_LSB) {
        LOG_ERROR_MSG("ELF: Not little-endian\n");
        return false;
    }
    
    // 检查版本
    if (ehdr->e_ident[6] != EV_CURRENT) {
        LOG_ERROR_MSG("ELF: Invalid version\n");
        return false;
    }
    
    // 检查文件类型（可执行文件）
    if (ehdr->e_type != ET_EXEC) {
        LOG_ERROR_MSG("ELF: Not an executable file (type=%d)\n", ehdr->e_type);
        return false;
    }
    
    // 检查机器类型（i386）
    if (ehdr->e_machine != EM_386) {
        LOG_ERROR_MSG("ELF: Not i386 architecture\n");
        return false;
    }
    
    return true;
}

/**
 * 获取 ELF 入口点地址
 */
uint32_t elf_get_entry(const void *elf_data) {
    if (!elf_validate_header(elf_data)) {
        return 0;
    }
    
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    return ehdr->e_entry;
}

/**
 * 加载 ELF 文件到指定页目录
 */
bool elf_load(const void *elf_data, uint32_t size, page_directory_t *page_dir, uint32_t *entry_point) {
    if (!elf_data || !page_dir || !entry_point) {
        LOG_ERROR_MSG("ELF: Invalid parameters\n");
        return false;
    }
    
    // 验证 ELF 头
    if (!elf_validate_header(elf_data)) {
        return false;
    }
    
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)((uint8_t *)elf_data + ehdr->e_phoff);
    
    LOG_INFO_MSG("ELF: Loading executable\n");
    LOG_INFO_MSG("  Entry point: %x\n", ehdr->e_entry);
    LOG_INFO_MSG("  Program headers: %u\n", ehdr->e_phnum);
    
    uint32_t page_dir_phys = VIRT_TO_PHYS((uint32_t)page_dir);
    
    // 记录加载进度的变量，用于出错时清理
    int32_t cleanup_last_segment_idx = -1;
    uint32_t cleanup_last_vaddr = 0;
    
    // 遍历程序头表
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdr[i];
        
        // 只处理 LOAD 类型的段
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        
        LOG_DEBUG_MSG("  Segment %u:\n", i);
        LOG_DEBUG_MSG("    VAddr: %x\n", ph->p_vaddr);
        LOG_DEBUG_MSG("    FileSize: %u bytes\n", ph->p_filesz);
        LOG_DEBUG_MSG("    MemSize: %u bytes\n", ph->p_memsz);
        LOG_DEBUG_MSG("    Flags: %c%c%c\n",
                     (ph->p_flags & PF_R) ? 'R' : '-',
                     (ph->p_flags & PF_W) ? 'W' : '-',
                     (ph->p_flags & PF_X) ? 'X' : '-');
        
        // 检查段是否在文件范围内
        if (ph->p_offset + ph->p_filesz > size) {
            LOG_ERROR_MSG("ELF: Segment exceeds file size\n");
            // 清理已分配的页
            cleanup_last_segment_idx = i; 
            cleanup_last_vaddr = 0; // 这个段还没有分配任何页
            goto cleanup;
        }
        
        // 检查虚拟地址是否在用户空间（< 2GB）
        if (ph->p_vaddr >= KERNEL_VIRTUAL_BASE) {
            LOG_ERROR_MSG("ELF: Segment in kernel space (vaddr=%x >= %x)\n",
                         ph->p_vaddr, KERNEL_VIRTUAL_BASE);
            // 清理已分配的页
            cleanup_last_segment_idx = i;
            cleanup_last_vaddr = 0;
            goto cleanup;
        }
        
        // 计算页标志
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) {
            flags |= PAGE_WRITE;
        }
        
        // 计算需要的页数
        uint32_t vaddr_start = PAGE_ALIGN_DOWN(ph->p_vaddr);
        uint32_t vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
        uint32_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;
        
        LOG_DEBUG_MSG("    Pages: %u (%x - %x)\n", 
                     num_pages, vaddr_start, vaddr_end);
        
        // 更新进度：当前开始处理这个 Segment
        cleanup_last_segment_idx = i;
        cleanup_last_vaddr = vaddr_start;
        
        // 分配并映射物理页
        for (uint32_t page = 0; page < num_pages; page++) {
            uint32_t vaddr = vaddr_start + page * PAGE_SIZE;
            
            // 分配物理页
            uint32_t phys = pmm_alloc_frame();
            if (!phys) {
                LOG_ERROR_MSG("ELF: Failed to allocate physical page at %x\n", vaddr);
                // 清理已分配的页 (cleanup_last_vaddr 指向当前分配失败的地址)
                cleanup_last_vaddr = vaddr;
                goto cleanup;
            }
            
            // 清零物理页
            uint8_t *phys_ptr = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(phys_ptr, 0, PAGE_SIZE);
            
            // 映射到进程地址空间
            if (!vmm_map_page_in_directory(page_dir_phys, vaddr, phys, flags)) {
                LOG_ERROR_MSG("ELF: Failed to map page at %x\n", vaddr);
                pmm_free_frame(phys);
                // 清理已分配的页
                cleanup_last_vaddr = vaddr;
                goto cleanup;
            }
            
            // 更新清理进度：当前页已成功分配
            cleanup_last_vaddr = vaddr + PAGE_SIZE;
            
            // 计算本页要复制的数据
            uint32_t page_offset = (vaddr >= ph->p_vaddr) ? 0 : (ph->p_vaddr - vaddr);
            uint32_t seg_offset = (vaddr >= ph->p_vaddr) ? (vaddr - ph->p_vaddr) : 0;
            
            // 计算要复制的字节数
            uint32_t copy_size = 0;
            if (seg_offset < ph->p_filesz) {
                copy_size = ph->p_filesz - seg_offset;
                if (copy_size > PAGE_SIZE - page_offset) {
                    copy_size = PAGE_SIZE - page_offset;
                }
                
                // 从 ELF 文件复制数据
                const uint8_t *src = (const uint8_t *)elf_data + ph->p_offset + seg_offset;
                memcpy(phys_ptr + page_offset, src, copy_size);
            }
        }
    }
    
    // 返回入口点
    *entry_point = ehdr->e_entry;
    
    LOG_INFO_MSG("ELF: Load complete, entry at %x\n", *entry_point);
    return true;

cleanup:
    // 清理已分配的页：根据进度反向清理
    LOG_DEBUG_MSG("ELF: Load failed, cleaning up segments up to index %d\n", cleanup_last_segment_idx);
    
    // 遍历到出错的 segment
    for (int32_t i = 0; i <= cleanup_last_segment_idx; i++) {
        const elf32_phdr_t *ph = &phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        
        uint32_t vaddr_start = PAGE_ALIGN_DOWN(ph->p_vaddr);
        uint32_t vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
        
        // 确定清理的结束地址
        uint32_t limit = vaddr_end;
        // 如果是最后一个（出错的）segment，只清理到出错前的位置
        if (i == cleanup_last_segment_idx) {
            limit = cleanup_last_vaddr;
            // 如果 limit <= vaddr_start，说明这个段还没开始分配页或者在第一页就挂了
            if (limit <= vaddr_start) continue; 
        }
        
        for (uint32_t vaddr = vaddr_start; vaddr < limit; vaddr += PAGE_SIZE) {
            uint32_t phys = vmm_unmap_page_in_directory(page_dir_phys, vaddr);
            if (phys) {
                pmm_free_frame(phys);
            }
        }
    }
    return false;
}

