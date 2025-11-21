/**
 * @file vmm.c
 * @brief 虚拟内存管理器实现
 * 
 * 实现x86分页机制，管理虚拟地址到物理地址的映射
 */

#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <kernel/sync/spinlock.h>

static page_directory_t *current_dir = NULL;  ///< 当前页目录虚拟地址
static uint32_t current_dir_phys = 0;          ///< 当前页目录物理地址
extern uint32_t boot_page_directory[];         ///< 引导时的页目录
static spinlock_t vmm_lock;                    ///< VMM 自旋锁，保护页表操作

/**
 * @brief 获取页目录索引
 * @param v 虚拟地址
 * @return 页目录索引（高10位）
 */
static inline uint32_t pde_idx(uint32_t v) { return v >> 22; }

/**
 * @brief 获取页表索引
 * @param v 虚拟地址
 * @return 页表索引（中间10位）
 */
static inline uint32_t pte_idx(uint32_t v) { return (v >> 12) & 0x3FF; }

/**
 * @brief 从页表项/页目录项中提取物理地址
 * @param e 页表项或页目录项
 * @return 物理地址（页对齐）
 */
static inline uint32_t get_frame(uint32_t e) { return e & 0xFFFFF000; }

/**
 * @brief 检查页表项/页目录项是否存在
 * @param e 页表项或页目录项
 * @return 存在返回 true，否则返回 false
 */
static inline bool is_present(uint32_t e) { return e & PAGE_PRESENT; }

/**
 * @brief 创建新的页表
 * @return 成功返回页表虚拟地址，失败返回 NULL
 */
static page_table_t* create_page_table(void) {
    uint32_t frame = pmm_alloc_frame();
    if (!frame) return NULL;
    return (page_table_t*)PHYS_TO_VIRT(frame);
}

/**
 * @brief 初始化虚拟内存管理器
 * 
 * 使用引导时创建的页目录，设置CR3寄存器
 * 扩展高半核映射以覆盖所有可用的物理内存
 */
void vmm_init(void) {
    // 初始化 VMM 自旋锁
    spinlock_init(&vmm_lock);
    
    current_dir = (page_directory_t*)boot_page_directory;
    current_dir_phys = VIRT_TO_PHYS((uint32_t)current_dir);
    
    // 检查并更新CR3寄存器
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 != current_dir_phys)
        __asm__ volatile("mov %0, %%cr3" : : "r"(current_dir_phys));
    
    // 扩展高半核映射以覆盖所有可用的物理内存
    // 引导时已经映射了前8MB（页目录项512-513）
    // 现在需要扩展到所有可用内存（最多2GB）
    pmm_info_t pmm_info = pmm_get_info();
    uint32_t max_phys = pmm_info.total_frames * PAGE_SIZE;
    
    // 限制在2GB以内（高半核虚拟地址空间限制）
    if (max_phys > 0x80000000) {
        max_phys = 0x80000000;
    }
    
    // 计算需要映射的页目录项数量（每个页目录项映射4MB）
    // 引导时已经映射了前8MB（索引512-513），从索引514开始
    uint32_t start_pde = 514;  // 对应虚拟地址 0x80800000
    // 修复：使用向上取整，确保覆盖所有物理内存。如果 max_phys 不是 4MB 对齐，
    // 向下取整会导致末尾的内存无法被映射。
    uint32_t end_pde = 512 + ((max_phys + 0x3FFFFF) >> 22); 
    
    LOG_DEBUG_MSG("VMM: Extending high-half kernel mapping\n");
    LOG_DEBUG_MSG("  Physical memory: %u MB\n", max_phys / (1024*1024));
    LOG_DEBUG_MSG("  Mapping PDEs: %u-%u (virtual: 0x%x-0x%x)\n",
                 start_pde, end_pde - 1,
                 start_pde << 22, (end_pde << 22) - 1);
    
    // 为每个页目录项创建页表并映射
    for (uint32_t pde = start_pde; pde < end_pde; pde++) {
        // 检查页目录项是否已存在
        if (is_present(current_dir->entries[pde])) {
            continue;  // 已经映射，跳过
        }
        
        // 分配页表
        uint32_t table_phys = pmm_alloc_frame();
        if (!table_phys) {
            LOG_WARN_MSG("VMM: Failed to allocate page table for PDE %u\n", pde);
            break;  // 分配失败，停止扩展
        }
        
        page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
        memset(table, 0, sizeof(page_table_t));
        
        // 计算这个页表对应的物理地址范围
        // PDE索引pde对应虚拟地址 pde * 4MB
        // 对应的物理地址也是 pde * 4MB（高半核恒等映射）
        uint32_t phys_base = (pde - 512) << 22;  // 物理地址基址
        
        // 填充页表项：每个页表项映射一个4KB页
        for (uint32_t pte = 0; pte < 1024; pte++) {
            uint32_t phys_addr = phys_base + (pte << 12);
            
            // 只映射在可用物理内存范围内的页
            if (phys_addr < max_phys) {
                // 设置页表项：Present | Read/Write | Supervisor
                table->entries[pte] = phys_addr | PAGE_PRESENT | PAGE_WRITE;
            }
        }
        
        // 设置页目录项：指向页表
        current_dir->entries[pde] = table_phys | PAGE_PRESENT | PAGE_WRITE;
    }
    
    // 刷新TLB以确保新映射生效
    vmm_flush_tlb(0);
    
    LOG_DEBUG_MSG("VMM: High-half kernel mapping extended\n");
}


// 声明引导页目录（作为内核主页目录参考）
extern uint32_t boot_page_directory[];

/**
 * @brief 处理内核空间缺页异常（同步内核页目录）
 * @param addr 缺页地址
 * @return 是否成功处理
 */
bool vmm_handle_kernel_page_fault(uint32_t addr) {
    // 必须是内核空间地址
    if (addr < KERNEL_VIRTUAL_BASE) return false;
    
    uint32_t pd_idx = addr >> 22;
    page_directory_t *k_dir = (page_directory_t *)boot_page_directory;
    
    // 检查主内核页目录中是否存在该映射
    // 注意：我们检查 PDE 是否存在 (Present 位)
    if (k_dir->entries[pd_idx] & PAGE_PRESENT) {
        // 将条目复制到当前页目录
        current_dir->entries[pd_idx] = k_dir->entries[pd_idx];
        
        // 刷新 TLB，确保 CPU 看到新的映射
        // 虽然 Intel 手册说修改 PDE 后需要刷新 TLB，但有些实现可能缓存了 PDE
        // 对于缺页处理，invlpg 通常足够，但这里我们修改了 PDE，安全起见可以刷新整个 TLB
        // 不过针对特定地址的 invlpg 应该也足以让 CPU 重新遍历页表结构
        vmm_flush_tlb(addr);
        
        return true;
    }
    
    return false;
}

/**
 * @brief 映射虚拟页到物理页
 * @param virt 虚拟地址（页对齐）
 * @param phys 物理地址（页对齐）
 * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
 * @return 成功返回 true，失败返回 false
 */
bool vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    uint32_t pd = pde_idx(virt);  // 页目录索引
    uint32_t pt = pte_idx(virt);  // 页表索引
    
    pde_t *pde = &current_dir->entries[pd];
    page_table_t *table;
    
    // 如果页表不存在，创建新页表
    if (!is_present(*pde)) {
        table = create_page_table();
        if (!table) {
            spinlock_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        
        // 创建页表时，权限应该根据目标虚拟地址决定
        // 如果是内核空间（>= 0x80000000），则不应设置 PAGE_USER
        // 否则（用户空间），通常需要 PAGE_USER 允许用户进程访问其内容
        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITE;
        if (virt < KERNEL_VIRTUAL_BASE) {
            pde_flags |= PAGE_USER;
        }
        
        uint32_t new_pde = VIRT_TO_PHYS((uint32_t)table) | pde_flags;
        *pde = new_pde;
        
        // 关键修复：如果是内核空间的新页表，必须同步到主内核页目录 (boot_page_directory)
        // 这样其他进程可以通过 page fault handler 同步这个新映射
        if (virt >= KERNEL_VIRTUAL_BASE) {
            page_directory_t *k_dir = (page_directory_t *)boot_page_directory;
            k_dir->entries[pd] = new_pde;
        }
    } else {
        table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    }
    
    // 设置页表项
    table->entries[pt] = phys | flags;
    vmm_flush_tlb(virt);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return true;
}

/**
 * @brief 取消虚拟页映射
 * @param virt 虚拟地址（页对齐）
 */
void vmm_unmap_page(uint32_t virt) {
    // 检查页对齐
    if (virt & (PAGE_SIZE-1)) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    pde_t *pde = &current_dir->entries[pde_idx(virt)];
    // 如果页表不存在，直接返回
    if (!is_present(*pde)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return;
    }
    
    // 清除页表项
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    table->entries[pte_idx(virt)] = 0;
    vmm_flush_tlb(virt);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 刷新TLB缓存
 * @param virt 虚拟地址（0表示刷新全部TLB）
 * 
 * 当修改页表后需要刷新TLB以确保CPU使用最新的页表项
 */
void vmm_flush_tlb(uint32_t virt) {
    if (virt == 0) {
        // 刷新整个TLB：重新加载CR3
        __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
    } else {
        // 刷新单个页：使用invlpg指令
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

/**
 * @brief 获取当前页目录的物理地址
 * @return 页目录的物理地址
 */
uint32_t vmm_get_page_directory(void) {
    return current_dir_phys;
}

/**
 * @brief 创建新的页目录（用于新进程）
 * @return 成功返回页目录的物理地址，失败返回 0
 */
uint32_t vmm_create_page_directory(void) {
    // 分配页目录的物理页
    uint32_t dir_phys = pmm_alloc_frame();
    if (!dir_phys) return 0;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 获取虚拟地址访问
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 清空整个页目录
    memset(new_dir, 0, sizeof(page_directory_t));
    
    // 复制内核空间映射（512-1023，即 0x80000000-0xFFFFFFFF）
    // 关键修改：从主内核页目录 (boot_page_directory) 复制，而不是从当前页目录复制
    // 这确保新进程总是获得最完整、最新的内核映射
    page_directory_t *master_dir = (page_directory_t *)boot_page_directory;
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = master_dir->entries[i];
    }
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return dir_phys;
}

/**
 * @brief 克隆页目录（用于 fork）
 * @param src_dir_phys 源页目录的物理地址
 * @return 成功返回新页目录的物理地址，失败返回 0
 */
uint32_t vmm_clone_page_directory(uint32_t src_dir_phys) {
    // 分配新页目录
    uint32_t new_dir_phys = pmm_alloc_frame();
    if (!new_dir_phys) return 0;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *src_dir = (page_directory_t*)PHYS_TO_VIRT(src_dir_phys);
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(new_dir_phys);
    
    // 清空新页目录
    memset(new_dir, 0, sizeof(page_directory_t));
    
    // 复制内核空间映射（共享）
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = src_dir->entries[i];
    }
    
    // 复制用户空间映射（深拷贝：复制物理页）
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(src_dir->entries[i])) {
            // 分配新页表
            uint32_t new_table_phys = pmm_alloc_frame();
            if (!new_table_phys) {
                // 分配失败，清理已分配的资源
                spinlock_unlock_irqrestore(&vmm_lock, irq_state);
                vmm_free_page_directory(new_dir_phys);
                return 0;
            }
            
            page_table_t *src_table = (page_table_t*)PHYS_TO_VIRT(get_frame(src_dir->entries[i]));
            page_table_t *new_table = (page_table_t*)PHYS_TO_VIRT(new_table_phys);
            
            // 清空新页表
            memset(new_table, 0, sizeof(page_table_t));
            
            // 逐页复制（深拷贝物理页，而不是共享）
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(src_table->entries[j])) {
                    // 分配新的物理页
                    uint32_t new_frame = pmm_alloc_frame();
                    if (!new_frame) {
                        // 分配失败，清理当前页表中已分配的页
                        for (uint32_t k = 0; k < j; k++) {
                            if (is_present(new_table->entries[k])) {
                                pmm_free_frame(get_frame(new_table->entries[k]));
                            }
                        }
                        // 释放页表本身
                        pmm_free_frame(new_table_phys);
                        
                        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
                        vmm_free_page_directory(new_dir_phys);
                        return 0;
                    }
                    
                    // 复制页面内容
                    uint32_t src_frame = get_frame(src_table->entries[j]);
                    void *src_virt = (void*)PHYS_TO_VIRT(src_frame);
                    void *dst_virt = (void*)PHYS_TO_VIRT(new_frame);
                    memcpy(dst_virt, src_virt, PAGE_SIZE);
                    
                    // 设置新页表项（保留权限标志）
                    new_table->entries[j] = new_frame | (src_table->entries[j] & 0xFFF);
                }
            }
            
            // 设置新页目录项
            new_dir->entries[i] = new_table_phys | (src_dir->entries[i] & 0xFFF);
        }
    }
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return new_dir_phys;
}

/**
 * @brief 释放页目录及其用户空间页表和物理页
 * @param dir_phys 页目录的物理地址
 * 
 * 注意：由于 fork 现在使用深拷贝，所以需要释放所有物理页
 */
void vmm_free_page_directory(uint32_t dir_phys) {
    if (!dir_phys) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    uint32_t freed_pages = 0;
    uint32_t freed_tables = 0;
    
    // 只释放用户空间页表（0-511）
    // 内核空间页表（512-1023）是共享的，不释放
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(dir->entries[i])) {
            uint32_t table_phys = get_frame(dir->entries[i]);
            page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
            
            // 释放页表中的所有物理页
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(table->entries[j])) {
                    uint32_t frame = get_frame(table->entries[j]);
                    pmm_free_frame(frame);
                    freed_pages++;
                }
            }
            
            // 释放页表本身
            pmm_free_frame(table_phys);
            freed_tables++;
        }
    }
    
    LOG_DEBUG_MSG("vmm_free_page_directory: freed %u pages, %u tables, 1 directory\n", 
                  freed_pages, freed_tables);
    
    // 释放页目录本身
    pmm_free_frame(dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 切换到指定的页目录
 * @param dir_phys 页目录的物理地址
 */
void vmm_switch_page_directory(uint32_t dir_phys) {
    if (!dir_phys) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 加载到 CR3 寄存器
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir_phys) : "memory");
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 在指定页目录中映射页面
 * @param dir_phys 页目录的物理地址
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 * @return 成功返回 true，失败返回 false
 */
bool vmm_map_page_in_directory(uint32_t dir_phys, uint32_t virt, 
                                uint32_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    uint32_t pd = pde_idx(virt);
    uint32_t pt = pte_idx(virt);
    
    pde_t *pde = &dir->entries[pd];
    page_table_t *table;
    
    // 如果页表不存在，创建新页表
    if (!is_present(*pde)) {
        uint32_t table_phys = pmm_alloc_frame();
        if (!table_phys) {
            spinlock_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        
        table = (page_table_t*)PHYS_TO_VIRT(table_phys);
        memset(table, 0, sizeof(page_table_t));
        
        // 权限逻辑同上：如果是内核空间地址，不加 PAGE_USER
        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITE;
        if (virt < KERNEL_VIRTUAL_BASE) {
            pde_flags |= PAGE_USER;
        }

        *pde = table_phys | pde_flags;
    } else {
        table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    }
    
    // 设置页表项
    table->entries[pt] = phys | flags;
    
    // 如果是当前页目录，刷新 TLB
    if (dir_phys == current_dir_phys) {
        vmm_flush_tlb(virt);
    }
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return true;
}

uint32_t vmm_unmap_page_in_directory(uint32_t dir_phys, uint32_t virt) {
    if (virt & (PAGE_SIZE - 1)) {
        return 0;
    }

    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);

    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    uint32_t pd = pde_idx(virt);
    uint32_t pt = pte_idx(virt);

    pde_t *pde = &dir->entries[pd];
    if (!is_present(*pde)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }

    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    uint32_t old_entry = table->entries[pt];
    if (!is_present(old_entry)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }

    table->entries[pt] = 0;

    // 检查页表是否为空（所有条目都为0）
    bool empty = true;
    for (uint32_t i = 0; i < 1024; i++) {
        if (is_present(table->entries[i])) {
            empty = false;
            break;
        }
    }

    if (empty) {
        uint32_t table_frame = get_frame(*pde);
        pmm_free_frame(table_frame);
        *pde = 0;
        LOG_DEBUG_MSG("  Page table at PDE %u (frame=%x) is empty, freed\n", pd, table_frame);
    } else {
        LOG_DEBUG_MSG("  Page table at PDE %u still has entries\n", pd);
    }

    if (dir_phys == current_dir_phys) {
        vmm_flush_tlb(virt);
    }

    uint32_t result = get_frame(old_entry);
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}
