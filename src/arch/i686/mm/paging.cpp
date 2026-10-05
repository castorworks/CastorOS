/**
 * @file paging.c
 * @brief i686 架构特定的分页实现
 * 
 * 实现 i686 (x86 32-bit) 的 2 级页表操作
 * 提供 HAL MMU 接口的 i686 实现
 */

#include <types.h>
#include <mm/mm_types.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <hal/hal.h>

/* 引导页目录（定义在 boot.asm） */
extern "C" uint32_t boot_page_directory[];

/* ============================================================================
 * i686 页表结构定义
 * ============================================================================
 * 
 * i686 使用 2 级页表：
 *   - 页目录 (Page Directory): 1024 个 PDE，每个 4 字节
 *   - 页表 (Page Table): 1024 个 PTE，每个 4 字节
 * 
 * 虚拟地址分解 (32-bit):
 *   [31:22] - 页目录索引 (10 bits, 1024 entries)
 *   [21:12] - 页表索引 (10 bits, 1024 entries)
 *   [11:0]  - 页内偏移 (12 bits, 4KB page)
 * ========================================================================== */

/** @brief 获取页目录索引 */
static inline uint32_t i686_pde_index(uint32_t virt) {
    return virt >> 22;
}

/** @brief 获取页表索引 */
static inline uint32_t i686_pte_index(uint32_t virt) {
    return (virt >> 12) & 0x3FF;
}

/** @brief 从页表项中提取物理地址 */
static inline uint32_t i686_get_frame(uint32_t entry) {
    return entry & 0xFFFFF000;
}

/** @brief 检查页表项是否存在 */
static inline bool i686_is_present(uint32_t entry) {
    return (entry & PAGE_PRESENT) != 0;
}


/* ============================================================================
 * HAL MMU 接口实现 - i686
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目 (i686)
 * @param virt 虚拟地址
 */
void hal::Mmu::flush_tlb(vaddr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"((uint32_t)virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (i686)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal::Mmu::flush_tlb_all() {
    __asm__ volatile(
        "mov %%cr3, %%eax\n\t"
        "mov %%eax, %%cr3"
        ::: "eax", "memory"
    );
}

/**
 * @brief 切换地址空间 (i686)
 * @param page_table_phys 新页目录的物理地址
 */
void hal::Mmu::switch_space(paddr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_table_phys) : "memory");
}

/**
 * @brief 获取当前页目录物理地址 (i686)
 * @return CR3 寄存器的值
 */
paddr_t hal::Mmu::get_current_page_table() {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (paddr_t)cr3;
}


/* ============================================================================
 * i686 页表格式验证
 * ========================================================================== */


/* ============================================================================
 * HAL MMU 扩展接口实现 - i686
 * 
 * 实现 Requirements 4.1, 4.3, 4.4, 4.5
 * ========================================================================== */

/**
 * @brief 获取当前地址空间 (i686)
 * @return 当前页目录的物理地址
 */
hal_addr_space_t hal::Mmu::current_space() {
    return (hal_addr_space_t)hal::Mmu::get_current_page_table();
}

/**
 * @brief 将 HAL 页标志转换为 i686 页表项标志
 * @param hal_flags HAL 页标志 (HAL_PAGE_*)
 * @return i686 页表项标志
 */
static uint32_t hal_flags_to_i686(uint32_t hal_flags) {
    uint32_t i686_flags = 0;
    
    if (hal_flags & HAL_PAGE_PRESENT)   i686_flags |= PAGE_PRESENT;
    if (hal_flags & HAL_PAGE_WRITE)     i686_flags |= PAGE_WRITE;
    if (hal_flags & HAL_PAGE_USER)      i686_flags |= PAGE_USER;
    if (hal_flags & HAL_PAGE_NOCACHE)   i686_flags |= PAGE_CACHE_DISABLE;
    if (hal_flags & HAL_PAGE_COW)       i686_flags |= PAGE_COW;
    if (hal_flags & HAL_PAGE_SHARED)    i686_flags |= PAGE_SHARED;
    /* HAL_PAGE_EXEC: i686 doesn't have NX bit in standard mode */
    /* HAL_PAGE_DIRTY/ACCESSED: set by hardware, not by software */
    
    return i686_flags;
}

/**
 * @brief 将 i686 页表项标志转换为 HAL 页标志
 * @param i686_flags i686 页表项标志
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t i686_flags_to_hal(uint32_t i686_flags) {
    uint32_t hal_flags = 0;
    
    if (i686_flags & PAGE_PRESENT)       hal_flags |= HAL_PAGE_PRESENT;
    if (i686_flags & PAGE_WRITE)         hal_flags |= HAL_PAGE_WRITE;
    if (i686_flags & PAGE_USER)          hal_flags |= HAL_PAGE_USER;
    if (i686_flags & PAGE_CACHE_DISABLE) hal_flags |= HAL_PAGE_NOCACHE;
    if (i686_flags & PAGE_COW)           hal_flags |= HAL_PAGE_COW;
    if (i686_flags & PAGE_SHARED)        hal_flags |= HAL_PAGE_SHARED;
    if (i686_flags & 0x40)               hal_flags |= HAL_PAGE_DIRTY;    /* Bit 6 */
    if (i686_flags & 0x20)               hal_flags |= HAL_PAGE_ACCESSED; /* Bit 5 */
    
    return hal_flags;
}

/**
 * @brief 获取指定地址空间的页目录虚拟地址
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @return 页目录的虚拟地址
 */
static page_directory_t* get_page_directory(hal_addr_space_t space) {
    paddr_t dir_phys;
    
    if (space == HAL_ADDR_SPACE_CURRENT || space == 0) {
        dir_phys = hal::Mmu::get_current_page_table();
    } else {
        dir_phys = space;
    }
    
    return (page_directory_t*)PHYS_TO_VIRT((uintptr_t)dir_phys);
}

/**
 * @brief 查询虚拟地址映射 (i686)
 * 
 * 遍历 2 级页表结构，获取虚拟地址对应的物理地址和标志。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @param[out] phys 物理地址 (可为 NULL)
 * @param[out] flags HAL 页标志 (可为 NULL)
 * @return true 如果映射存在，false 如果未映射
 */
bool hal::Mmu::query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags) {
    page_directory_t *dir = get_page_directory(space);
    
    uint32_t pd_idx = i686_pde_index((uint32_t)virt);
    uint32_t pt_idx = i686_pte_index((uint32_t)virt);
    
    /* Check if page directory entry is present */
    pde_t pde = dir->entries[pd_idx];
    if (!i686_is_present(pde)) {
        return false;
    }
    
    /* Get page table */
    paddr_t table_phys = i686_get_frame(pde);
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
    
    /* Check if page table entry is present */
    pte_t pte = table->entries[pt_idx];
    if (!i686_is_present(pte)) {
        return false;
    }
    
    /* Extract physical address and flags */
    if (phys != NULL) {
        *phys = (paddr_t)i686_get_frame(pte);
    }
    
    if (flags != NULL) {
        *flags = i686_flags_to_hal(pte & 0xFFF);
    }
    
    return true;
}

/**
 * @brief 修改页表项标志 (i686)
 * 
 * 修改现有映射的标志，不改变物理地址。
 * 用于实现 COW（清除写标志）和权限变更。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @param set_flags 要设置的 HAL 标志
 * @param clear_flags 要清除的 HAL 标志
 * @return true 成功，false 如果映射不存在
 * 
 * @note 调用者需要在修改后调用 hal::Mmu::flush_tlb()
 */
bool hal::Mmu::protect(hal_addr_space_t space, vaddr_t virt, 
                     uint32_t set_flags, uint32_t clear_flags) {
    page_directory_t *dir = get_page_directory(space);
    
    uint32_t pd_idx = i686_pde_index((uint32_t)virt);
    uint32_t pt_idx = i686_pte_index((uint32_t)virt);
    
    /* Check if page directory entry is present */
    pde_t pde = dir->entries[pd_idx];
    if (!i686_is_present(pde)) {
        return false;
    }
    
    /* Get page table */
    paddr_t table_phys = i686_get_frame(pde);
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
    
    /* Check if page table entry is present */
    pte_t *pte = &table->entries[pt_idx];
    if (!i686_is_present(*pte)) {
        return false;
    }
    
    /* Convert HAL flags to i686 flags */
    uint32_t i686_set = hal_flags_to_i686(set_flags);
    uint32_t i686_clear = hal_flags_to_i686(clear_flags);
    
    /* Modify flags: set new flags, clear specified flags */
    uint32_t frame = i686_get_frame(*pte);
    uint32_t current_flags = *pte & 0xFFF;
    
    current_flags |= i686_set;
    current_flags &= ~i686_clear;
    
    *pte = frame | current_flags;
    
    return true;
}

/**
 * @brief 克隆地址空间（fork 用），写时复制
 *
 * 内核半区的页目录项原样共享。用户半区的页表各复制一份，页表项指向同一批物理页：
 * 可写的页在两边都改成只读并打上 COW 标记（PAGE_SHARED 的共享映射除外，保持原样），
 * 每个被共享的物理页引用计数加一。第一次写入时由缺页处理复制。
 */
hal_addr_space_t hal::Mmu::clone_space(hal_addr_space_t src) {
    if (src == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    paddr_t src_phys = (src == HAL_ADDR_SPACE_CURRENT || src == 0)
                       ? hal::Mmu::get_current_page_table()
                       : src;

    paddr_t new_phys = mm::Pmm::alloc_frame();
    if (new_phys == PADDR_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    page_directory_t *src_dir = (page_directory_t *)PHYS_TO_VIRT((uintptr_t)src_phys);
    page_directory_t *new_dir = (page_directory_t *)PHYS_TO_VIRT((uintptr_t)new_phys);
    memset(new_dir, 0, sizeof(page_directory_t));
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = src_dir->entries[i];
    }

    bool failed = false;
    for (uint32_t i = 0; i < 512 && !failed; i++) {
        if (!i686_is_present(src_dir->entries[i])) {
            continue;
        }
        paddr_t new_table_phys = mm::Pmm::alloc_frame();
        if (new_table_phys == PADDR_INVALID) {
            failed = true;
            break;
        }
        page_table_t *src_table = (page_table_t *)PHYS_TO_VIRT(i686_get_frame(src_dir->entries[i]));
        page_table_t *new_table = (page_table_t *)PHYS_TO_VIRT((uintptr_t)new_table_phys);

        for (uint32_t j = 0; j < 1024; j++) {
            uint32_t entry = src_table->entries[j];
            if (!i686_is_present(entry)) {
                new_table->entries[j] = 0;
                continue;
            }
            if ((entry & PAGE_WRITE) && !(entry & PAGE_SHARED)) {
                entry = (entry & ~(uint32_t)PAGE_WRITE) | PAGE_COW;
                src_table->entries[j] = entry;
            }
            new_table->entries[j] = entry;
            mm::Pmm::frame_ref_share(i686_get_frame(entry));
        }
        new_dir->entries[i] = (uint32_t)new_table_phys | (src_dir->entries[i] & 0xFFF);
    }

    // 源地址空间的页表项被改成了只读：它正在使用的话，旧的 TLB 项必须作废
    if (src_phys == hal::Mmu::get_current_page_table()) {
        hal::Mmu::flush_tlb_all();
    }

    if (failed) {
        // 源这边已经打上的 COW 标记不用撤销：引用计数为 1 的 COW 页在写入时直接恢复可写
        hal::Mmu::destroy_space((hal_addr_space_t)new_phys);
        return HAL_ADDR_SPACE_INVALID;
    }
    return (hal_addr_space_t)new_phys;
}


/**
 * @brief 创建新地址空间 (i686)
 * 
 * 分配并初始化新的页目录，内核空间映射从主内核页目录复制。
 * 
 * @return 新地址空间句柄，失败返回 HAL_ADDR_SPACE_INVALID
 */
hal_addr_space_t hal::Mmu::create_space() {
    /* Allocate a new page directory */
    paddr_t dir_phys = mm::Pmm::alloc_frame();
    if (dir_phys == PADDR_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    /* Get virtual address for access */
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT((uintptr_t)dir_phys);
    
    /* Clear the entire page directory */
    memset(new_dir, 0, sizeof(page_directory_t));
    
    /* Get the master kernel page directory (boot_page_directory) */
    page_directory_t *master_dir = (page_directory_t *)boot_page_directory;
    
    /* Copy kernel space mappings (512-1023, i.e., 0x80000000-0xFFFFFFFF) */
    /* This ensures the new process gets the complete kernel mappings */
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = master_dir->entries[i];
    }
    
    LOG_DEBUG_MSG("hal::Mmu::create_space: Created new page directory at phys 0x%llx\n",
                  (unsigned long long)dir_phys);
    
    return (hal_addr_space_t)dir_phys;
}

/**
 * @brief 销毁地址空间：释放用户半区的物理页、页表和页目录本身
 *
 * 物理页由引用计数管理（Pmm::free_frame 只在计数归零时真正释放），所以和别的
 * 地址空间共享的 COW 页、共享映射不会被提前释放。内核半区的页表是共享的，不动。
 */
void hal::Mmu::destroy_space(hal_addr_space_t space) {
    if (space == HAL_ADDR_SPACE_INVALID || space == 0) {
        return;
    }
    if (space == hal::Mmu::current_space() ||
        space == (hal_addr_space_t)VIRT_TO_PHYS((uintptr_t)boot_page_directory)) {
        LOG_ERROR_MSG("HAL MMU: Cannot destroy the current or the boot address space\n");
        return;
    }

    page_directory_t *dir = (page_directory_t *)PHYS_TO_VIRT((uintptr_t)space);
    for (uint32_t i = 0; i < 512; i++) {
        if (!i686_is_present(dir->entries[i])) {
            continue;
        }
        uint32_t table_phys = i686_get_frame(dir->entries[i]);
        page_table_t *table = (page_table_t *)PHYS_TO_VIRT(table_phys);
        for (uint32_t j = 0; j < 1024; j++) {
            if (!i686_is_present(table->entries[j])) {
                continue;
            }
            uint32_t frame = i686_get_frame(table->entries[j]);
            // 物理内存之上的设备内存（map_device）不归 PMM 管，没有什么可释放的
            if (frame != 0 && frame < 0x80000000) {
                mm::Pmm::free_frame(frame);
            }
        }
        mm::Pmm::free_frame(table_phys);
        dir->entries[i] = 0;
    }
    mm::Pmm::free_frame((paddr_t)space);
}


/**
 * @brief 映射虚拟页到物理页 (i686)
 * 
 * 直接操作页表结构，不回调 VMM 函数以避免循环依赖。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址 (必须页对齐)
 * @param phys 物理地址 (必须页对齐)
 * @param flags HAL 页标志
 * @return true 成功，false 失败
 * 
 * @note 调用者需要在映射后调用 hal::Mmu::flush_tlb()
 */
bool hal::Mmu::map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    /* Check page alignment */
    if (((uint32_t)virt | (uint32_t)phys) & (PAGE_SIZE - 1)) {
        return false;
    }
    
    /* Convert HAL flags to i686 flags */
    uint32_t i686_flags = hal_flags_to_i686(flags);
    
    /* Get page directory */
    page_directory_t *dir = get_page_directory(space);
    
    uint32_t pd_idx = i686_pde_index((uint32_t)virt);
    uint32_t pt_idx = i686_pte_index((uint32_t)virt);
    
    /* Check if page table exists, create if not */
    pde_t *pde = &dir->entries[pd_idx];
    page_table_t *table;
    
    if (!i686_is_present(*pde)) {
        /* Allocate new page table */
        paddr_t table_phys = mm::Pmm::alloc_frame();
        if (table_phys == PADDR_INVALID) {
            return false;
        }
        
        /* Map the new page table and clear it */
        table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
        memset(table, 0, PAGE_SIZE);
        
        /* Set PDE with appropriate flags */
        /* PDE needs PRESENT, WRITE, and USER if mapping user pages */
        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITE;
        if (i686_flags & PAGE_USER) {
            pde_flags |= PAGE_USER;
        }
        *pde = (uint32_t)table_phys | pde_flags;
    } else {
        /* Get existing page table */
        paddr_t table_phys = i686_get_frame(*pde);
        table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
        
        /* If mapping user page, ensure PDE has USER flag */
        if ((i686_flags & PAGE_USER) && !(*pde & PAGE_USER)) {
            *pde |= PAGE_USER;
        }
    }
    
    /* Set page table entry */
    table->entries[pt_idx] = (uint32_t)phys | i686_flags;
    
    return true;
}

/**
 * @brief 取消虚拟页映射 (i686)
 * 
 * 直接操作页表结构，不回调 VMM 函数以避免循环依赖。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @return 原物理地址，未映射返回 PADDR_INVALID
 * 
 * @note 调用者需要在取消映射后调用 hal::Mmu::flush_tlb()
 */
paddr_t hal::Mmu::unmap(hal_addr_space_t space, vaddr_t virt) {
    /* Check page alignment */
    if ((uint32_t)virt & (PAGE_SIZE - 1)) {
        return PADDR_INVALID;
    }
    
    /* Get page directory */
    page_directory_t *dir = get_page_directory(space);
    
    uint32_t pd_idx = i686_pde_index((uint32_t)virt);
    uint32_t pt_idx = i686_pte_index((uint32_t)virt);
    
    /* Check if page directory entry is present */
    pde_t pde = dir->entries[pd_idx];
    if (!i686_is_present(pde)) {
        return PADDR_INVALID;
    }
    
    /* Get page table */
    paddr_t table_phys = i686_get_frame(pde);
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
    
    /* Check if page table entry is present */
    pte_t *pte = &table->entries[pt_idx];
    if (!i686_is_present(*pte)) {
        return PADDR_INVALID;
    }
    
    /* Get physical address before clearing */
    paddr_t phys = (paddr_t)i686_get_frame(*pte);
    
    /* Clear the page table entry */
    *pte = 0;
    
    return phys;
}

/* ============================================================================
 * 大页映射实现 (i686)
 * 
 * i686 支持 4MB 大页（通过 PSE），但此实现使用回退方式：
 * 将 2MB 大页请求映射为 512 个 4KB 页。
 * 
 * ========================================================================== */

/** @brief 2MB 大页大小 */
#define HUGE_PAGE_SIZE_2MB      (2 * 1024 * 1024)

/** @brief 2MB 大页包含的 4KB 页数 (local definition to avoid conflict with pmm.h) */
#define HUGE_PAGE_FRAMES_I686   (HUGE_PAGE_SIZE_2MB / PAGE_SIZE)

