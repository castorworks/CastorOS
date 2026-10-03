/**
 * @file pgtable.c
 * @brief x86_64 架构的页表抽象层实现
 * 
 * 实现 HAL 页表抽象接口的 x86_64 版本。
 * 处理 4 级页表格式（PML4 -> PDPT -> PD -> PT），64 位页表项。
 * 
 * x86_64 页表项格式:
 *   [63]    - NX (No Execute)
 *   [62:52] - Available / Reserved
 *   [51:12] - 物理页帧地址 (40 bits)
 *   [11:9]  - Available (3 bits, 用于 COW 等)
 *   [8]     - Global (G)
 *   [7]     - PAT / PS (Page Size for PDE/PDPTE)
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
 * x86_64 页表项标志位定义
 * ========================================================================== */

/** @brief 页存在标志 */
#define X64_PTE_PRESENT         (1ULL << 0)

/** @brief 页可写标志 */
#define X64_PTE_WRITE           (1ULL << 1)

/** @brief 用户模式可访问标志 */
#define X64_PTE_USER            (1ULL << 2)

/** @brief Write-Through 标志 */
#define X64_PTE_WRITE_THROUGH   (1ULL << 3)

/** @brief 禁用缓存标志 */
#define X64_PTE_CACHE_DISABLE   (1ULL << 4)

/** @brief 已访问标志 */
#define X64_PTE_ACCESSED        (1ULL << 5)

/** @brief 脏页标志 */
#define X64_PTE_DIRTY           (1ULL << 6)

/** @brief PAT 标志 (用于 PTE) / PS 标志 (用于 PDE/PDPTE) */
#define X64_PTE_PAT             (1ULL << 7)
#define X64_PTE_HUGE            (1ULL << 7)

/** @brief 全局页标志 */
#define X64_PTE_GLOBAL          (1ULL << 8)

/** @brief COW 标志 (使用 Available bit 9) */
#define X64_PTE_COW             (1ULL << 9)

/** @brief 不可执行标志 */
#define X64_PTE_NX              (1ULL << 63)

/** @brief 物理地址掩码 (bits 51:12 for 4KB pages) */
#define X64_PTE_ADDR_MASK       0x000FFFFFFFFFF000ULL

/** @brief 标志位掩码 (低 12 位 + bit 63) */
#define X64_PTE_FLAGS_MASK      0x8000000000000FFFULL

/* ============================================================================
 * 页表项操作函数实现
 * ========================================================================== */

/**
 * @brief 创建页表项 (x86_64)
 * 
 * 将物理地址和架构无关的标志组合成 x86_64 格式的页表项。
 * 
 * @param phys 物理地址（必须页对齐）
 * @param flags 架构无关的页表项标志
 * @return x86_64 格式的页表项
 */
pte_t pgtable_make_entry(paddr_t phys, uint32_t flags) {
    uint64_t x64_flags = 0;
    
    /* 转换架构无关标志到 x86_64 标志 */
    if (flags & PTE_PRESENT)    x64_flags |= X64_PTE_PRESENT;
    if (flags & PTE_WRITE)      x64_flags |= X64_PTE_WRITE;
    if (flags & PTE_USER)       x64_flags |= X64_PTE_USER;
    if (flags & PTE_NOCACHE)    x64_flags |= X64_PTE_CACHE_DISABLE;
    if (flags & PTE_COW)        x64_flags |= X64_PTE_COW;
    if (flags & PTE_GLOBAL)     x64_flags |= X64_PTE_GLOBAL;
    if (flags & PTE_HUGE)       x64_flags |= X64_PTE_HUGE;
    
    /* PTE_EXEC: x86_64 使用 NX 位，不可执行时设置 NX */
    if (!(flags & PTE_EXEC))    x64_flags |= X64_PTE_NX;
    
    /* PTE_DIRTY/PTE_ACCESSED: 由硬件设置，不在创建时设置 */
    
    /* 组合物理地址和标志 */
    return (phys & X64_PTE_ADDR_MASK) | x64_flags;
}

/**
 * @brief 从页表项提取物理地址 (x86_64)
 * 
 * @param entry 页表项
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry) {
    return (paddr_t)(entry & X64_PTE_ADDR_MASK);
}

/**
 * @brief 从页表项提取标志 (x86_64)
 * 
 * @param entry 页表项
 * @return 架构无关的标志
 */
uint32_t pgtable_get_flags(pte_t entry) {
    uint32_t flags = 0;
    
    /* 转换 x86_64 标志到架构无关标志 */
    if (entry & X64_PTE_PRESENT)       flags |= PTE_PRESENT;
    if (entry & X64_PTE_WRITE)         flags |= PTE_WRITE;
    if (entry & X64_PTE_USER)          flags |= PTE_USER;
    if (entry & X64_PTE_CACHE_DISABLE) flags |= PTE_NOCACHE;
    if (entry & X64_PTE_COW)           flags |= PTE_COW;
    if (entry & X64_PTE_DIRTY)         flags |= PTE_DIRTY;
    if (entry & X64_PTE_ACCESSED)      flags |= PTE_ACCESSED;
    if (entry & X64_PTE_GLOBAL)        flags |= PTE_GLOBAL;
    if (entry & X64_PTE_HUGE)          flags |= PTE_HUGE;
    
    /* NX 位：如果没有设置 NX，则可执行 */
    if (!(entry & X64_PTE_NX))         flags |= PTE_EXEC;
    
    return flags;
}

/**
 * @brief 检查页表项是否存在 (x86_64)
 */
bool pgtable_is_present(pte_t entry) {
    return (entry & X64_PTE_PRESENT) != 0;
}

/**
 * @brief 检查页表项是否可写 (x86_64)
 */
bool pgtable_is_writable(pte_t entry) {
    return (entry & X64_PTE_WRITE) != 0;
}

/**
 * @brief 检查页表项是否用户可访问 (x86_64)
 */
bool pgtable_is_user(pte_t entry) {
    return (entry & X64_PTE_USER) != 0;
}

/**
 * @brief 检查页表项是否为 COW 页 (x86_64)
 */
bool pgtable_is_cow(pte_t entry) {
    return (entry & X64_PTE_COW) != 0;
}

/**
 * @brief 检查页表项是否为大页 (x86_64)
 */
bool pgtable_is_huge(pte_t entry) {
    return (entry & X64_PTE_HUGE) != 0;
}

/**
 * @brief 检查页表项是否可执行 (x86_64)
 */
bool pgtable_is_executable(pte_t entry) {
    /* NX 位为 0 表示可执行 */
    return (entry & X64_PTE_NX) == 0;
}

/**
 * @brief 修改页表项标志 (x86_64)
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
 * @brief 获取页表级数 (x86_64)
 * 
 * @return 4 (PML4 -> PDPT -> PD -> PT)
 */
uint32_t pgtable_get_levels(void) {
    return 4;
}

/**
 * @brief 获取每级页表的条目数 (x86_64)
 * 
 * @return 512
 */
uint32_t pgtable_get_entries_per_level(void) {
    return 512;
}

/**
 * @brief 获取页表项大小 (x86_64)
 * 
 * @return 8 字节
 */
uint32_t pgtable_get_entry_size(void) {
    return 8;
}

/**
 * @brief 检查是否支持 NX 位 (x86_64)
 * 
 * @return true
 */
bool pgtable_supports_nx(void) {
    return true;
}

/**
 * @brief 检查是否支持大页 (x86_64)
 * 
 * @return true (支持 2MB 和 1GB 大页)
 */
bool pgtable_supports_huge_pages(void) {
    return true;
}

/* ============================================================================
 * 虚拟地址索引提取函数实现
 * ========================================================================== */

/**
 * @brief 从虚拟地址提取顶级页表索引 (x86_64)
 * 
 * @param virt 虚拟地址
 * @return PML4 索引 (bits 47:39)
 */
uint32_t pgtable_get_top_index(vaddr_t virt) {
    return ((uint64_t)virt >> 39) & 0x1FF;
}

/**
 * @brief 从虚拟地址提取指定级别的页表索引 (x86_64)
 * 
 * @param virt 虚拟地址
 * @param level 页表级别 (0=PT, 1=PD, 2=PDPT, 3=PML4)
 * @return 指定级别的页表索引
 */
uint32_t pgtable_get_index(vaddr_t virt, uint32_t level) {
    switch (level) {
        case 0:  /* PT 索引 (bits 20:12) */
            return ((uint64_t)virt >> 12) & 0x1FF;
        case 1:  /* PD 索引 (bits 29:21) */
            return ((uint64_t)virt >> 21) & 0x1FF;
        case 2:  /* PDPT 索引 (bits 38:30) */
            return ((uint64_t)virt >> 30) & 0x1FF;
        case 3:  /* PML4 索引 (bits 47:39) */
            return ((uint64_t)virt >> 39) & 0x1FF;
        default:
            return 0;  /* 无效级别 */
    }
}

/* ============================================================================
 * 调试和验证函数实现
 * ========================================================================== */

/**
 * @brief 验证页表项格式 (x86_64)
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
    
    /* x86_64 物理地址最大 52 位 (4PB) */
    if (phys > 0x000FFFFFFFFFFFFFULL) {
        return false;
    }
    
    /* 检查保留位 (bits 62:52 应为 0，除非使用特殊功能) */
    /* 简化检查：只验证基本格式 */
    
    return true;
}

/**
 * @brief 获取页表项的字符串描述 (x86_64)
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
                     "phys=0x%012llx %s%s%s%s%s%s%s%s",
                     (unsigned long long)phys,
                     (flags & PTE_WRITE) ? "W" : "R",
                     (flags & PTE_USER) ? "U" : "K",
                     (flags & PTE_EXEC) ? "X" : "-",
                     (flags & PTE_NOCACHE) ? " NC" : "",
                     (flags & PTE_COW) ? " COW" : "",
                     (flags & PTE_HUGE) ? " HUGE" : "",
                     (flags & PTE_DIRTY) ? " D" : "",
                     (flags & PTE_ACCESSED) ? " A" : "");
}
