/**
 * @file paging.c
 * @brief i686 架构特定的分页实现
 * 
 * 实现 i686 (x86 32-bit) 的 2 级页表操作
 * 提供 HAL MMU 接口的 i686 实现
 * 
 * Requirements: 5.2, 12.1
 */

#include <types.h>
#include <mm/mm_types.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <hal/hal.h>

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
void hal_mmu_flush_tlb(vaddr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"((uint32_t)virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (i686)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal_mmu_flush_tlb_all(void) {
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
void hal_mmu_switch_space(paddr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_table_phys) : "memory");
}

/**
 * @brief 获取页错误地址 (i686)
 * @return CR2 寄存器中的错误地址
 */
vaddr_t hal_mmu_get_fault_addr(void) {
    uint32_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    return (vaddr_t)fault_addr;
}

/**
 * @brief 获取当前页目录物理地址 (i686)
 * @return CR3 寄存器的值
 */
paddr_t hal_mmu_get_current_page_table(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (paddr_t)cr3;
}

/**
 * @brief 启用分页 (i686)
 * 
 * 设置 CR0 的 PG 位启用分页
 * 注意：在调用此函数前必须先设置好 CR3
 */
void hal_mmu_enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Set PG bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

/**
 * @brief 检查分页是否启用 (i686)
 * @return true 如果分页已启用
 */
bool hal_mmu_is_paging_enabled(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) != 0;
}


/* ============================================================================
 * i686 页表格式验证
 * ========================================================================== */

/**
 * @brief 验证 i686 页表项格式
 * @param entry 页表项
 * @return true 如果格式正确
 * 
 * i686 PTE 格式:
 *   [31:12] - 物理页帧地址 (20 bits)
 *   [11:9]  - Available (3 bits, 用于 COW 等)
 *   [8]     - Global (G)
 *   [7]     - PAT (Page Attribute Table)
 *   [6]     - Dirty (D)
 *   [5]     - Accessed (A)
 *   [4]     - Cache Disable (PCD)
 *   [3]     - Write-Through (PWT)
 *   [2]     - User/Supervisor (U/S)
 *   [1]     - Read/Write (R/W)
 *   [0]     - Present (P)
 */
bool i686_validate_pte_format(uint32_t entry) {
    // 如果不存在，格式无关紧要
    if (!i686_is_present(entry)) {
        return true;
    }
    
    // 物理地址必须页对齐
    uint32_t frame = i686_get_frame(entry);
    if (frame & (PAGE_SIZE - 1)) {
        return false;
    }
    
    return true;
}

/**
 * @brief 验证 i686 页目录项格式
 * @param entry 页目录项
 * @return true 如果格式正确
 * 
 * i686 PDE 格式与 PTE 类似，但指向页表而非物理页
 */
bool i686_validate_pde_format(uint32_t entry) {
    // 如果不存在，格式无关紧要
    if (!i686_is_present(entry)) {
        return true;
    }
    
    // 页表地址必须页对齐
    uint32_t table_addr = i686_get_frame(entry);
    if (table_addr & (PAGE_SIZE - 1)) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取 i686 页表级数
 * @return 2 (i686 使用 2 级页表)
 */
uint32_t i686_get_page_table_levels(void) {
    return 2;
}

/**
 * @brief 获取 i686 页大小
 * @return 4096 (4KB)
 */
uint32_t i686_get_page_size(void) {
    return PAGE_SIZE;
}

/**
 * @brief 获取 i686 内核虚拟基址
 * @return 0x80000000 (2GB, 高半核)
 */
uintptr_t i686_get_kernel_virtual_base(void) {
    return KERNEL_VIRTUAL_BASE;
}


/* ============================================================================
 * HAL MMU 扩展接口实现 - i686
 * 
 * 实现 Requirements 4.1, 4.3, 4.4, 4.5
 * ========================================================================== */

/**
 * @brief 获取当前地址空间 (i686)
 * @return 当前页目录的物理地址
 * 
 * @see Requirements 4.5
 */
hal_addr_space_t hal_mmu_current_space(void) {
    return (hal_addr_space_t)hal_mmu_get_current_page_table();
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
        dir_phys = hal_mmu_get_current_page_table();
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
 * 
 * @see Requirements 4.1
 */
bool hal_mmu_query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags) {
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
 * @note 调用者需要在修改后调用 hal_mmu_flush_tlb()
 * 
 * @see Requirements 4.1
 */
bool hal_mmu_protect(hal_addr_space_t space, vaddr_t virt, 
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
 * @brief 克隆地址空间 (i686, COW 语义)
 * 
 * 创建地址空间的副本，用户空间页面使用 Copy-on-Write 语义：
 * - 用户页面被标记为只读 + COW
 * - 物理页面的引用计数增加
 * - 内核空间直接共享（不复制）
 * 
 * @param src 源地址空间句柄
 * @return 新地址空间句柄，失败返回 HAL_ADDR_SPACE_INVALID
 * 
 * @see Requirements 4.4
 */
hal_addr_space_t hal_mmu_clone_space(hal_addr_space_t src) {
    /* Validate source address space */
    if (src == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    /* Get source page directory */
    paddr_t src_phys = (src == HAL_ADDR_SPACE_CURRENT || src == 0) 
                       ? hal_mmu_get_current_page_table() 
                       : src;
    
    /* Use VMM's clone function which already implements COW */
    extern uintptr_t vmm_clone_page_directory(uintptr_t src_dir_phys);
    uintptr_t new_dir_phys = vmm_clone_page_directory((uintptr_t)src_phys);
    
    if (new_dir_phys == 0) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    return (hal_addr_space_t)new_dir_phys;
}

/**
 * @brief 解析页错误信息 (i686)
 * 
 * 从 CR2 寄存器和错误码中提取页错误详细信息。
 * 
 * i686 Page Fault Error Code (pushed by CPU):
 *   Bit 0 (P):    1 = 页面存在（保护违规），0 = 页面不存在
 *   Bit 1 (W/R):  1 = 写操作，0 = 读操作
 *   Bit 2 (U/S):  1 = 用户模式，0 = 内核模式
 *   Bit 3 (RSVD): 1 = 保留位被设置
 *   Bit 4 (I/D):  1 = 指令获取导致（仅当 NX 启用时）
 * 
 * @param[out] info 页错误信息结构
 * 
 * @note 此函数应在页错误处理程序中调用，错误码从栈上获取
 * 
 * @see Requirements 4.3
 */
void hal_mmu_parse_fault(hal_page_fault_info_t *info) {
    if (info == NULL) {
        return;
    }
    
    /* Get fault address from CR2 */
    info->fault_addr = hal_mmu_get_fault_addr();
    
    /* 
     * Get error code from the ISR stack frame
     * The error code is pushed by the CPU before the ISR is called.
     * We need to access it through the current task's interrupt frame.
     * 
     * For now, we'll use a simplified approach: the error code should be
     * passed to the page fault handler and stored in a global variable
     * or passed through the info structure.
     * 
     * Since we can't directly access the stack frame here, we'll provide
     * a helper function that the ISR can call with the error code.
     */
    
    /* Default values - caller should update raw_error if available */
    info->raw_error = 0;
    info->is_present = false;
    info->is_write = false;
    info->is_user = false;
    info->is_exec = false;
    info->is_reserved = false;
}

/**
 * @brief 使用错误码解析页错误信息 (i686)
 * 
 * 此函数应由页错误 ISR 调用，传入 CPU 推送的错误码。
 * 
 * @param[out] info 页错误信息结构
 * @param error_code CPU 推送的错误码
 * 
 * @see Requirements 4.3
 */
void hal_mmu_parse_fault_with_error(hal_page_fault_info_t *info, uint32_t error_code) {
    if (info == NULL) {
        return;
    }
    
    /* Get fault address from CR2 */
    info->fault_addr = hal_mmu_get_fault_addr();
    
    /* Parse error code */
    info->raw_error = error_code;
    info->is_present = (error_code & 0x01) != 0;   /* Bit 0: Present */
    info->is_write = (error_code & 0x02) != 0;     /* Bit 1: Write */
    info->is_user = (error_code & 0x04) != 0;      /* Bit 2: User */
    info->is_reserved = (error_code & 0x08) != 0;  /* Bit 3: Reserved bit set */
    info->is_exec = (error_code & 0x10) != 0;      /* Bit 4: Instruction fetch (NX) */
}

/**
 * @brief 创建新地址空间 (i686)
 * 
 * 分配并初始化新的页目录，内核空间映射从主内核页目录复制。
 * 
 * @return 新地址空间句柄，失败返回 HAL_ADDR_SPACE_INVALID
 * 
 * @see Requirements 4.2
 */
hal_addr_space_t hal_mmu_create_space(void) {
    /* Allocate a new page directory */
    paddr_t dir_phys = pmm_alloc_frame();
    if (dir_phys == PADDR_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    /* Get virtual address for access */
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT((uintptr_t)dir_phys);
    
    /* Clear the entire page directory */
    memset(new_dir, 0, sizeof(page_directory_t));
    
    /* Get the master kernel page directory (boot_page_directory) */
    extern uint32_t boot_page_directory[];
    page_directory_t *master_dir = (page_directory_t *)boot_page_directory;
    
    /* Copy kernel space mappings (512-1023, i.e., 0x80000000-0xFFFFFFFF) */
    /* This ensures the new process gets the complete kernel mappings */
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = master_dir->entries[i];
    }
    
    LOG_DEBUG_MSG("hal_mmu_create_space: Created new page directory at phys 0x%llx\n",
                  (unsigned long long)dir_phys);
    
    return (hal_addr_space_t)dir_phys;
}

/**
 * @brief 销毁地址空间 (i686)
 * 
 * 释放页目录和所有用户空间页表，递减共享物理页的引用计数。
 * 
 * @param space 要销毁的地址空间句柄
 * 
 * @warning 不能销毁当前活动的地址空间
 */
void hal_mmu_destroy_space(hal_addr_space_t space) {
    if (space == HAL_ADDR_SPACE_INVALID || space == 0) {
        return;
    }
    
    /* Don't destroy current address space */
    if (space == hal_mmu_current_space()) {
        LOG_ERROR_MSG("HAL MMU: Cannot destroy current address space\n");
        return;
    }
    
    extern void vmm_free_page_directory(uintptr_t dir_phys);
    vmm_free_page_directory((uintptr_t)space);
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
 * @note 调用者需要在映射后调用 hal_mmu_flush_tlb()
 * 
 * @see Requirements 4.1
 */
bool hal_mmu_map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
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
        paddr_t table_phys = pmm_alloc_frame();
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
 * @note 调用者需要在取消映射后调用 hal_mmu_flush_tlb()
 */
paddr_t hal_mmu_unmap(hal_addr_space_t space, vaddr_t virt) {
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

/**
 * @brief 虚拟地址转物理地址 (i686)
 * 
 * 便捷函数，查询当前地址空间中虚拟地址对应的物理地址。
 * 
 * @param virt 虚拟地址
 * @return 物理地址，未映射返回 PADDR_INVALID
 */
paddr_t hal_mmu_virt_to_phys(vaddr_t virt) {
    paddr_t phys;
    if (hal_mmu_query(HAL_ADDR_SPACE_CURRENT, virt, &phys, NULL)) {
        return phys;
    }
    return PADDR_INVALID;
}

/**
 * @brief 创建新页表 (i686, 兼容旧接口)
 * @return 页表物理地址，失败返回 PADDR_INVALID
 * @deprecated 使用 hal_mmu_create_space() 代替
 */
paddr_t hal_mmu_create_page_table(void) {
    hal_addr_space_t space = hal_mmu_create_space();
    return (space == HAL_ADDR_SPACE_INVALID) ? PADDR_INVALID : space;
}

/**
 * @brief 销毁页表 (i686, 兼容旧接口)
 * @param page_table_phys 页表物理地址
 * @deprecated 使用 hal_mmu_destroy_space() 代替
 */
void hal_mmu_destroy_page_table(paddr_t page_table_phys) {
    hal_mmu_destroy_space((hal_addr_space_t)page_table_phys);
}
