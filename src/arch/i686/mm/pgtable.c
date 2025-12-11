/**
 * @file pgtable.c
 * @brief i686 架构的页表抽象层实现
 * 
 * 实现 HAL 页表抽象接口的 i686 版本。
 * 处理 2 级页表格式（PD -> PT），32 位页表项。
 * 
 * i686 页表项格式:
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
 * 
 * @see Requirements 3.1, 3.2, 3.3
 */

#include <hal/pgtable.h>
#include <lib/kprintf.h>

/* ============================================================================
 * i686 页表项标志位定义
 * ========================================================================== */

/** @brief 页存在标志 */
#define I686_PTE_PRESENT        (1 << 0)

/** @brief 页可写标志 */
#define I686_PTE_WRITE          (1 << 1)

/** @brief 用户模式可访问标志 */
#define I686_PTE_USER           (1 << 2)

/** @brief Write-Through 标志 */
#define I686_PTE_WRITE_THROUGH  (1 << 3)

/** @brief 禁用缓存标志 */
#define I686_PTE_CACHE_DISABLE  (1 << 4)

/** @brief 已访问标志 */
#define I686_PTE_ACCESSED       (1 << 5)

/** @brief 脏页标志 */
#define I686_PTE_DIRTY          (1 << 6)

/** @brief PAT 标志 (用于页表项) / PS 标志 (用于页目录项) */
#define I686_PTE_PAT            (1 << 7)

/** @brief 全局页标志 */
#define I686_PTE_GLOBAL         (1 << 8)

/** @brief COW 标志 (使用 Available bit 9) */
#define I686_PTE_COW            (1 << 9)

/** @brief 物理地址掩码 (bits 31:12) */
#define I686_PTE_ADDR_MASK      0xFFFFF000UL

/** @brief 标志位掩码 (bits 11:0) */
#define I686_PTE_FLAGS_MASK     0x00000FFFUL

/* ============================================================================
 * 页表项操作函数实现
 * ========================================================================== */

/**
 * @brief 创建页表项 (i686)
 * 
 * 将物理地址和架构无关的标志组合成 i686 格式的页表项。
 * 
 * @param phys 物理地址（必须页对齐）
 * @param flags 架构无关的页表项标志
 * @return i686 格式的页表项
 */
pte_t pgtable_make_entry(paddr_t phys, uint32_t flags) {
    uint32_t i686_flags = 0;
    
    /* 转换架构无关标志到 i686 标志 */
    if (flags & PTE_PRESENT)    i686_flags |= I686_PTE_PRESENT;
    if (flags & PTE_WRITE)      i686_flags |= I686_PTE_WRITE;
    if (flags & PTE_USER)       i686_flags |= I686_PTE_USER;
    if (flags & PTE_NOCACHE)    i686_flags |= I686_PTE_CACHE_DISABLE;
    if (flags & PTE_COW)        i686_flags |= I686_PTE_COW;
    if (flags & PTE_GLOBAL)     i686_flags |= I686_PTE_GLOBAL;
    /* PTE_EXEC: i686 标准模式不支持 NX 位，忽略此标志 */
    /* PTE_DIRTY/PTE_ACCESSED: 由硬件设置，不在创建时设置 */
    /* PTE_HUGE: 在 PDE 中使用 PS 位，此处不处理 */
    
    /* 组合物理地址和标志 */
    pte_t entry = ((pte_t)phys & I686_PTE_ADDR_MASK) | 
                  (i686_flags & I686_PTE_FLAGS_MASK);
    
    return entry;
}

/**
 * @brief 从页表项提取物理地址 (i686)
 * 
 * @param entry 页表项
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry) {
    return (paddr_t)(entry & I686_PTE_ADDR_MASK);
}

/**
 * @brief 从页表项提取标志 (i686)
 * 
 * @param entry 页表项
 * @return 架构无关的标志
 */
uint32_t pgtable_get_flags(pte_t entry) {
    uint32_t i686_flags = entry & I686_PTE_FLAGS_MASK;
    uint32_t flags = 0;
    
    /* 转换 i686 标志到架构无关标志 */
    if (i686_flags & I686_PTE_PRESENT)       flags |= PTE_PRESENT;
    if (i686_flags & I686_PTE_WRITE)         flags |= PTE_WRITE;
    if (i686_flags & I686_PTE_USER)          flags |= PTE_USER;
    if (i686_flags & I686_PTE_CACHE_DISABLE) flags |= PTE_NOCACHE;
    if (i686_flags & I686_PTE_COW)           flags |= PTE_COW;
    if (i686_flags & I686_PTE_DIRTY)         flags |= PTE_DIRTY;
    if (i686_flags & I686_PTE_ACCESSED)      flags |= PTE_ACCESSED;
    if (i686_flags & I686_PTE_GLOBAL)        flags |= PTE_GLOBAL;
    if (i686_flags & I686_PTE_PAT)           flags |= PTE_HUGE;  /* PS bit in PDE */
    
    /* i686 标准模式不支持 NX 位，所有页都可执行 */
    flags |= PTE_EXEC;
    
    return flags;
}

/**
 * @brief 检查页表项是否存在 (i686)
 */
bool pgtable_is_present(pte_t entry) {
    return (entry & I686_PTE_PRESENT) != 0;
}

/**
 * @brief 检查页表项是否可写 (i686)
 */
bool pgtable_is_writable(pte_t entry) {
    return (entry & I686_PTE_WRITE) != 0;
}

/**
 * @brief 检查页表项是否用户可访问 (i686)
 */
bool pgtable_is_user(pte_t entry) {
    return (entry & I686_PTE_USER) != 0;
}

/**
 * @brief 检查页表项是否为 COW 页 (i686)
 */
bool pgtable_is_cow(pte_t entry) {
    return (entry & I686_PTE_COW) != 0;
}

/**
 * @brief 检查页表项是否为大页 (i686)
 * 
 * @note 在 PDE 中，bit 7 是 PS (Page Size) 位
 */
bool pgtable_is_huge(pte_t entry) {
    return (entry & I686_PTE_PAT) != 0;
}

/**
 * @brief 检查页表项是否可执行 (i686)
 * 
 * @note i686 标准模式不支持 NX 位，所有页都可执行
 */
bool pgtable_is_executable(pte_t entry) {
    (void)entry;
    return true;  /* i686 不支持 NX 位 */
}

/**
 * @brief 修改页表项标志 (i686)
 */
pte_t pgtable_modify_flags(pte_t entry, uint32_t set_flags, uint32_t clear_flags) {
    /* 提取物理地址 */
    paddr_t phys = pgtable_get_phys(entry);
    
    /* 获取当前标志 */
    uint32_t current_flags = pgtable_get_flags(entry);
    
    /* 修改标志：先清除后设置 */
    current_flags &= ~clear_flags;
    current_flags |= set_flags;
    
    /* 创建新的页表项 */
    return pgtable_make_entry(phys, current_flags);
}

/* ============================================================================
 * 页表配置查询函数实现
 * ========================================================================== */

/**
 * @brief 获取页表级数 (i686)
 * 
 * @return 2 (PD -> PT)
 */
uint32_t pgtable_get_levels(void) {
    return 2;
}

/**
 * @brief 获取每级页表的条目数 (i686)
 * 
 * @return 1024
 */
uint32_t pgtable_get_entries_per_level(void) {
    return 1024;
}

/**
 * @brief 获取页表项大小 (i686)
 * 
 * @return 4 字节
 */
uint32_t pgtable_get_entry_size(void) {
    return 4;
}

/**
 * @brief 检查是否支持 NX 位 (i686)
 * 
 * @return false (i686 标准模式不支持)
 */
bool pgtable_supports_nx(void) {
    return false;
}

/**
 * @brief 检查是否支持大页 (i686)
 * 
 * @return false (此实现不支持 4MB 大页)
 */
bool pgtable_supports_huge_pages(void) {
    return false;
}

/* ============================================================================
 * 虚拟地址索引提取函数实现
 * ========================================================================== */

/**
 * @brief 从虚拟地址提取顶级页表索引 (i686)
 * 
 * @param virt 虚拟地址
 * @return PD 索引 (bits 31:22)
 */
uint32_t pgtable_get_top_index(vaddr_t virt) {
    return ((uint32_t)virt >> 22) & 0x3FF;
}

/**
 * @brief 从虚拟地址提取指定级别的页表索引 (i686)
 * 
 * @param virt 虚拟地址
 * @param level 页表级别 (0=PT, 1=PD)
 * @return 指定级别的页表索引
 */
uint32_t pgtable_get_index(vaddr_t virt, uint32_t level) {
    switch (level) {
        case 0:  /* PT 索引 (bits 21:12) */
            return ((uint32_t)virt >> 12) & 0x3FF;
        case 1:  /* PD 索引 (bits 31:22) */
            return ((uint32_t)virt >> 22) & 0x3FF;
        default:
            return 0;  /* 无效级别 */
    }
}

/* ============================================================================
 * 调试和验证函数实现
 * ========================================================================== */

/**
 * @brief 验证页表项格式 (i686)
 */
bool pgtable_validate_entry(pte_t entry) {
    /* 如果不存在，格式无关紧要 */
    if (!pgtable_is_present(entry)) {
        return true;
    }
    
    /* 物理地址必须页对齐 */
    paddr_t phys = pgtable_get_phys(entry);
    if (phys & (PAGE_SIZE - 1)) {
        return false;
    }
    
    /* i686 物理地址最大 4GB */
    if (phys > 0xFFFFFFFFULL) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取页表项的字符串描述 (i686)
 */
int pgtable_entry_to_string(pte_t entry, char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return 0;
    }
    
    if (!pgtable_is_present(entry)) {
        return ksnprintf(buf, buf_size, "NOT PRESENT");
    }
    
    paddr_t phys = pgtable_get_phys(entry);
    uint32_t flags = pgtable_get_flags(entry);
    
    return ksnprintf(buf, buf_size, 
                     "phys=0x%08lx %s%s%s%s%s%s",
                     (unsigned long)phys,
                     (flags & PTE_WRITE) ? "W" : "R",
                     (flags & PTE_USER) ? "U" : "K",
                     (flags & PTE_NOCACHE) ? "NC" : "",
                     (flags & PTE_COW) ? " COW" : "",
                     (flags & PTE_DIRTY) ? " D" : "",
                     (flags & PTE_ACCESSED) ? " A" : "");
}
