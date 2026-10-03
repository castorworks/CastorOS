/**
 * @file paging64.c
 * @brief x86_64 架构特定的分页实现
 * 
 * 实现 x86_64 (AMD64/Intel 64-bit) 的 4 级页表操作
 * 提供 HAL MMU 接口的 x86_64 实现
 * 
 * x86_64 使用 4 级页表：
 *   - PML4 (Page Map Level 4): 512 个 PML4E，每个 8 字节
 *   - PDPT (Page Directory Pointer Table): 512 个 PDPTE，每个 8 字节
 *   - PD (Page Directory): 512 个 PDE，每个 8 字节
 *   - PT (Page Table): 512 个 PTE，每个 8 字节
 * 
 * 虚拟地址分解 (48-bit canonical):
 *   [63:48] - 符号扩展 (必须与 bit 47 相同)
 *   [47:39] - PML4 索引 (9 bits, 512 entries)
 *   [38:30] - PDPT 索引 (9 bits, 512 entries)
 *   [29:21] - PD 索引 (9 bits, 512 entries)
 *   [20:12] - PT 索引 (9 bits, 512 entries)
 *   [11:0]  - 页内偏移 (12 bits, 4KB page)
 * 
 * Requirements: 5.2, 12.1
 */

#include <types.h>
#include <hal/hal.h>
#include <lib/klog.h>
#include <lib/string.h>

/* x86_64 specific constants (from arch_types.h) */
#ifndef KERNEL_VIRTUAL_BASE_X64
#define KERNEL_VIRTUAL_BASE_X64     0xFFFF800000000000ULL
#endif

#ifndef USER_SPACE_END_X64
#define USER_SPACE_END_X64          0x00007FFFFFFFFFFFULL
#endif

#ifndef PHYS_ADDR_MAX_X64
#define PHYS_ADDR_MAX_X64           0x0000FFFFFFFFFFFFULL
#endif

/* ============================================================================
 * x86_64 页表结构定义
 * ========================================================================== */

/** 页表项类型 (64-bit) */
typedef uint64_t pte64_t;

/** 页表项标志位 */
#define PTE64_PRESENT       (1ULL << 0)   /**< 页存在 */
#define PTE64_WRITE         (1ULL << 1)   /**< 可写 */
#define PTE64_USER          (1ULL << 2)   /**< 用户可访问 */
#define PTE64_WRITE_THROUGH (1ULL << 3)   /**< 写穿透 */
#define PTE64_CACHE_DISABLE (1ULL << 4)   /**< 禁用缓存 */
#define PTE64_ACCESSED      (1ULL << 5)   /**< 已访问 */
#define PTE64_DIRTY         (1ULL << 6)   /**< 已修改 */
#define PTE64_HUGE          (1ULL << 7)   /**< 大页 (2MB/1GB) */
#define PTE64_GLOBAL        (1ULL << 8)   /**< 全局页 */
#define PTE64_COW           (1ULL << 9)   /**< COW 标志 (Available bit) */
#define PTE64_NX            (1ULL << 63)  /**< 不可执行 */

/** 物理地址掩码 (bits 12-51 for 4KB pages) */
#define PTE64_ADDR_MASK     0x000FFFFFFFFFF000ULL

/** 页表项数量 */
#define PTE64_ENTRIES       512

/* ============================================================================
 * 地址分解宏
 * ========================================================================== */

/** @brief 获取 PML4 索引 (bits 47:39) */
static inline uint64_t pml4_index(uint64_t virt) {
    return (virt >> 39) & 0x1FF;
}

/** @brief 获取 PDPT 索引 (bits 38:30) */
static inline uint64_t pdpt_index(uint64_t virt) {
    return (virt >> 30) & 0x1FF;
}

/** @brief 获取 PD 索引 (bits 29:21) */
static inline uint64_t pd_index(uint64_t virt) {
    return (virt >> 21) & 0x1FF;
}

/** @brief 获取 PT 索引 (bits 20:12) */
static inline uint64_t pt_index(uint64_t virt) {
    return (virt >> 12) & 0x1FF;
}

/** @brief 从页表项中提取物理地址 */
static inline uint64_t pte64_get_frame(pte64_t entry) {
    return entry & PTE64_ADDR_MASK;
}

/** @brief 检查页表项是否存在 */
static inline bool pte64_is_present(pte64_t entry) {
    return (entry & PTE64_PRESENT) != 0;
}

/** @brief 检查是否为大页 */
static inline bool pte64_is_huge(pte64_t entry) {
    return (entry & PTE64_HUGE) != 0;
}

/* ============================================================================
 * HAL MMU 接口实现 - x86_64
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目 (x86_64)
 * @param virt 虚拟地址
 */
void hal_mmu_flush_tlb(uintptr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (x86_64)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal_mmu_flush_tlb_all(void) {
    __asm__ volatile(
        "mov %%cr3, %%rax\n\t"
        "mov %%rax, %%cr3"
        ::: "rax", "memory"
    );
}

/**
 * @brief 切换地址空间 (x86_64)
 * @param page_table_phys 新 PML4 的物理地址
 */
void hal_mmu_switch_space(paddr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint64_t)page_table_phys) : "memory");
}

/**
 * @brief 获取页错误地址 (x86_64)
 * @return CR2 寄存器中的错误地址
 */
vaddr_t hal_mmu_get_fault_addr(void) {
    uint64_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    return (vaddr_t)fault_addr;
}

/**
 * @brief 获取当前 PML4 物理地址 (x86_64)
 * @return CR3 寄存器的值
 */
paddr_t hal_mmu_get_current_page_table(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (paddr_t)cr3;
}

/**
 * @brief 启用分页 (x86_64)
 * 
 * 在 x86_64 长模式下，分页总是启用的
 * 此函数主要用于确保 CR0.PG 位已设置
 */
void hal_mmu_enable_paging(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Set PG bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

/**
 * @brief 检查分页是否启用 (x86_64)
 * @return true 如果分页已启用
 */
bool hal_mmu_is_paging_enabled(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) != 0;
}

/* ============================================================================
 * x86_64 页表格式验证
 * ========================================================================== */

/**
 * @brief 验证 x86_64 页表项格式
 * @param entry 页表项
 * @return true 如果格式正确
 * 
 * x86_64 PTE 格式:
 *   [63]    - NX (No Execute)
 *   [62:52] - Available / Reserved
 *   [51:12] - 物理页帧地址 (40 bits)
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
bool x86_64_validate_pte_format(pte64_t entry) {
    // 如果不存在，格式无关紧要
    if (!pte64_is_present(entry)) {
        return true;
    }
    
    // 物理地址必须页对齐
    uint64_t frame = pte64_get_frame(entry);
    if (frame & (PAGE_SIZE - 1)) {
        return false;
    }
    
    // 检查物理地址是否在有效范围内 (48-bit physical addressing)
    if (frame > PHYS_ADDR_MAX_X64) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取 x86_64 页表级数
 * @return 4 (x86_64 使用 4 级页表)
 */
uint32_t x86_64_get_page_table_levels(void) {
    return 4;
}

/**
 * @brief 获取 x86_64 页大小
 * @return 4096 (4KB)
 */
uint32_t x86_64_get_page_size(void) {
    return PAGE_SIZE;
}

/**
 * @brief 获取 x86_64 内核虚拟基址
 * @return 0xFFFF800000000000 (高半核)
 */
uint64_t x86_64_get_kernel_virtual_base(void) {
    return KERNEL_VIRTUAL_BASE_X64;
}

/**
 * @brief 检查虚拟地址是否为规范地址 (canonical)
 * @param virt 虚拟地址
 * @return true 如果是规范地址
 * 
 * x86_64 使用 48 位虚拟地址，bits 63:48 必须与 bit 47 相同
 * 规范地址范围：
 *   - 低半部分: 0x0000000000000000 - 0x00007FFFFFFFFFFF
 *   - 高半部分: 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
 */
bool x86_64_is_canonical_address(uint64_t virt) {
    // 提取 bits 63:47
    uint64_t high_bits = virt >> 47;
    // 必须全为 0 或全为 1
    return (high_bits == 0) || (high_bits == 0x1FFFF);
}

/**
 * @brief 检查地址是否在内核空间
 * @param virt 虚拟地址
 * @return true 如果在内核空间
 */
bool x86_64_is_kernel_address(uint64_t virt) {
    return virt >= KERNEL_VIRTUAL_BASE_X64;
}

/**
 * @brief 检查地址是否在用户空间
 * @param virt 虚拟地址
 * @return true 如果在用户空间
 */
bool x86_64_is_user_address(uint64_t virt) {
    return virt <= USER_SPACE_END_X64;
}

/* ============================================================================
 * 页错误信息解析 (x86_64)
 * ========================================================================== */

/**
 * @brief x86_64 页错误信息结构
 */
typedef struct {
    bool present;       /**< 页是否存在 (P=1 表示保护违规) */
    bool write;         /**< 是否为写操作 */
    bool user;          /**< 是否为用户模式 */
    bool reserved;      /**< 是否为保留位错误 */
    bool instruction;   /**< 是否为指令获取 */
    bool pk;            /**< 是否为保护密钥违规 */
    bool ss;            /**< 是否为影子栈访问 */
    bool sgx;           /**< 是否为 SGX 违规 */
} x86_64_page_fault_info_t;

/**
 * @brief 解析 x86_64 页错误错误码
 * @param error_code 错误码
 * @return 解析后的页错误信息
 * 
 * x86_64 Page Fault Error Code:
 *   Bit 0 (P):    1 = 页面存在（保护违规），0 = 页面不存在
 *   Bit 1 (W/R):  1 = 写操作，0 = 读操作
 *   Bit 2 (U/S):  1 = 用户模式，0 = 内核模式
 *   Bit 3 (RSVD): 1 = 保留位被设置
 *   Bit 4 (I/D):  1 = 指令获取导致
 *   Bit 5 (PK):   1 = 保护密钥违规
 *   Bit 6 (SS):   1 = 影子栈访问
 *   Bit 15 (SGX): 1 = SGX 违规
 */
x86_64_page_fault_info_t x86_64_parse_page_fault_error(uint64_t error_code) {
    x86_64_page_fault_info_t info;
    info.present     = (error_code & (1ULL << 0)) != 0;
    info.write       = (error_code & (1ULL << 1)) != 0;
    info.user        = (error_code & (1ULL << 2)) != 0;
    info.reserved    = (error_code & (1ULL << 3)) != 0;
    info.instruction = (error_code & (1ULL << 4)) != 0;
    info.pk          = (error_code & (1ULL << 5)) != 0;
    info.ss          = (error_code & (1ULL << 6)) != 0;
    info.sgx         = (error_code & (1ULL << 15)) != 0;
    return info;
}

/**
 * @brief 检查是否为 COW 页错误
 * @param error_code 错误码
 * @return true 如果是 COW 页错误
 * 
 * COW 页错误特征：页面存在(P=1) + 写操作(W=1)
 */
bool x86_64_is_cow_fault(uint64_t error_code) {
    // P=1 (页面存在) + W=1 (写操作) = 0x3
    return (error_code & 0x3) == 0x3;
}

/**
 * @brief 获取页错误类型描述字符串
 * @param error_code 错误码
 * @return 描述字符串
 */
const char* x86_64_page_fault_type_str(uint64_t error_code) {
    x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(error_code);
    
    if (!info.present) {
        if (info.write) {
            return info.user ? "User write to non-present page" 
                             : "Kernel write to non-present page";
        } else {
            return info.user ? "User read from non-present page"
                             : "Kernel read from non-present page";
        }
    } else {
        if (info.write) {
            return info.user ? "User write protection violation"
                             : "Kernel write protection violation";
        } else if (info.instruction) {
            return info.user ? "User instruction fetch violation"
                             : "Kernel instruction fetch violation";
        } else {
            return info.user ? "User read protection violation"
                             : "Kernel read protection violation";
        }
    }
}


/* ============================================================================
 * HAL MMU 扩展接口实现 - x86_64
 * 
 * 实现 Requirements 4.1, 4.3, 5.1
 * ========================================================================== */

#include <mm/pmm.h>
#include <mm/mm_types.h>

/**
 * @brief 获取当前地址空间 (x86_64)
 * @return 当前 PML4 的物理地址
 * 
 * @see Requirements 4.5
 */
hal_addr_space_t hal_mmu_current_space(void) {
    return (hal_addr_space_t)hal_mmu_get_current_page_table();
}

/**
 * @brief 将 HAL 页标志转换为 x86_64 页表项标志
 * @param hal_flags HAL 页标志 (HAL_PAGE_*)
 * @return x86_64 页表项标志
 */
static uint64_t hal_flags_to_x64(uint32_t hal_flags) {
    uint64_t x64_flags = 0;
    
    if (hal_flags & HAL_PAGE_PRESENT)   x64_flags |= PTE64_PRESENT;
    if (hal_flags & HAL_PAGE_WRITE)     x64_flags |= PTE64_WRITE;
    if (hal_flags & HAL_PAGE_USER)      x64_flags |= PTE64_USER;
    if (hal_flags & HAL_PAGE_NOCACHE)   x64_flags |= PTE64_CACHE_DISABLE;
    if (hal_flags & HAL_PAGE_COW)       x64_flags |= PTE64_COW;
    if (!(hal_flags & HAL_PAGE_EXEC))   x64_flags |= PTE64_NX;  /* NX = not executable */
    /* HAL_PAGE_DIRTY/ACCESSED: set by hardware, not by software */
    
    return x64_flags;
}

/**
 * @brief 将 x86_64 页表项标志转换为 HAL 页标志
 * @param x64_flags x86_64 页表项标志
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t x64_flags_to_hal(uint64_t x64_flags) {
    uint32_t hal_flags = 0;
    
    if (x64_flags & PTE64_PRESENT)       hal_flags |= HAL_PAGE_PRESENT;
    if (x64_flags & PTE64_WRITE)         hal_flags |= HAL_PAGE_WRITE;
    if (x64_flags & PTE64_USER)          hal_flags |= HAL_PAGE_USER;
    if (x64_flags & PTE64_CACHE_DISABLE) hal_flags |= HAL_PAGE_NOCACHE;
    if (x64_flags & PTE64_COW)           hal_flags |= HAL_PAGE_COW;
    if (x64_flags & PTE64_DIRTY)         hal_flags |= HAL_PAGE_DIRTY;
    if (x64_flags & PTE64_ACCESSED)      hal_flags |= HAL_PAGE_ACCESSED;
    if (!(x64_flags & PTE64_NX))         hal_flags |= HAL_PAGE_EXEC;
    
    return hal_flags;
}

/**
 * @brief 获取指定地址空间的 PML4 虚拟地址
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @return PML4 的虚拟地址
 */
static pte64_t* get_pml4(hal_addr_space_t space) {
    paddr_t pml4_phys;
    
    if (space == HAL_ADDR_SPACE_CURRENT || space == 0) {
        pml4_phys = hal_mmu_get_current_page_table();
    } else {
        pml4_phys = space;
    }
    
    return (pte64_t*)PADDR_TO_KVADDR(pml4_phys);
}

/**
 * @brief 分配并清零一个页表页
 * @return 物理地址，失败返回 PADDR_INVALID
 */
static paddr_t alloc_page_table(void) {
    paddr_t frame = pmm_alloc_frame();
    if (frame == PADDR_INVALID) {
        return PADDR_INVALID;
    }
    
    /* Clear the page table */
    void *virt = (void*)PADDR_TO_KVADDR(frame);
    memset(virt, 0, PAGE_SIZE);
    
    return frame;
}

/**
 * @brief 查询虚拟地址映射 (x86_64)
 * 
 * 遍历 4 级页表结构 (PML4 -> PDPT -> PD -> PT)，
 * 获取虚拟地址对应的物理地址和标志。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @param[out] phys 物理地址 (可为 NULL)
 * @param[out] flags HAL 页标志 (可为 NULL)
 * @return true 如果映射存在，false 如果未映射
 * 
 * @see Requirements 4.1, 5.1
 */
bool hal_mmu_query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags) {
    /* Validate canonical address */
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        return false;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    uint64_t pt_idx = pt_index((uint64_t)virt);
    
    /* Level 4: PML4 */
    pte64_t pml4e = pml4[pml4_idx];
    if (!pte64_is_present(pml4e)) {
        return false;
    }
    
    /* Level 3: PDPT */
    pte64_t *pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4e));
    pte64_t pdpte = pdpt[pdpt_idx];
    if (!pte64_is_present(pdpte)) {
        return false;
    }
    
    /* Check for 1GB huge page */
    if (pte64_is_huge(pdpte)) {
        if (phys != NULL) {
            /* 1GB page: bits 29:0 are offset */
            *phys = pte64_get_frame(pdpte) | ((uint64_t)virt & 0x3FFFFFFFULL);
        }
        if (flags != NULL) {
            *flags = x64_flags_to_hal(pdpte);
        }
        return true;
    }
    
    /* Level 2: PD */
    pte64_t *pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpte));
    pte64_t pde = pd[pd_idx];
    if (!pte64_is_present(pde)) {
        return false;
    }
    
    /* Check for 2MB huge page */
    if (pte64_is_huge(pde)) {
        if (phys != NULL) {
            /* 2MB page: bits 20:0 are offset */
            *phys = pte64_get_frame(pde) | ((uint64_t)virt & 0x1FFFFFULL);
        }
        if (flags != NULL) {
            *flags = x64_flags_to_hal(pde);
        }
        return true;
    }
    
    /* Level 1: PT */
    pte64_t *pt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pde));
    pte64_t pte = pt[pt_idx];
    if (!pte64_is_present(pte)) {
        return false;
    }
    
    /* Extract physical address and flags */
    if (phys != NULL) {
        *phys = pte64_get_frame(pte);
    }
    
    if (flags != NULL) {
        *flags = x64_flags_to_hal(pte);
    }
    
    return true;
}

/**
 * @brief 映射虚拟页到物理页 (x86_64)
 * 
 * 在 4 级页表中创建映射，自动分配中间页表级别。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址 (必须页对齐)
 * @param phys 物理地址 (必须页对齐)
 * @param flags HAL 页标志
 * @return true 成功，false 失败
 * 
 * @note 调用者需要在映射后调用 hal_mmu_flush_tlb()
 * 
 * @see Requirements 4.1, 5.1
 */
bool hal_mmu_map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    /* Validate addresses */
    if (!IS_VADDR_ALIGNED(virt) || !IS_PADDR_ALIGNED(phys)) {
        LOG_ERROR_MSG("hal_mmu_map: addresses not page-aligned\n");
        return false;
    }
    
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        LOG_ERROR_MSG("hal_mmu_map: non-canonical address\n");
        return false;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    uint64_t pt_idx = pt_index((uint64_t)virt);
    
    /* Convert HAL flags to x86_64 flags */
    uint64_t x64_flags = hal_flags_to_x64(flags);
    
    /* Intermediate table flags: present, writable, user (if user page) */
    uint64_t table_flags = PTE64_PRESENT | PTE64_WRITE;
    if (flags & HAL_PAGE_USER) {
        table_flags |= PTE64_USER;
    }
    
    /* Level 4: PML4 -> PDPT */
    pte64_t *pdpt;
    if (!pte64_is_present(pml4[pml4_idx])) {
        paddr_t pdpt_phys = alloc_page_table();
        if (pdpt_phys == PADDR_INVALID) {
            return false;
        }
        pml4[pml4_idx] = pdpt_phys | table_flags;
    } else if (flags & HAL_PAGE_USER) {
        /* Existing entry: ensure USER flag is set for user mappings */
        pml4[pml4_idx] |= PTE64_USER;
    }
    pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4[pml4_idx]));
    
    /* Level 3: PDPT -> PD */
    pte64_t *pd;
    if (!pte64_is_present(pdpt[pdpt_idx])) {
        paddr_t pd_phys = alloc_page_table();
        if (pd_phys == PADDR_INVALID) {
            return false;
        }
        pdpt[pdpt_idx] = pd_phys | table_flags;
    } else if (pte64_is_huge(pdpt[pdpt_idx])) {
        /* Cannot map 4KB page over 1GB huge page */
        LOG_ERROR_MSG("hal_mmu_map: cannot map over 1GB huge page\n");
        return false;
    } else if (flags & HAL_PAGE_USER) {
        /* Existing entry: ensure USER flag is set for user mappings */
        pdpt[pdpt_idx] |= PTE64_USER;
    }
    pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpt[pdpt_idx]));
    
    /* Level 2: PD -> PT */
    pte64_t *pt;
    if (!pte64_is_present(pd[pd_idx])) {
        paddr_t pt_phys = alloc_page_table();
        if (pt_phys == PADDR_INVALID) {
            return false;
        }
        pd[pd_idx] = pt_phys | table_flags;
    } else if (pte64_is_huge(pd[pd_idx])) {
        /* Cannot map 4KB page over 2MB huge page */
        LOG_ERROR_MSG("hal_mmu_map: cannot map over 2MB huge page\n");
        return false;
    } else if (flags & HAL_PAGE_USER) {
        /* Existing entry: ensure USER flag is set for user mappings */
        pd[pd_idx] |= PTE64_USER;
    }
    pt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pd[pd_idx]));
    
    /* Level 1: PT entry */
    pt[pt_idx] = phys | x64_flags;
    
    return true;
}

/**
 * @brief 取消虚拟页映射 (x86_64)
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @return 原物理地址，未映射返回 PADDR_INVALID
 * 
 * @note 调用者需要在取消映射后调用 hal_mmu_flush_tlb()
 * @note 此函数不释放中间页表级别
 * 
 * @see Requirements 4.1, 5.1
 */
paddr_t hal_mmu_unmap(hal_addr_space_t space, vaddr_t virt) {
    /* Validate canonical address */
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        return PADDR_INVALID;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    uint64_t pt_idx = pt_index((uint64_t)virt);
    
    /* Level 4: PML4 */
    pte64_t pml4e = pml4[pml4_idx];
    if (!pte64_is_present(pml4e)) {
        return PADDR_INVALID;
    }
    
    /* Level 3: PDPT */
    pte64_t *pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4e));
    pte64_t pdpte = pdpt[pdpt_idx];
    if (!pte64_is_present(pdpte)) {
        return PADDR_INVALID;
    }
    
    /* Cannot unmap 1GB huge page with this function */
    if (pte64_is_huge(pdpte)) {
        LOG_ERROR_MSG("hal_mmu_unmap: cannot unmap 1GB huge page\n");
        return PADDR_INVALID;
    }
    
    /* Level 2: PD */
    pte64_t *pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpte));
    pte64_t pde = pd[pd_idx];
    if (!pte64_is_present(pde)) {
        return PADDR_INVALID;
    }
    
    /* Cannot unmap 2MB huge page with this function */
    if (pte64_is_huge(pde)) {
        LOG_ERROR_MSG("hal_mmu_unmap: cannot unmap 2MB huge page\n");
        return PADDR_INVALID;
    }
    
    /* Level 1: PT */
    pte64_t *pt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pde));
    pte64_t pte = pt[pt_idx];
    if (!pte64_is_present(pte)) {
        return PADDR_INVALID;
    }
    
    /* Get physical address before clearing */
    paddr_t phys = pte64_get_frame(pte);
    
    /* Clear the entry */
    pt[pt_idx] = 0;
    
    return phys;
}

/**
 * @brief 修改页表项标志 (x86_64)
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
 * @see Requirements 4.1, 5.1
 */
bool hal_mmu_protect(hal_addr_space_t space, vaddr_t virt, 
                     uint32_t set_flags, uint32_t clear_flags) {
    /* Validate canonical address */
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        return false;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    uint64_t pt_idx = pt_index((uint64_t)virt);
    
    /* Level 4: PML4 */
    pte64_t pml4e = pml4[pml4_idx];
    if (!pte64_is_present(pml4e)) {
        return false;
    }
    
    /* Level 3: PDPT */
    pte64_t *pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4e));
    pte64_t pdpte = pdpt[pdpt_idx];
    if (!pte64_is_present(pdpte)) {
        return false;
    }
    
    /* Handle 1GB huge page */
    if (pte64_is_huge(pdpte)) {
        uint64_t x64_set = hal_flags_to_x64(set_flags);
        uint64_t x64_clear = hal_flags_to_x64(clear_flags);
        
        paddr_t frame = pte64_get_frame(pdpte);
        uint64_t current_flags = pdpte & ~PTE64_ADDR_MASK;
        
        current_flags |= x64_set;
        current_flags &= ~x64_clear;
        
        pdpt[pdpt_idx] = frame | current_flags;
        return true;
    }
    
    /* Level 2: PD */
    pte64_t *pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpte));
    pte64_t pde = pd[pd_idx];
    if (!pte64_is_present(pde)) {
        return false;
    }
    
    /* Handle 2MB huge page */
    if (pte64_is_huge(pde)) {
        uint64_t x64_set = hal_flags_to_x64(set_flags);
        uint64_t x64_clear = hal_flags_to_x64(clear_flags);
        
        paddr_t frame = pte64_get_frame(pde);
        uint64_t current_flags = pde & ~PTE64_ADDR_MASK;
        
        current_flags |= x64_set;
        current_flags &= ~x64_clear;
        
        pd[pd_idx] = frame | current_flags;
        return true;
    }
    
    /* Level 1: PT */
    pte64_t *pt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pde));
    pte64_t *pte = &pt[pt_idx];
    if (!pte64_is_present(*pte)) {
        return false;
    }
    
    /* Convert HAL flags to x86_64 flags */
    uint64_t x64_set = hal_flags_to_x64(set_flags);
    uint64_t x64_clear = hal_flags_to_x64(clear_flags);
    
    /* Modify flags: set new flags, clear specified flags */
    paddr_t frame = pte64_get_frame(*pte);
    uint64_t current_flags = *pte & ~PTE64_ADDR_MASK;
    
    current_flags |= x64_set;
    current_flags &= ~x64_clear;
    
    *pte = frame | current_flags;
    
    return true;
}

/**
 * @brief 解析页错误信息 (x86_64)
 * 
 * 从 CR2 寄存器和错误码中提取页错误详细信息。
 * 
 * @param[out] info 页错误信息结构
 * 
 * @see Requirements 4.3
 */
void hal_mmu_parse_fault(hal_page_fault_info_t *info) {
    if (info == NULL) {
        return;
    }
    
    /* Get fault address from CR2 */
    info->fault_addr = hal_mmu_get_fault_addr();
    
    /* Default values - caller should update raw_error if available */
    info->raw_error = 0;
    info->is_present = false;
    info->is_write = false;
    info->is_user = false;
    info->is_exec = false;
    info->is_reserved = false;
}

/**
 * @brief 使用错误码解析页错误信息 (x86_64)
 * 
 * 此函数应由页错误 ISR 调用，传入 CPU 推送的错误码。
 * 
 * @param[out] info 页错误信息结构
 * @param error_code CPU 推送的错误码
 * 
 * @see Requirements 4.3
 */
void hal_mmu_parse_fault_with_error(hal_page_fault_info_t *info, uint64_t error_code) {
    if (info == NULL) {
        return;
    }
    
    /* Get fault address from CR2 */
    info->fault_addr = hal_mmu_get_fault_addr();
    
    /* Parse error code */
    info->raw_error = (uint32_t)error_code;
    info->is_present = (error_code & 0x01) != 0;   /* Bit 0: Present */
    info->is_write = (error_code & 0x02) != 0;     /* Bit 1: Write */
    info->is_user = (error_code & 0x04) != 0;      /* Bit 2: User */
    info->is_reserved = (error_code & 0x08) != 0;  /* Bit 3: Reserved bit set */
    info->is_exec = (error_code & 0x10) != 0;      /* Bit 4: Instruction fetch (NX) */
}

/**
 * @brief 虚拟地址转物理地址 (x86_64)
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

/* ============================================================================
 * 大页映射实现 (2MB Huge Pages) - x86_64
 * 
 * x86_64 支持 2MB 大页（通过 PD 级别的 PS 位）和 1GB 大页（通过 PDPT 级别的 PS 位）
 * 此实现仅支持 2MB 大页
 * 
 * @see Requirements 8.1, 8.2
 * ========================================================================== */

/** @brief 2MB 大页大小 */
#define HUGE_PAGE_SIZE_2MB      (2 * 1024 * 1024)

/** @brief 2MB 大页物理地址掩码 (bits 21-51) */
#define PTE64_HUGE_ADDR_MASK    0x000FFFFFFFE00000ULL

/**
 * @brief 检查是否支持大页 (x86_64)
 * @return true (x86_64 支持 2MB 大页)
 */
bool hal_mmu_huge_pages_supported(void) {
    return true;
}

/**
 * @brief 检查地址是否 2MB 对齐
 */
static inline bool is_huge_page_aligned(uint64_t addr) {
    return (addr & (HUGE_PAGE_SIZE_2MB - 1)) == 0;
}

/**
 * @brief 映射 2MB 大页 (x86_64)
 * 
 * 在 PD 级别创建 2MB 大页映射（设置 PS 位）
 * 
 * @param space 地址空间句柄
 * @param virt 虚拟地址（必须 2MB 对齐）
 * @param phys 物理地址（必须 2MB 对齐）
 * @param flags HAL 页标志
 * @return true 成功，false 失败
 * 
 * @see Requirements 8.2
 */
bool hal_mmu_map_huge(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    /* Validate 2MB alignment */
    if (!is_huge_page_aligned((uint64_t)virt) || !is_huge_page_aligned((uint64_t)phys)) {
        LOG_ERROR_MSG("hal_mmu_map_huge: addresses not 2MB-aligned (virt=0x%llx, phys=0x%llx)\n",
                      (unsigned long long)virt, (unsigned long long)phys);
        return false;
    }
    
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        LOG_ERROR_MSG("hal_mmu_map_huge: non-canonical address 0x%llx\n",
                      (unsigned long long)virt);
        return false;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    
    /* Convert HAL flags to x86_64 flags */
    uint64_t x64_flags = hal_flags_to_x64(flags);
    
    /* Add HUGE flag for 2MB page */
    x64_flags |= PTE64_HUGE;
    
    /* Intermediate table flags: present, writable, user (if user page) */
    uint64_t table_flags = PTE64_PRESENT | PTE64_WRITE;
    if (flags & HAL_PAGE_USER) {
        table_flags |= PTE64_USER;
    }
    
    /* Level 4: PML4 -> PDPT */
    pte64_t *pdpt;
    if (!pte64_is_present(pml4[pml4_idx])) {
        paddr_t pdpt_phys = alloc_page_table();
        if (pdpt_phys == PADDR_INVALID) {
            return false;
        }
        pml4[pml4_idx] = pdpt_phys | table_flags;
    }
    pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4[pml4_idx]));
    
    /* Level 3: PDPT -> PD */
    pte64_t *pd;
    if (!pte64_is_present(pdpt[pdpt_idx])) {
        paddr_t pd_phys = alloc_page_table();
        if (pd_phys == PADDR_INVALID) {
            return false;
        }
        pdpt[pdpt_idx] = pd_phys | table_flags;
    } else if (pte64_is_huge(pdpt[pdpt_idx])) {
        /* Cannot map 2MB page over 1GB huge page */
        LOG_ERROR_MSG("hal_mmu_map_huge: cannot map over 1GB huge page\n");
        return false;
    }
    pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpt[pdpt_idx]));
    
    /* Level 2: PD entry (2MB huge page) */
    /* Check if there's already a PT at this location */
    if (pte64_is_present(pd[pd_idx]) && !pte64_is_huge(pd[pd_idx])) {
        LOG_ERROR_MSG("hal_mmu_map_huge: cannot map 2MB page over existing PT\n");
        return false;
    }
    
    /* Create 2MB huge page entry */
    pd[pd_idx] = (phys & PTE64_HUGE_ADDR_MASK) | x64_flags;
    
    LOG_DEBUG_MSG("hal_mmu_map_huge: Mapped 2MB page virt=0x%llx -> phys=0x%llx\n",
                  (unsigned long long)virt, (unsigned long long)phys);
    
    return true;
}

/**
 * @brief 取消 2MB 大页映射 (x86_64)
 * 
 * @param space 地址空间句柄
 * @param virt 虚拟地址（必须 2MB 对齐）
 * @return 原物理地址，未映射返回 PADDR_INVALID
 * 
 * @see Requirements 8.2
 */
paddr_t hal_mmu_unmap_huge(hal_addr_space_t space, vaddr_t virt) {
    /* Validate 2MB alignment */
    if (!is_huge_page_aligned((uint64_t)virt)) {
        LOG_ERROR_MSG("hal_mmu_unmap_huge: address not 2MB-aligned (virt=0x%llx)\n",
                      (unsigned long long)virt);
        return PADDR_INVALID;
    }
    
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        return PADDR_INVALID;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    
    /* Level 4: PML4 */
    pte64_t pml4e = pml4[pml4_idx];
    if (!pte64_is_present(pml4e)) {
        return PADDR_INVALID;
    }
    
    /* Level 3: PDPT */
    pte64_t *pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4e));
    pte64_t pdpte = pdpt[pdpt_idx];
    if (!pte64_is_present(pdpte)) {
        return PADDR_INVALID;
    }
    
    /* Cannot unmap if this is a 1GB huge page */
    if (pte64_is_huge(pdpte)) {
        LOG_ERROR_MSG("hal_mmu_unmap_huge: cannot unmap 1GB huge page with this function\n");
        return PADDR_INVALID;
    }
    
    /* Level 2: PD */
    pte64_t *pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpte));
    pte64_t pde = pd[pd_idx];
    if (!pte64_is_present(pde)) {
        return PADDR_INVALID;
    }
    
    /* Verify this is a 2MB huge page */
    if (!pte64_is_huge(pde)) {
        LOG_ERROR_MSG("hal_mmu_unmap_huge: entry is not a 2MB huge page\n");
        return PADDR_INVALID;
    }
    
    /* Get physical address before clearing */
    paddr_t phys = pde & PTE64_HUGE_ADDR_MASK;
    
    /* Clear the entry */
    pd[pd_idx] = 0;
    
    LOG_DEBUG_MSG("hal_mmu_unmap_huge: Unmapped 2MB page virt=0x%llx (was phys=0x%llx)\n",
                  (unsigned long long)virt, (unsigned long long)phys);
    
    return phys;
}

/**
 * @brief 查询映射是否为大页 (x86_64)
 * 
 * @param space 地址空间句柄
 * @param virt 虚拟地址
 * @return true 如果是 2MB 大页映射
 * 
 * @see Requirements 8.3
 */
bool hal_mmu_is_huge_page(hal_addr_space_t space, vaddr_t virt) {
    if (!x86_64_is_canonical_address((uint64_t)virt)) {
        return false;
    }
    
    pte64_t *pml4 = get_pml4(space);
    
    /* Get indices for each level */
    uint64_t pml4_idx = pml4_index((uint64_t)virt);
    uint64_t pdpt_idx = pdpt_index((uint64_t)virt);
    uint64_t pd_idx = pd_index((uint64_t)virt);
    
    /* Level 4: PML4 */
    pte64_t pml4e = pml4[pml4_idx];
    if (!pte64_is_present(pml4e)) {
        return false;
    }
    
    /* Level 3: PDPT */
    pte64_t *pdpt = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pml4e));
    pte64_t pdpte = pdpt[pdpt_idx];
    if (!pte64_is_present(pdpte)) {
        return false;
    }
    
    /* Check for 1GB huge page */
    if (pte64_is_huge(pdpte)) {
        return true;  /* 1GB huge page */
    }
    
    /* Level 2: PD */
    pte64_t *pd = (pte64_t*)PADDR_TO_KVADDR(pte64_get_frame(pdpte));
    pte64_t pde = pd[pd_idx];
    if (!pte64_is_present(pde)) {
        return false;
    }
    
    /* Check for 2MB huge page */
    return pte64_is_huge(pde);
}


/* ============================================================================
 * x86_64 地址空间管理实现
 * 
 * 实现 Requirements 5.2, 5.3, 5.5
 * ========================================================================== */

/** @brief 内核空间 PML4 索引起始 (256 = 0xFFFF800000000000) */
#define KERNEL_PML4_START   256

/** @brief 内核空间 PML4 索引结束 (512) */
#define KERNEL_PML4_END     512

/** @brief 用户空间 PML4 索引起始 (0) */
#define USER_PML4_START     0

/** @brief 用户空间 PML4 索引结束 (256) */
#define USER_PML4_END       256

/**
 * @brief 创建新地址空间 (x86_64)
 * 
 * 分配并初始化新的 PML4，内核空间映射从当前 PML4 复制。
 * 
 * x86_64 地址空间布局：
 *   - PML4[0..255]: 用户空间 (0x0000000000000000 - 0x00007FFFFFFFFFFF)
 *   - PML4[256..511]: 内核空间 (0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF)
 * 
 * @return 新地址空间句柄 (PML4 物理地址)，失败返回 HAL_ADDR_SPACE_INVALID
 * 
 * @see Requirements 5.2
 */
hal_addr_space_t hal_mmu_create_space(void) {
    /* Allocate a new PML4 */
    paddr_t pml4_phys = alloc_page_table();
    if (pml4_phys == PADDR_INVALID) {
        LOG_ERROR_MSG("hal_mmu_create_space: Failed to allocate PML4\n");
        return HAL_ADDR_SPACE_INVALID;
    }
    
    pte64_t *new_pml4 = (pte64_t*)PADDR_TO_KVADDR(pml4_phys);
    
    /* Get current PML4 for copying kernel mappings */
    pte64_t *current_pml4 = get_pml4(HAL_ADDR_SPACE_CURRENT);
    
    /* Clear user space entries (PML4[0..255]) */
    for (uint32_t i = USER_PML4_START; i < USER_PML4_END; i++) {
        new_pml4[i] = 0;
    }
    
    /* Copy kernel space entries (PML4[256..511]) */
    /* These are shared across all address spaces */
    for (uint32_t i = KERNEL_PML4_START; i < KERNEL_PML4_END; i++) {
        new_pml4[i] = current_pml4[i];
    }
    
    LOG_DEBUG_MSG("hal_mmu_create_space: Created new PML4 at phys 0x%llx\n", 
                  (unsigned long long)pml4_phys);
    
    return (hal_addr_space_t)pml4_phys;
}

/**
 * @brief 递归释放页表结构 (x86_64)
 * 
 * 释放指定级别的页表及其所有子页表。
 * 对于叶子页表项（物理页），递减引用计数。
 * 
 * @param table_phys 页表物理地址
 * @param level 页表级别 (3=PDPT, 2=PD, 1=PT)
 * @param is_user 是否为用户空间页表
 */
static void free_page_table_recursive(paddr_t table_phys, int level, bool is_user) {
    if (table_phys == PADDR_INVALID || table_phys == 0) {
        return;
    }
    
    pte64_t *table = (pte64_t*)PADDR_TO_KVADDR(table_phys);
    
    for (uint32_t i = 0; i < PTE64_ENTRIES; i++) {
        pte64_t entry = table[i];
        
        if (!pte64_is_present(entry)) {
            continue;
        }
        
        paddr_t frame = pte64_get_frame(entry);
        
        if (level == 1) {
            /* Level 1 (PT): entries point to physical pages */
            /* Decrement reference count for shared pages (COW) */
            uint32_t refcount = pmm_frame_get_refcount(frame);
            if (refcount > 0) {
                pmm_frame_ref_dec(frame);
                /* If refcount becomes 0, the frame is freed by pmm_frame_ref_dec */
                if (refcount == 1) {
                    /* This was the last reference, frame is now free */
                    LOG_DEBUG_MSG("free_page_table_recursive: Freed physical page 0x%llx\n",
                                  (unsigned long long)frame);
                }
            }
        } else if (!pte64_is_huge(entry)) {
            /* Not a huge page, recurse into child table */
            free_page_table_recursive(frame, level - 1, is_user);
        } else {
            /* Huge page (2MB or 1GB) - decrement refcount */
            uint32_t refcount = pmm_frame_get_refcount(frame);
            if (refcount > 0) {
                pmm_frame_ref_dec(frame);
            }
        }
    }
    
    /* Free this page table itself */
    pmm_free_frame(table_phys);
}

/**
 * @brief 销毁地址空间 (x86_64)
 * 
 * 释放 PML4 和所有用户空间页表，递减共享物理页的引用计数。
 * 内核空间页表是共享的，不释放。
 * 
 * @param space 要销毁的地址空间句柄
 * 
 * @warning 不能销毁当前活动的地址空间
 * 
 * @see Requirements 5.5
 */
void hal_mmu_destroy_space(hal_addr_space_t space) {
    if (space == HAL_ADDR_SPACE_INVALID || space == 0) {
        return;
    }
    
    /* Don't destroy current address space */
    hal_addr_space_t current = hal_mmu_current_space();
    if (space == current) {
        LOG_ERROR_MSG("hal_mmu_destroy_space: Cannot destroy current address space\n");
        return;
    }
    
    pte64_t *pml4 = (pte64_t*)PADDR_TO_KVADDR(space);
    
    LOG_DEBUG_MSG("hal_mmu_destroy_space: Destroying address space at phys 0x%llx\n",
                  (unsigned long long)space);
    
    /* Free user space page tables (PML4[0..255]) */
    for (uint32_t i = USER_PML4_START; i < USER_PML4_END; i++) {
        pte64_t pml4e = pml4[i];
        
        if (!pte64_is_present(pml4e)) {
            continue;
        }
        
        paddr_t pdpt_phys = pte64_get_frame(pml4e);
        
        /* Recursively free PDPT and its children */
        /* Level 3 = PDPT, Level 2 = PD, Level 1 = PT */
        free_page_table_recursive(pdpt_phys, 3, true);
    }
    
    /* Free the PML4 itself */
    pmm_free_frame(space);
    
    LOG_DEBUG_MSG("hal_mmu_destroy_space: Address space destroyed\n");
}

/**
 * @brief 递归克隆页表结构 (x86_64, COW 语义)
 * 
 * 克隆指定级别的页表，对于叶子页表项：
 * - 可写页面被标记为只读 + COW
 * - 物理页的引用计数增加
 * 
 * @param src_table_phys 源页表物理地址
 * @param level 页表级别 (3=PDPT, 2=PD, 1=PT)
 * @param[out] dst_table_phys 目标页表物理地址
 * @return true 成功，false 失败
 */
static bool clone_page_table_recursive(paddr_t src_table_phys, int level, 
                                        paddr_t *dst_table_phys) {
    if (src_table_phys == PADDR_INVALID || src_table_phys == 0) {
        *dst_table_phys = 0;
        return true;
    }
    
    /* Allocate new page table */
    paddr_t new_table_phys = alloc_page_table();
    if (new_table_phys == PADDR_INVALID) {
        return false;
    }
    
    pte64_t *src_table = (pte64_t*)PADDR_TO_KVADDR(src_table_phys);
    pte64_t *dst_table = (pte64_t*)PADDR_TO_KVADDR(new_table_phys);
    
    for (uint32_t i = 0; i < PTE64_ENTRIES; i++) {
        pte64_t entry = src_table[i];
        
        if (!pte64_is_present(entry)) {
            dst_table[i] = 0;
            continue;
        }
        
        paddr_t frame = pte64_get_frame(entry);
        uint64_t flags = entry & ~PTE64_ADDR_MASK;
        
        if (level == 1) {
            /* Level 1 (PT): entries point to physical pages */
            /* Apply COW semantics: mark writable pages as read-only + COW */
            if (flags & PTE64_WRITE) {
                flags &= ~PTE64_WRITE;  /* Remove write permission */
                flags |= PTE64_COW;     /* Mark as COW */
                
                /* Update source entry as well (both parent and child are COW) */
                src_table[i] = frame | flags;
            }
            
            /* Increment reference count for shared physical page */
            pmm_frame_ref_inc(frame);
            
            /* Copy entry to destination */
            dst_table[i] = frame | flags;
            
        } else if (pte64_is_huge(entry)) {
            /* Huge page (2MB or 1GB) - apply COW semantics */
            if (flags & PTE64_WRITE) {
                flags &= ~PTE64_WRITE;
                flags |= PTE64_COW;
                src_table[i] = frame | flags;
            }
            
            pmm_frame_ref_inc(frame);
            dst_table[i] = frame | flags;
            
        } else {
            /* Not a leaf entry, recurse into child table */
            paddr_t child_dst_phys;
            if (!clone_page_table_recursive(frame, level - 1, &child_dst_phys)) {
                /* Clone failed, need to clean up */
                /* Free already cloned entries */
                for (uint32_t j = 0; j < i; j++) {
                    if (pte64_is_present(dst_table[j])) {
                        paddr_t child_phys = pte64_get_frame(dst_table[j]);
                        if (level > 2 || !pte64_is_huge(dst_table[j])) {
                            free_page_table_recursive(child_phys, level - 1, true);
                        } else {
                            pmm_frame_ref_dec(child_phys);
                        }
                    }
                }
                pmm_free_frame(new_table_phys);
                return false;
            }
            
            /* Copy flags from source, point to new child table */
            dst_table[i] = child_dst_phys | (flags & 0xFFF);
        }
    }
    
    *dst_table_phys = new_table_phys;
    return true;
}

/**
 * @brief 克隆地址空间 (x86_64, COW 语义)
 * 
 * 创建地址空间的副本，用户空间页面使用 Copy-on-Write 语义：
 * - 用户页面被标记为只读 + COW
 * - 物理页面的引用计数增加
 * - 内核空间直接共享（不复制）
 * 
 * @param src 源地址空间句柄
 * @return 新地址空间句柄，失败返回 HAL_ADDR_SPACE_INVALID
 * 
 * @see Requirements 5.3
 */
hal_addr_space_t hal_mmu_clone_space(hal_addr_space_t src) {
    /* Validate source address space */
    if (src == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    /* Get source PML4 */
    paddr_t src_phys = (src == HAL_ADDR_SPACE_CURRENT || src == 0) 
                       ? hal_mmu_get_current_page_table() 
                       : src;
    
    /* Allocate new PML4 */
    paddr_t new_pml4_phys = alloc_page_table();
    if (new_pml4_phys == PADDR_INVALID) {
        LOG_ERROR_MSG("hal_mmu_clone_space: Failed to allocate PML4\n");
        return HAL_ADDR_SPACE_INVALID;
    }
    
    pte64_t *src_pml4 = (pte64_t*)PADDR_TO_KVADDR(src_phys);
    pte64_t *new_pml4 = (pte64_t*)PADDR_TO_KVADDR(new_pml4_phys);
    
    LOG_DEBUG_MSG("hal_mmu_clone_space: Cloning address space from 0x%llx to 0x%llx\n",
                  (unsigned long long)src_phys, (unsigned long long)new_pml4_phys);
    
    /* Clone user space (PML4[0..255]) with COW semantics */
    for (uint32_t i = USER_PML4_START; i < USER_PML4_END; i++) {
        pte64_t pml4e = src_pml4[i];
        
        if (!pte64_is_present(pml4e)) {
            new_pml4[i] = 0;
            continue;
        }
        
        paddr_t src_pdpt_phys = pte64_get_frame(pml4e);
        uint64_t pml4e_flags = pml4e & 0xFFF;
        
        /* Recursively clone PDPT and its children */
        paddr_t new_pdpt_phys;
        if (!clone_page_table_recursive(src_pdpt_phys, 3, &new_pdpt_phys)) {
            LOG_ERROR_MSG("hal_mmu_clone_space: Failed to clone PDPT at index %u\n", i);
            
            /* Clean up already cloned entries */
            for (uint32_t j = USER_PML4_START; j < i; j++) {
                if (pte64_is_present(new_pml4[j])) {
                    paddr_t pdpt_phys = pte64_get_frame(new_pml4[j]);
                    free_page_table_recursive(pdpt_phys, 3, true);
                }
            }
            pmm_free_frame(new_pml4_phys);
            return HAL_ADDR_SPACE_INVALID;
        }
        
        new_pml4[i] = new_pdpt_phys | pml4e_flags;
    }
    
    /* Copy kernel space entries (PML4[256..511]) - shared, not cloned */
    for (uint32_t i = KERNEL_PML4_START; i < KERNEL_PML4_END; i++) {
        new_pml4[i] = src_pml4[i];
    }
    
    /* Flush TLB for source address space (we modified COW flags) */
    if (src_phys == hal_mmu_get_current_page_table()) {
        hal_mmu_flush_tlb_all();
    }
    
    LOG_DEBUG_MSG("hal_mmu_clone_space: Clone complete\n");
    
    return (hal_addr_space_t)new_pml4_phys;
}

