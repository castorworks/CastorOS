/**
 * @file pgtable.c
 * @brief ARM64 架构的页表抽象层实现
 * 
 * 实现 HAL 页表抽象接口的 ARM64 版本。
 * 处理 4 级页表格式（L0 -> L1 -> L2 -> L3），64 位描述符。
 * 
 * ARM64 页描述符格式 (4KB granule):
 *   [63:59] - Reserved / Software defined
 *   [58:55] - Reserved
 *   [54]    - UXN (User Execute Never)
 *   [53]    - PXN (Privileged Execute Never)
 *   [52]    - Contiguous hint
 *   [51:48] - Reserved
 *   [47:12] - 物理地址 (36 bits for 4KB pages)
 *   [11]    - nG (Not Global)
 *   [10]    - AF (Access Flag)
 *   [9:8]   - SH (Shareability)
 *   [7:6]   - AP (Access Permissions)
 *   [5]     - NS (Non-Secure)
 *   [4:2]   - AttrIndx (MAIR index)
 *   [1]     - Table/Block (1=Table for L0-L2, 1=Page for L3)
 *   [0]     - Valid
 * 
 * @see Requirements 3.1, 3.2, 3.3
 */

#include <hal/pgtable.h>
#include <lib/string.h>

/* ============================================================================
 * ARM64 描述符标志位定义
 * ========================================================================== */

/** @brief 有效位 */
#define ARM64_DESC_VALID        (1ULL << 0)

/** @brief 表/块选择 (1=表描述符) */
#define ARM64_DESC_TABLE        (1ULL << 1)

/** @brief 页描述符类型 (L3 级别) */
#define ARM64_DESC_PAGE         (1ULL << 1)

/** @brief MAIR 索引位移 */
#define ARM64_DESC_ATTR_SHIFT   2

/** @brief MAIR 索引掩码 */
#define ARM64_DESC_ATTR_MASK    (7ULL << 2)

/** @brief Non-Secure 位 */
#define ARM64_DESC_NS           (1ULL << 5)

/** @brief AP (Access Permissions) 位移 */
#define ARM64_DESC_AP_SHIFT     6

/** @brief AP 掩码 */
#define ARM64_DESC_AP_MASK      (3ULL << 6)

/** @brief AP 值定义 */
#define ARM64_AP_RW_EL1         (0ULL << 6)     /**< EL1 读写, EL0 无访问 */
#define ARM64_AP_RW_ALL         (1ULL << 6)     /**< EL1/EL0 读写 */
#define ARM64_AP_RO_EL1         (2ULL << 6)     /**< EL1 只读, EL0 无访问 */
#define ARM64_AP_RO_ALL         (3ULL << 6)     /**< EL1/EL0 只读 */

/** @brief SH (Shareability) 位移 */
#define ARM64_DESC_SH_SHIFT     8

/** @brief SH 掩码 */
#define ARM64_DESC_SH_MASK      (3ULL << 8)

/** @brief SH 值定义 */
#define ARM64_SH_NON            (0ULL << 8)     /**< Non-shareable */
#define ARM64_SH_OUTER          (2ULL << 8)     /**< Outer Shareable */
#define ARM64_SH_INNER          (3ULL << 8)     /**< Inner Shareable */

/** @brief AF (Access Flag) */
#define ARM64_DESC_AF           (1ULL << 10)

/** @brief nG (Not Global) */
#define ARM64_DESC_NG           (1ULL << 11)

/** @brief Contiguous hint */
#define ARM64_DESC_CONT         (1ULL << 52)

/** @brief PXN (Privileged Execute Never) */
#define ARM64_DESC_PXN          (1ULL << 53)

/** @brief UXN (User Execute Never) */
#define ARM64_DESC_UXN          (1ULL << 54)

/** @brief Dirty 标志 (软件定义, bit 55) */
#define ARM64_DESC_DIRTY        (1ULL << 55)

/** @brief COW 标志 (软件定义, bit 56) */
#define ARM64_DESC_COW          (1ULL << 56)

/** @brief 物理地址掩码 (bits 47:12) */
#define ARM64_DESC_ADDR_MASK    0x0000FFFFFFFFF000ULL

/** @brief MAIR 索引定义 */
#define MAIR_IDX_DEVICE         0   /**< Device-nGnRnE */
#define MAIR_IDX_NORMAL_NC      1   /**< Normal Non-Cacheable */
#define MAIR_IDX_NORMAL_WT      2   /**< Normal Write-Through */
#define MAIR_IDX_NORMAL_WB      3   /**< Normal Write-Back (default) */

/* ============================================================================
 * 页表项操作函数实现
 * ========================================================================== */

/**
 * @brief 创建页表项 (ARM64)
 * 
 * 将物理地址和架构无关的标志组合成 ARM64 格式的页描述符。
 * 
 * @param phys 物理地址（必须页对齐）
 * @param flags 架构无关的页表项标志
 * @return ARM64 格式的页描述符
 */
pte_t pgtable_make_entry(paddr_t phys, uint32_t flags) {
    uint64_t arm64_flags = ARM64_DESC_AF;  /* 总是设置 Access Flag */
    
    /* 有效位 */
    if (flags & PTE_PRESENT) {
        arm64_flags |= ARM64_DESC_VALID | ARM64_DESC_PAGE;
    }
    
    /* 访问权限 */
    if (flags & PTE_USER) {
        if (flags & PTE_WRITE) {
            arm64_flags |= ARM64_AP_RW_ALL;     /* EL1/EL0 读写 */
        } else {
            arm64_flags |= ARM64_AP_RO_ALL;     /* EL1/EL0 只读 */
        }
        arm64_flags |= ARM64_DESC_NG;           /* 用户页设置 nG */
    } else {
        if (flags & PTE_WRITE) {
            arm64_flags |= ARM64_AP_RW_EL1;     /* EL1 读写 */
        } else {
            arm64_flags |= ARM64_AP_RO_EL1;     /* EL1 只读 */
        }
    }
    
    /* 内存类型 */
    if (flags & PTE_NOCACHE) {
        arm64_flags |= ((uint64_t)MAIR_IDX_DEVICE << ARM64_DESC_ATTR_SHIFT);
    } else {
        arm64_flags |= ((uint64_t)MAIR_IDX_NORMAL_WB << ARM64_DESC_ATTR_SHIFT);
        arm64_flags |= ARM64_SH_INNER;          /* 普通内存使用 Inner Shareable */
    }
    
    /* 执行权限 */
    if (!(flags & PTE_EXEC)) {
        arm64_flags |= ARM64_DESC_UXN | ARM64_DESC_PXN;  /* 不可执行 */
    }
    
    /* COW 标志 */
    if (flags & PTE_COW) {
        arm64_flags |= ARM64_DESC_COW;
    }
    
    /* 组合物理地址和标志 */
    return (phys & ARM64_DESC_ADDR_MASK) | arm64_flags;
}

/**
 * @brief 从页表项提取物理地址 (ARM64)
 * 
 * @param entry 页描述符
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry) {
    return (paddr_t)(entry & ARM64_DESC_ADDR_MASK);
}

/**
 * @brief 从页表项提取标志 (ARM64)
 * 
 * @param entry 页描述符
 * @return 架构无关的标志
 */
uint32_t pgtable_get_flags(pte_t entry) {
    uint32_t flags = 0;
    
    /* 有效位 */
    if (entry & ARM64_DESC_VALID) {
        flags |= PTE_PRESENT;
    }
    
    /* 访问权限 */
    uint64_t ap = entry & ARM64_DESC_AP_MASK;
    if (ap == ARM64_AP_RW_ALL || ap == ARM64_AP_RO_ALL) {
        flags |= PTE_USER;
    }
    if (ap == ARM64_AP_RW_EL1 || ap == ARM64_AP_RW_ALL) {
        flags |= PTE_WRITE;
    }
    
    /* 内存类型 */
    uint64_t attr_idx = (entry & ARM64_DESC_ATTR_MASK) >> ARM64_DESC_ATTR_SHIFT;
    if (attr_idx == MAIR_IDX_DEVICE || attr_idx == MAIR_IDX_NORMAL_NC) {
        flags |= PTE_NOCACHE;
    }
    
    /* 执行权限 */
    if (!(entry & (ARM64_DESC_UXN | ARM64_DESC_PXN))) {
        flags |= PTE_EXEC;
    }
    
    /* COW 标志 */
    if (entry & ARM64_DESC_COW) {
        flags |= PTE_COW;
    }
    
    /* Dirty 标志 */
    if (entry & ARM64_DESC_DIRTY) {
        flags |= PTE_DIRTY;
    }
    
    /* Accessed 标志 */
    if (entry & ARM64_DESC_AF) {
        flags |= PTE_ACCESSED;
    }
    
    /* Global 标志 (nG 的反义) */
    if (!(entry & ARM64_DESC_NG)) {
        flags |= PTE_GLOBAL;
    }
    
    return flags;
}

/**
 * @brief 检查页表项是否存在 (ARM64)
 */
bool pgtable_is_present(pte_t entry) {
    return (entry & ARM64_DESC_VALID) != 0;
}

/**
 * @brief 检查页表项是否可写 (ARM64)
 */
bool pgtable_is_writable(pte_t entry) {
    uint64_t ap = entry & ARM64_DESC_AP_MASK;
    return (ap == ARM64_AP_RW_EL1 || ap == ARM64_AP_RW_ALL);
}

/**
 * @brief 检查页表项是否用户可访问 (ARM64)
 */
bool pgtable_is_user(pte_t entry) {
    uint64_t ap = entry & ARM64_DESC_AP_MASK;
    return (ap == ARM64_AP_RW_ALL || ap == ARM64_AP_RO_ALL);
}

/**
 * @brief 检查页表项是否为 COW 页 (ARM64)
 */
bool pgtable_is_cow(pte_t entry) {
    return (entry & ARM64_DESC_COW) != 0;
}

/**
 * @brief 检查页表项是否为大页 (ARM64)
 * 
 * @note 在 L1/L2 级别，如果 bit 1 为 0 且 bit 0 为 1，则为块描述符
 */
bool pgtable_is_huge(pte_t entry) {
    /* 块描述符: Valid=1, Table=0 */
    return (entry & ARM64_DESC_VALID) && !(entry & ARM64_DESC_TABLE);
}

/**
 * @brief 检查页表项是否可执行 (ARM64)
 */
bool pgtable_is_executable(pte_t entry) {
    /* 如果 UXN 和 PXN 都没有设置，则可执行 */
    return !(entry & (ARM64_DESC_UXN | ARM64_DESC_PXN));
}

/**
 * @brief 修改页表项标志 (ARM64)
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
 * @brief 获取页表级数 (ARM64)
 * 
 * @return 4 (L0 -> L1 -> L2 -> L3)
 */
uint32_t pgtable_get_levels(void) {
    return 4;
}

/**
 * @brief 获取每级页表的条目数 (ARM64)
 * 
 * @return 512 (4KB granule)
 */
uint32_t pgtable_get_entries_per_level(void) {
    return 512;
}

/**
 * @brief 获取页表项大小 (ARM64)
 * 
 * @return 8 字节
 */
uint32_t pgtable_get_entry_size(void) {
    return 8;
}

/**
 * @brief 检查是否支持 NX 位 (ARM64)
 * 
 * @return true (ARM64 支持 UXN/PXN)
 */
bool pgtable_supports_nx(void) {
    return true;
}

/**
 * @brief 检查是否支持大页 (ARM64)
 * 
 * @return true (支持 2MB 和 1GB 块)
 */
bool pgtable_supports_huge_pages(void) {
    return true;
}

/* ============================================================================
 * 虚拟地址索引提取函数实现
 * ========================================================================== */

/**
 * @brief 从虚拟地址提取顶级页表索引 (ARM64)
 * 
 * @param virt 虚拟地址
 * @return L0 索引 (bits 47:39)
 */
uint32_t pgtable_get_top_index(vaddr_t virt) {
    return ((uint64_t)virt >> 39) & 0x1FF;
}

/**
 * @brief 从虚拟地址提取指定级别的页表索引 (ARM64)
 * 
 * @param virt 虚拟地址
 * @param level 页表级别 (0=L3, 1=L2, 2=L1, 3=L0)
 * @return 指定级别的页表索引
 */
uint32_t pgtable_get_index(vaddr_t virt, uint32_t level) {
    switch (level) {
        case 0:  /* L3 索引 (bits 20:12) */
            return ((uint64_t)virt >> 12) & 0x1FF;
        case 1:  /* L2 索引 (bits 29:21) */
            return ((uint64_t)virt >> 21) & 0x1FF;
        case 2:  /* L1 索引 (bits 38:30) */
            return ((uint64_t)virt >> 30) & 0x1FF;
        case 3:  /* L0 索引 (bits 47:39) */
            return ((uint64_t)virt >> 39) & 0x1FF;
        default:
            return 0;  /* 无效级别 */
    }
}

/* ============================================================================
 * 调试和验证函数实现
 * ========================================================================== */

/**
 * @brief 验证页表项格式 (ARM64)
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
    
    /* ARM64 物理地址最大 48 位 */
    if (phys > 0x0000FFFFFFFFFFFFULL) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取页表项的字符串描述 (ARM64)
 */
int pgtable_entry_to_string(pte_t entry, char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return 0;
    }
    
    if (!pgtable_is_present(entry)) {
        return snprintf(buf, buf_size, "NOT PRESENT");
    }
    
    paddr_t phys = pgtable_get_phys(entry);
    uint32_t flags = pgtable_get_flags(entry);
    
    return snprintf(buf, buf_size, 
                    "phys=0x%012llx %s%s%s%s%s%s%s",
                    (unsigned long long)phys,
                    (flags & PTE_WRITE) ? "W" : "R",
                    (flags & PTE_USER) ? "U" : "K",
                    (flags & PTE_EXEC) ? "X" : "-",
                    (flags & PTE_NOCACHE) ? " NC" : "",
                    (flags & PTE_COW) ? " COW" : "",
                    (flags & PTE_DIRTY) ? " D" : "",
                    (flags & PTE_ACCESSED) ? " A" : "");
}
