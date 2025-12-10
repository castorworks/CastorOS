/**
 * @file mmu.c
 * @brief ARM64 架构特定的 MMU 实现
 * 
 * 实现 ARM64 (AArch64) 的 4 级页表操作
 * 提供 HAL MMU 接口的 ARM64 实现
 * 
 * ARM64 使用 4 级转换表 (4KB granule, 48-bit VA):
 *   - Level 0: 512 个描述符，每个 8 字节
 *   - Level 1: 512 个描述符，每个 8 字节 (可选 1GB 块)
 *   - Level 2: 512 个描述符，每个 8 字节 (可选 2MB 块)
 *   - Level 3: 512 个描述符，每个 8 字节 (4KB 页)
 * 
 * 虚拟地址分解 (48-bit):
 *   [63:48] - TTBR 选择 (0=TTBR0, 1=TTBR1)
 *   [47:39] - Level 0 索引 (9 bits, 512 entries)
 *   [38:30] - Level 1 索引 (9 bits, 512 entries)
 *   [29:21] - Level 2 索引 (9 bits, 512 entries)
 *   [20:12] - Level 3 索引 (9 bits, 512 entries)
 *   [11:0]  - 页内偏移 (12 bits, 4KB page)
 * 
 * Requirements: 6.1, 6.2, 6.4, 6.5
 */

#include <types.h>
#include <hal/hal.h>
#include <mm/mm_types.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>

/* ============================================================================
 * ARM64 页表描述符定义
 * ========================================================================== */

/** 描述符类型 (bits [1:0]) */
#define DESC_TYPE_INVALID       0x0     /**< 无效描述符 */
#define DESC_TYPE_BLOCK         0x1     /**< 块描述符 (L1/L2) */
#define DESC_TYPE_TABLE         0x3     /**< 表描述符 (L0/L1/L2) */
#define DESC_TYPE_PAGE          0x3     /**< 页描述符 (L3) */
#define DESC_TYPE_MASK          0x3

/** 描述符属性位 */
#define DESC_VALID              (1ULL << 0)     /**< 有效位 */
#define DESC_TABLE              (1ULL << 1)     /**< 表/块选择 (1=表) */

/** 块/页描述符属性 (Lower attributes) */
#define DESC_ATTR_INDEX_MASK    (7ULL << 2)     /**< MAIR 索引 (AttrIndx) */
#define DESC_ATTR_INDEX_SHIFT   2
#define DESC_NS                 (1ULL << 5)     /**< Non-Secure */
#define DESC_AP_RW_EL1          (0ULL << 6)     /**< EL1 读写, EL0 无访问 */
#define DESC_AP_RW_ALL          (1ULL << 6)     /**< EL1/EL0 读写 */
#define DESC_AP_RO_EL1          (2ULL << 6)     /**< EL1 只读, EL0 无访问 */
#define DESC_AP_RO_ALL          (3ULL << 6)     /**< EL1/EL0 只读 */
#define DESC_AP_MASK            (3ULL << 6)
#define DESC_SH_NON             (0ULL << 8)     /**< Non-shareable */
#define DESC_SH_OUTER           (2ULL << 8)     /**< Outer Shareable */
#define DESC_SH_INNER           (3ULL << 8)     /**< Inner Shareable */
#define DESC_SH_MASK            (3ULL << 8)
#define DESC_AF                 (1ULL << 10)    /**< Access Flag */
#define DESC_NG                 (1ULL << 11)    /**< Not Global */

/** 块/页描述符属性 (Upper attributes) */
#define DESC_CONT               (1ULL << 52)    /**< Contiguous hint */
#define DESC_PXN                (1ULL << 53)    /**< Privileged Execute Never */
#define DESC_UXN                (1ULL << 54)    /**< User Execute Never */
#define DESC_DIRTY              (1ULL << 55)    /**< Dirty (software) */
#define DESC_COW                (1ULL << 56)    /**< COW flag (software) */

/** 物理地址掩码 (bits 47:12 for 4KB pages) */
#define DESC_ADDR_MASK          0x0000FFFFFFFFF000ULL

/** 页表项数量 */
#define DESC_ENTRIES            512

/** MAIR 索引定义 */
#define MAIR_IDX_DEVICE_nGnRnE  0   /**< Device-nGnRnE (strongly ordered) */
#define MAIR_IDX_NORMAL_NC      1   /**< Normal Non-Cacheable */
#define MAIR_IDX_NORMAL_WT      2   /**< Normal Write-Through */
#define MAIR_IDX_NORMAL_WB      3   /**< Normal Write-Back (default) */

/** MAIR 属性值 */
#define MAIR_DEVICE_nGnRnE      0x00    /**< Device-nGnRnE */
#define MAIR_NORMAL_NC          0x44    /**< Normal Non-Cacheable */
#define MAIR_NORMAL_WT          0xBB    /**< Normal Write-Through */
#define MAIR_NORMAL_WB          0xFF    /**< Normal Write-Back */

/* ============================================================================
 * 地址分解宏
 * ========================================================================== */

/** @brief 获取 Level 0 索引 (bits 47:39) */
static inline uint64_t l0_index(uint64_t virt) {
    return (virt >> 39) & 0x1FF;
}

/** @brief 获取 Level 1 索引 (bits 38:30) */
static inline uint64_t l1_index(uint64_t virt) {
    return (virt >> 30) & 0x1FF;
}

/** @brief 获取 Level 2 索引 (bits 29:21) */
static inline uint64_t l2_index(uint64_t virt) {
    return (virt >> 21) & 0x1FF;
}

/** @brief 获取 Level 3 索引 (bits 20:12) */
static inline uint64_t l3_index(uint64_t virt) {
    return (virt >> 12) & 0x1FF;
}

/** @brief 从描述符中提取物理地址 */
static inline uint64_t desc_get_addr(uint64_t desc) {
    return desc & DESC_ADDR_MASK;
}

/** @brief 检查描述符是否有效 */
static inline bool desc_is_valid(uint64_t desc) {
    return (desc & DESC_VALID) != 0;
}

/** @brief 检查是否为表描述符 */
static inline bool desc_is_table(uint64_t desc) {
    return (desc & DESC_TYPE_MASK) == DESC_TYPE_TABLE;
}

/** @brief 检查是否为块描述符 */
static inline bool desc_is_block(uint64_t desc) {
    return desc_is_valid(desc) && ((desc & DESC_TYPE_MASK) == DESC_TYPE_BLOCK);
}

/* ============================================================================
 * ARM64 系统寄存器操作
 * ========================================================================== */

/** @brief 读取 TTBR0_EL1 (用户空间页表基址) */
static inline uint64_t read_ttbr0_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(val));
    return val;
}

/** @brief 写入 TTBR0_EL1 */
static inline void write_ttbr0_el1(uint64_t val) {
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"(val));
}

/** @brief 读取 TTBR1_EL1 (内核空间页表基址) */
static inline uint64_t read_ttbr1_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(val));
    return val;
}

/** @brief 写入 TTBR1_EL1 */
static inline void write_ttbr1_el1(uint64_t val) {
    __asm__ volatile("msr ttbr1_el1, %0" : : "r"(val));
}

/** @brief 读取 TCR_EL1 (Translation Control Register) */
static inline uint64_t read_tcr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(val));
    return val;
}

/** @brief 写入 TCR_EL1 */
static inline void write_tcr_el1(uint64_t val) {
    __asm__ volatile("msr tcr_el1, %0" : : "r"(val));
}

/** @brief 读取 MAIR_EL1 (Memory Attribute Indirection Register) */
static inline uint64_t read_mair_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(val));
    return val;
}

/** @brief 写入 MAIR_EL1 */
static inline void write_mair_el1(uint64_t val) {
    __asm__ volatile("msr mair_el1, %0" : : "r"(val));
}

/** @brief 读取 SCTLR_EL1 (System Control Register) */
static inline uint64_t read_sctlr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(val));
    return val;
}

/** @brief 写入 SCTLR_EL1 */
static inline void write_sctlr_el1(uint64_t val) {
    __asm__ volatile("msr sctlr_el1, %0" : : "r"(val));
}

/** @brief 读取 FAR_EL1 (Fault Address Register) */
static inline uint64_t read_far_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, far_el1" : "=r"(val));
    return val;
}

/** @brief 读取 ESR_EL1 (Exception Syndrome Register) */
static inline uint64_t read_esr_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(val));
    return val;
}

/* ============================================================================
 * TLB 和屏障操作
 * ========================================================================== */

/** @brief 数据同步屏障 */
static inline void dsb_sy(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

/** @brief 数据同步屏障 (inner shareable) */
static inline void dsb_ish(void) {
    __asm__ volatile("dsb ish" ::: "memory");
}

/** @brief 指令同步屏障 */
static inline void isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

/** @brief 刷新单个 TLB 条目 (by VA, inner shareable) */
static inline void tlbi_vaae1is(uint64_t virt) {
    /* TLBI VAAE1IS: TLB Invalidate by VA, All ASID, EL1, Inner Shareable */
    uint64_t addr = (virt >> 12) & 0xFFFFFFFFFFFULL;
    __asm__ volatile("tlbi vaae1is, %0" : : "r"(addr));
}

/** @brief 刷新所有 TLB 条目 (EL1, inner shareable) */
static inline void tlbi_vmalle1is(void) {
    /* TLBI VMALLE1IS: TLB Invalidate by VMID, All at stage 1, EL1, Inner Shareable */
    __asm__ volatile("tlbi vmalle1is");
}

/* ============================================================================
 * HAL MMU 接口实现 - ARM64
 * 
 * Requirements: 6.1, 6.5
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目 (ARM64)
 * @param virt 虚拟地址
 * 
 * @see Requirements 6.5
 */
void hal_mmu_flush_tlb(vaddr_t virt) {
    dsb_ish();
    tlbi_vaae1is((uint64_t)virt);
    dsb_ish();
    isb();
}

/**
 * @brief 刷新整个 TLB (ARM64)
 * 
 * @see Requirements 6.5
 */
void hal_mmu_flush_tlb_all(void) {
    dsb_ish();
    tlbi_vmalle1is();
    dsb_ish();
    isb();
}

/**
 * @brief 切换地址空间 (ARM64)
 * @param space 新地址空间的物理地址 (Level 0 表)
 * 
 * 更新 TTBR0_EL1 并执行必要的屏障操作。
 * 
 * @see Requirements 6.5
 */
void hal_mmu_switch_space(paddr_t space) {
    dsb_ish();
    write_ttbr0_el1((uint64_t)space);
    isb();
    /* TLB invalidation is typically done by the caller if needed */
}

/**
 * @brief 获取页错误地址 (ARM64)
 * @return FAR_EL1 寄存器中的错误地址
 * 
 * @see Requirements 6.4
 */
vaddr_t hal_mmu_get_fault_addr(void) {
    return (vaddr_t)read_far_el1();
}

/**
 * @brief 获取当前用户空间页表物理地址 (ARM64)
 * @return TTBR0_EL1 寄存器的值 (物理地址部分)
 */
paddr_t hal_mmu_get_current_page_table(void) {
    /* TTBR0_EL1 bits [47:1] contain the physical address */
    return (paddr_t)(read_ttbr0_el1() & 0x0000FFFFFFFFFFFCULL);
}

/**
 * @brief 获取当前地址空间 (ARM64)
 * @return 当前 Level 0 表的物理地址
 */
hal_addr_space_t hal_mmu_current_space(void) {
    return (hal_addr_space_t)hal_mmu_get_current_page_table();
}

/**
 * @brief 检查 MMU 是否启用 (ARM64)
 * @return true 如果 MMU 已启用
 */
bool hal_mmu_is_paging_enabled(void) {
    uint64_t sctlr = read_sctlr_el1();
    return (sctlr & 0x1) != 0;  /* M bit */
}

/**
 * @brief 启用 MMU (ARM64)
 * 
 * 设置 SCTLR_EL1.M 位启用 MMU
 * 注意：在调用此函数前必须先配置好 TCR_EL1, MAIR_EL1, TTBR0/1_EL1
 */
void hal_mmu_enable_paging(void) {
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= 0x1;  /* Set M bit */
    dsb_sy();
    write_sctlr_el1(sctlr);
    isb();
}

/* ============================================================================
 * TCR_EL1 配置
 * ========================================================================== */

/** TCR_EL1 字段定义 */
#define TCR_T0SZ_SHIFT      0       /**< TTBR0 region size */
#define TCR_EPD0            (1ULL << 7)     /**< Disable TTBR0 walks */
#define TCR_IRGN0_WB_WA     (1ULL << 8)     /**< Inner Write-Back Write-Allocate */
#define TCR_ORGN0_WB_WA     (1ULL << 10)    /**< Outer Write-Back Write-Allocate */
#define TCR_SH0_INNER       (3ULL << 12)    /**< Inner Shareable */
#define TCR_TG0_4KB         (0ULL << 14)    /**< 4KB granule for TTBR0 */
#define TCR_T1SZ_SHIFT      16      /**< TTBR1 region size */
#define TCR_A1              (1ULL << 22)    /**< ASID from TTBR1 */
#define TCR_EPD1            (1ULL << 23)    /**< Disable TTBR1 walks */
#define TCR_IRGN1_WB_WA     (1ULL << 24)    /**< Inner Write-Back Write-Allocate */
#define TCR_ORGN1_WB_WA     (1ULL << 26)    /**< Outer Write-Back Write-Allocate */
#define TCR_SH1_INNER       (3ULL << 28)    /**< Inner Shareable */
#define TCR_TG1_4KB         (2ULL << 30)    /**< 4KB granule for TTBR1 */
#define TCR_IPS_48BIT       (5ULL << 32)    /**< 48-bit physical address */
#define TCR_AS_16BIT        (1ULL << 36)    /**< 16-bit ASID */

/**
 * @brief 初始化 MMU (ARM64)
 * 
 * 配置 TCR_EL1 和 MAIR_EL1 寄存器。
 * 
 * TCR_EL1 配置:
 *   - T0SZ = 16 (48-bit VA for TTBR0, user space)
 *   - T1SZ = 16 (48-bit VA for TTBR1, kernel space)
 *   - TG0 = 4KB granule
 *   - TG1 = 4KB granule
 *   - IPS = 48-bit physical address
 *   - Inner/Outer cacheable, Inner Shareable
 * 
 * MAIR_EL1 配置:
 *   - Index 0: Device-nGnRnE (0x00)
 *   - Index 1: Normal Non-Cacheable (0x44)
 *   - Index 2: Normal Write-Through (0xBB)
 *   - Index 3: Normal Write-Back (0xFF)
 * 
 * @see Requirements 6.1
 */
void hal_mmu_init(void) {
    /* Configure MAIR_EL1 */
    uint64_t mair = ((uint64_t)MAIR_DEVICE_nGnRnE << (MAIR_IDX_DEVICE_nGnRnE * 8)) |
                    ((uint64_t)MAIR_NORMAL_NC << (MAIR_IDX_NORMAL_NC * 8)) |
                    ((uint64_t)MAIR_NORMAL_WT << (MAIR_IDX_NORMAL_WT * 8)) |
                    ((uint64_t)MAIR_NORMAL_WB << (MAIR_IDX_NORMAL_WB * 8));
    write_mair_el1(mair);
    
    /* Configure TCR_EL1 for 48-bit VA, 4KB granule */
    uint64_t tcr = (16ULL << TCR_T0SZ_SHIFT) |      /* T0SZ = 16 -> 48-bit VA */
                   TCR_IRGN0_WB_WA |
                   TCR_ORGN0_WB_WA |
                   TCR_SH0_INNER |
                   TCR_TG0_4KB |
                   (16ULL << TCR_T1SZ_SHIFT) |      /* T1SZ = 16 -> 48-bit VA */
                   TCR_IRGN1_WB_WA |
                   TCR_ORGN1_WB_WA |
                   TCR_SH1_INNER |
                   TCR_TG1_4KB |
                   TCR_IPS_48BIT;
    write_tcr_el1(tcr);
    
    isb();
    
    LOG_INFO_MSG("ARM64 MMU: TCR_EL1 = 0x%llx, MAIR_EL1 = 0x%llx\n",
                 (unsigned long long)tcr, (unsigned long long)mair);
}



/* ============================================================================
 * 页表操作辅助函数
 * ========================================================================== */

/**
 * @brief 获取指定地址空间的 Level 0 表虚拟地址
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @return Level 0 表的虚拟地址
 */
static uint64_t* get_l0_table(hal_addr_space_t space) {
    paddr_t l0_phys;
    
    if (space == HAL_ADDR_SPACE_CURRENT || space == 0) {
        l0_phys = hal_mmu_get_current_page_table();
    } else {
        l0_phys = space;
    }
    
    return (uint64_t*)PADDR_TO_KVADDR(l0_phys);
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
 * @brief 将 HAL 页标志转换为 ARM64 描述符属性
 * @param hal_flags HAL 页标志 (HAL_PAGE_*)
 * @return ARM64 描述符属性
 */
static uint64_t hal_flags_to_arm64(uint32_t hal_flags) {
    uint64_t arm64_flags = DESC_AF;  /* Always set Access Flag */
    
    if (hal_flags & HAL_PAGE_PRESENT) {
        arm64_flags |= DESC_VALID;
    }
    
    /* Access permissions */
    if (hal_flags & HAL_PAGE_USER) {
        if (hal_flags & HAL_PAGE_WRITE) {
            arm64_flags |= DESC_AP_RW_ALL;  /* EL1/EL0 read-write */
        } else {
            arm64_flags |= DESC_AP_RO_ALL;  /* EL1/EL0 read-only */
        }
    } else {
        if (hal_flags & HAL_PAGE_WRITE) {
            arm64_flags |= DESC_AP_RW_EL1;  /* EL1 read-write only */
        } else {
            arm64_flags |= DESC_AP_RO_EL1;  /* EL1 read-only only */
        }
    }
    
    /* Memory type */
    if (hal_flags & HAL_PAGE_NOCACHE) {
        arm64_flags |= ((uint64_t)MAIR_IDX_DEVICE_nGnRnE << DESC_ATTR_INDEX_SHIFT);
    } else {
        arm64_flags |= ((uint64_t)MAIR_IDX_NORMAL_WB << DESC_ATTR_INDEX_SHIFT);
        arm64_flags |= DESC_SH_INNER;  /* Inner Shareable for normal memory */
    }
    
    /* Execute permissions */
    if (!(hal_flags & HAL_PAGE_EXEC)) {
        arm64_flags |= DESC_UXN | DESC_PXN;  /* No execute */
    }
    
    /* COW flag (software defined) */
    if (hal_flags & HAL_PAGE_COW) {
        arm64_flags |= DESC_COW;
    }
    
    /* Non-global for user pages */
    if (hal_flags & HAL_PAGE_USER) {
        arm64_flags |= DESC_NG;
    }
    
    return arm64_flags;
}

/**
 * @brief 将 ARM64 描述符属性转换为 HAL 页标志
 * @param arm64_flags ARM64 描述符属性
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t arm64_flags_to_hal(uint64_t arm64_flags) {
    uint32_t hal_flags = 0;
    
    if (arm64_flags & DESC_VALID) {
        hal_flags |= HAL_PAGE_PRESENT;
    }
    
    /* Access permissions */
    uint64_t ap = arm64_flags & DESC_AP_MASK;
    if (ap == DESC_AP_RW_ALL || ap == DESC_AP_RO_ALL) {
        hal_flags |= HAL_PAGE_USER;
    }
    if (ap == DESC_AP_RW_EL1 || ap == DESC_AP_RW_ALL) {
        hal_flags |= HAL_PAGE_WRITE;
    }
    
    /* Memory type */
    uint64_t attr_idx = (arm64_flags & DESC_ATTR_INDEX_MASK) >> DESC_ATTR_INDEX_SHIFT;
    if (attr_idx == MAIR_IDX_DEVICE_nGnRnE || attr_idx == MAIR_IDX_NORMAL_NC) {
        hal_flags |= HAL_PAGE_NOCACHE;
    }
    
    /* Execute permissions */
    if (!(arm64_flags & (DESC_UXN | DESC_PXN))) {
        hal_flags |= HAL_PAGE_EXEC;
    }
    
    /* COW flag */
    if (arm64_flags & DESC_COW) {
        hal_flags |= HAL_PAGE_COW;
    }
    
    /* Dirty flag (software) */
    if (arm64_flags & DESC_DIRTY) {
        hal_flags |= HAL_PAGE_DIRTY;
    }
    
    /* Accessed flag */
    if (arm64_flags & DESC_AF) {
        hal_flags |= HAL_PAGE_ACCESSED;
    }
    
    return hal_flags;
}

/* Note: is_user_address and is_kernel_address helpers can be added when needed */

/* ============================================================================
 * HAL MMU 页表操作实现 - ARM64
 * 
 * Requirements: 6.2
 * ========================================================================== */

/**
 * @brief 查询虚拟地址映射 (ARM64)
 * 
 * 遍历 4 级转换表结构，获取虚拟地址对应的物理地址和标志。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @param[out] phys 物理地址 (可为 NULL)
 * @param[out] flags HAL 页标志 (可为 NULL)
 * @return true 如果映射存在，false 如果未映射
 * 
 * @see Requirements 6.2
 */
bool hal_mmu_query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags) {
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    uint64_t l3_idx = l3_index((uint64_t)virt);
    
    /* Level 0 */
    uint64_t l0e = l0[l0_idx];
    if (!desc_is_valid(l0e) || !desc_is_table(l0e)) {
        return false;
    }
    
    /* Level 1 */
    uint64_t *l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0e));
    uint64_t l1e = l1[l1_idx];
    if (!desc_is_valid(l1e)) {
        return false;
    }
    
    /* Check for 1GB block */
    if (desc_is_block(l1e)) {
        if (phys != NULL) {
            /* 1GB block: bits 29:0 are offset */
            *phys = desc_get_addr(l1e) | ((uint64_t)virt & 0x3FFFFFFFULL);
        }
        if (flags != NULL) {
            *flags = arm64_flags_to_hal(l1e);
        }
        return true;
    }
    
    if (!desc_is_table(l1e)) {
        return false;
    }
    
    /* Level 2 */
    uint64_t *l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1e));
    uint64_t l2e = l2[l2_idx];
    if (!desc_is_valid(l2e)) {
        return false;
    }
    
    /* Check for 2MB block */
    if (desc_is_block(l2e)) {
        if (phys != NULL) {
            /* 2MB block: bits 20:0 are offset */
            *phys = desc_get_addr(l2e) | ((uint64_t)virt & 0x1FFFFFULL);
        }
        if (flags != NULL) {
            *flags = arm64_flags_to_hal(l2e);
        }
        return true;
    }
    
    if (!desc_is_table(l2e)) {
        return false;
    }
    
    /* Level 3 */
    uint64_t *l3 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l2e));
    uint64_t l3e = l3[l3_idx];
    if (!desc_is_valid(l3e)) {
        return false;
    }
    
    /* Extract physical address and flags */
    if (phys != NULL) {
        *phys = desc_get_addr(l3e);
    }
    
    if (flags != NULL) {
        *flags = arm64_flags_to_hal(l3e);
    }
    
    return true;
}

/**
 * @brief 映射虚拟页到物理页 (ARM64)
 * 
 * 在 4 级转换表中创建映射，自动分配中间页表级别。
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址 (必须页对齐)
 * @param phys 物理地址 (必须页对齐)
 * @param flags HAL 页标志
 * @return true 成功，false 失败
 * 
 * @note 调用者需要在映射后调用 hal_mmu_flush_tlb()
 * 
 * @see Requirements 6.2
 */
bool hal_mmu_map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    /* Validate addresses */
    if (!IS_VADDR_ALIGNED(virt) || !IS_PADDR_ALIGNED(phys)) {
        LOG_ERROR_MSG("hal_mmu_map: addresses not page-aligned\n");
        return false;
    }
    
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    uint64_t l3_idx = l3_index((uint64_t)virt);
    
    /* Convert HAL flags to ARM64 flags */
    uint64_t arm64_flags = hal_flags_to_arm64(flags);
    
    /* Table descriptor flags: valid + table type */
    uint64_t table_flags = DESC_VALID | DESC_TABLE;
    
    /* Level 0 -> Level 1 */
    uint64_t *l1;
    if (!desc_is_valid(l0[l0_idx])) {
        paddr_t l1_phys = alloc_page_table();
        if (l1_phys == PADDR_INVALID) {
            return false;
        }
        l0[l0_idx] = l1_phys | table_flags;
    } else if (!desc_is_table(l0[l0_idx])) {
        LOG_ERROR_MSG("hal_mmu_map: L0 entry is not a table\n");
        return false;
    }
    l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0[l0_idx]));
    
    /* Level 1 -> Level 2 */
    uint64_t *l2;
    if (!desc_is_valid(l1[l1_idx])) {
        paddr_t l2_phys = alloc_page_table();
        if (l2_phys == PADDR_INVALID) {
            return false;
        }
        l1[l1_idx] = l2_phys | table_flags;
    } else if (desc_is_block(l1[l1_idx])) {
        LOG_ERROR_MSG("hal_mmu_map: cannot map 4KB page over 1GB block\n");
        return false;
    }
    l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1[l1_idx]));
    
    /* Level 2 -> Level 3 */
    uint64_t *l3;
    if (!desc_is_valid(l2[l2_idx])) {
        paddr_t l3_phys = alloc_page_table();
        if (l3_phys == PADDR_INVALID) {
            return false;
        }
        l2[l2_idx] = l3_phys | table_flags;
    } else if (desc_is_block(l2[l2_idx])) {
        LOG_ERROR_MSG("hal_mmu_map: cannot map 4KB page over 2MB block\n");
        return false;
    }
    l3 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l2[l2_idx]));
    
    /* Level 3: Page descriptor */
    l3[l3_idx] = phys | arm64_flags | DESC_TYPE_PAGE;
    
    return true;
}

/**
 * @brief 取消虚拟页映射 (ARM64)
 * 
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt 虚拟地址
 * @return 原物理地址，未映射返回 PADDR_INVALID
 * 
 * @note 调用者需要在取消映射后调用 hal_mmu_flush_tlb()
 * @note 此函数不释放中间页表级别
 * 
 * @see Requirements 6.2
 */
paddr_t hal_mmu_unmap(hal_addr_space_t space, vaddr_t virt) {
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    uint64_t l3_idx = l3_index((uint64_t)virt);
    
    /* Level 0 */
    uint64_t l0e = l0[l0_idx];
    if (!desc_is_valid(l0e) || !desc_is_table(l0e)) {
        return PADDR_INVALID;
    }
    
    /* Level 1 */
    uint64_t *l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0e));
    uint64_t l1e = l1[l1_idx];
    if (!desc_is_valid(l1e)) {
        return PADDR_INVALID;
    }
    
    /* Cannot unmap 1GB block with this function */
    if (desc_is_block(l1e)) {
        LOG_ERROR_MSG("hal_mmu_unmap: cannot unmap 1GB block\n");
        return PADDR_INVALID;
    }
    
    if (!desc_is_table(l1e)) {
        return PADDR_INVALID;
    }
    
    /* Level 2 */
    uint64_t *l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1e));
    uint64_t l2e = l2[l2_idx];
    if (!desc_is_valid(l2e)) {
        return PADDR_INVALID;
    }
    
    /* Cannot unmap 2MB block with this function */
    if (desc_is_block(l2e)) {
        LOG_ERROR_MSG("hal_mmu_unmap: cannot unmap 2MB block\n");
        return PADDR_INVALID;
    }
    
    if (!desc_is_table(l2e)) {
        return PADDR_INVALID;
    }
    
    /* Level 3 */
    uint64_t *l3 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l2e));
    uint64_t l3e = l3[l3_idx];
    if (!desc_is_valid(l3e)) {
        return PADDR_INVALID;
    }
    
    /* Get physical address before clearing */
    paddr_t phys = desc_get_addr(l3e);
    
    /* Clear the entry */
    l3[l3_idx] = 0;
    
    return phys;
}

/**
 * @brief 修改页表项标志 (ARM64)
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
 * @see Requirements 6.2
 */
bool hal_mmu_protect(hal_addr_space_t space, vaddr_t virt, 
                     uint32_t set_flags, uint32_t clear_flags) {
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    uint64_t l3_idx = l3_index((uint64_t)virt);
    
    /* Level 0 */
    uint64_t l0e = l0[l0_idx];
    if (!desc_is_valid(l0e) || !desc_is_table(l0e)) {
        return false;
    }
    
    /* Level 1 */
    uint64_t *l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0e));
    uint64_t l1e = l1[l1_idx];
    if (!desc_is_valid(l1e)) {
        return false;
    }
    
    /* Handle 1GB block */
    if (desc_is_block(l1e)) {
        uint64_t arm64_set = hal_flags_to_arm64(set_flags);
        uint64_t arm64_clear = hal_flags_to_arm64(clear_flags);
        
        paddr_t frame = desc_get_addr(l1e);
        uint64_t current_flags = l1e & ~DESC_ADDR_MASK;
        
        current_flags |= arm64_set;
        current_flags &= ~arm64_clear;
        
        l1[l1_idx] = frame | current_flags;
        return true;
    }
    
    if (!desc_is_table(l1e)) {
        return false;
    }
    
    /* Level 2 */
    uint64_t *l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1e));
    uint64_t l2e = l2[l2_idx];
    if (!desc_is_valid(l2e)) {
        return false;
    }
    
    /* Handle 2MB block */
    if (desc_is_block(l2e)) {
        uint64_t arm64_set = hal_flags_to_arm64(set_flags);
        uint64_t arm64_clear = hal_flags_to_arm64(clear_flags);
        
        paddr_t frame = desc_get_addr(l2e);
        uint64_t current_flags = l2e & ~DESC_ADDR_MASK;
        
        current_flags |= arm64_set;
        current_flags &= ~arm64_clear;
        
        l2[l2_idx] = frame | current_flags;
        return true;
    }
    
    if (!desc_is_table(l2e)) {
        return false;
    }
    
    /* Level 3 */
    uint64_t *l3 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l2e));
    uint64_t *l3e = &l3[l3_idx];
    if (!desc_is_valid(*l3e)) {
        return false;
    }
    
    /* Convert HAL flags to ARM64 flags */
    uint64_t arm64_set = hal_flags_to_arm64(set_flags);
    uint64_t arm64_clear = hal_flags_to_arm64(clear_flags);
    
    /* Modify flags */
    paddr_t frame = desc_get_addr(*l3e);
    uint64_t current_flags = *l3e & ~DESC_ADDR_MASK;
    
    current_flags |= arm64_set;
    current_flags &= ~arm64_clear;
    
    *l3e = frame | current_flags;
    
    return true;
}

/**
 * @brief 虚拟地址转物理地址 (ARM64)
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
 * ARM64 地址空间管理实现
 * 
 * Requirements: 6.2
 * ========================================================================== */

/** @brief 内核空间 L0 索引起始 (256 = 0xFFFF000000000000) */
#define KERNEL_L0_START     256

/** @brief 内核空间 L0 索引结束 (512) */
#define KERNEL_L0_END       512

/** @brief 用户空间 L0 索引起始 (0) */
#define USER_L0_START       0

/** @brief 用户空间 L0 索引结束 (256) */
#define USER_L0_END         256

/**
 * @brief 创建新地址空间 (ARM64)
 * 
 * 分配并初始化新的 Level 0 表，内核空间映射从当前 L0 表复制。
 * 
 * ARM64 地址空间布局：
 *   - L0[0..255]: 用户空间 (0x0000000000000000 - 0x0000FFFFFFFFFFFF, TTBR0)
 *   - L0[256..511]: 内核空间 (0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF, TTBR1)
 * 
 * @return 新地址空间句柄 (L0 表物理地址)，失败返回 HAL_ADDR_SPACE_INVALID
 * 
 * @see Requirements 6.2
 */
hal_addr_space_t hal_mmu_create_space(void) {
    /* Allocate a new Level 0 table */
    paddr_t l0_phys = alloc_page_table();
    if (l0_phys == PADDR_INVALID) {
        LOG_ERROR_MSG("hal_mmu_create_space: Failed to allocate L0 table\n");
        return HAL_ADDR_SPACE_INVALID;
    }
    
    uint64_t *new_l0 = (uint64_t*)PADDR_TO_KVADDR(l0_phys);
    
    /* Get current L0 for copying kernel mappings */
    uint64_t *current_l0 = get_l0_table(HAL_ADDR_SPACE_CURRENT);
    
    /* Clear user space entries (L0[0..255]) */
    for (uint32_t i = USER_L0_START; i < USER_L0_END; i++) {
        new_l0[i] = 0;
    }
    
    /* Copy kernel space entries (L0[256..511]) */
    /* These are shared across all address spaces */
    for (uint32_t i = KERNEL_L0_START; i < KERNEL_L0_END; i++) {
        new_l0[i] = current_l0[i];
    }
    
    LOG_DEBUG_MSG("hal_mmu_create_space: Created new L0 table at phys 0x%llx\n", 
                  (unsigned long long)l0_phys);
    
    return (hal_addr_space_t)l0_phys;
}

/**
 * @brief 递归释放页表结构 (ARM64)
 * 
 * 释放指定级别的页表及其所有子页表。
 * 对于叶子页表项（物理页），递减引用计数。
 * 
 * @param table_phys 页表物理地址
 * @param level 页表级别 (3=L1, 2=L2, 1=L3)
 */
static void free_page_table_recursive(paddr_t table_phys, int level) {
    if (table_phys == PADDR_INVALID || table_phys == 0) {
        return;
    }
    
    uint64_t *table = (uint64_t*)PADDR_TO_KVADDR(table_phys);
    
    for (uint32_t i = 0; i < DESC_ENTRIES; i++) {
        uint64_t entry = table[i];
        
        if (!desc_is_valid(entry)) {
            continue;
        }
        
        paddr_t frame = desc_get_addr(entry);
        
        if (level == 1) {
            /* Level 3 (L3): entries point to physical pages */
            /* Decrement reference count for shared pages (COW) */
            uint32_t refcount = pmm_frame_get_refcount(frame);
            if (refcount > 0) {
                pmm_frame_ref_dec(frame);
                if (refcount == 1) {
                    LOG_DEBUG_MSG("free_page_table_recursive: Freed physical page 0x%llx\n",
                                  (unsigned long long)frame);
                }
            }
        } else if (desc_is_block(entry)) {
            /* Block descriptor (1GB or 2MB) - decrement refcount */
            uint32_t refcount = pmm_frame_get_refcount(frame);
            if (refcount > 0) {
                pmm_frame_ref_dec(frame);
            }
        } else if (desc_is_table(entry)) {
            /* Table descriptor, recurse into child table */
            free_page_table_recursive(frame, level - 1);
        }
    }
    
    /* Free this page table itself */
    pmm_free_frame(table_phys);
}

/**
 * @brief 销毁地址空间 (ARM64)
 * 
 * 释放 L0 表和所有用户空间页表，递减共享物理页的引用计数。
 * 内核空间页表是共享的，不释放。
 * 
 * @param space 要销毁的地址空间句柄
 * 
 * @warning 不能销毁当前活动的地址空间
 * 
 * @see Requirements 6.2
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
    
    uint64_t *l0 = (uint64_t*)PADDR_TO_KVADDR(space);
    
    LOG_DEBUG_MSG("hal_mmu_destroy_space: Destroying address space at phys 0x%llx\n",
                  (unsigned long long)space);
    
    /* Free user space page tables (L0[0..255]) */
    for (uint32_t i = USER_L0_START; i < USER_L0_END; i++) {
        uint64_t l0e = l0[i];
        
        if (!desc_is_valid(l0e) || !desc_is_table(l0e)) {
            continue;
        }
        
        paddr_t l1_phys = desc_get_addr(l0e);
        
        /* Recursively free L1 and its children */
        /* Level 3 = L1, Level 2 = L2, Level 1 = L3 */
        free_page_table_recursive(l1_phys, 3);
    }
    
    /* Free the L0 table itself */
    pmm_free_frame(space);
    
    LOG_DEBUG_MSG("hal_mmu_destroy_space: Address space destroyed\n");
}

/**
 * @brief 递归克隆页表结构 (ARM64, COW 语义)
 * 
 * 克隆指定级别的页表，对于叶子页表项：
 * - 可写页面被标记为只读 + COW
 * - 物理页的引用计数增加
 * 
 * @param src_table_phys 源页表物理地址
 * @param level 页表级别 (3=L1, 2=L2, 1=L3)
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
    
    uint64_t *src_table = (uint64_t*)PADDR_TO_KVADDR(src_table_phys);
    uint64_t *dst_table = (uint64_t*)PADDR_TO_KVADDR(new_table_phys);
    
    for (uint32_t i = 0; i < DESC_ENTRIES; i++) {
        uint64_t entry = src_table[i];
        
        if (!desc_is_valid(entry)) {
            dst_table[i] = 0;
            continue;
        }
        
        paddr_t frame = desc_get_addr(entry);
        uint64_t flags = entry & ~DESC_ADDR_MASK;
        
        if (level == 1) {
            /* Level 3 (L3): entries point to physical pages */
            /* Apply COW semantics: mark writable pages as read-only + COW */
            uint64_t ap = flags & DESC_AP_MASK;
            if (ap == DESC_AP_RW_ALL || ap == DESC_AP_RW_EL1) {
                /* Change to read-only */
                flags &= ~DESC_AP_MASK;
                flags |= (ap == DESC_AP_RW_ALL) ? DESC_AP_RO_ALL : DESC_AP_RO_EL1;
                flags |= DESC_COW;  /* Mark as COW */
                
                /* Update source entry as well (both parent and child are COW) */
                src_table[i] = frame | flags;
            }
            
            /* Increment reference count for shared physical page */
            pmm_frame_ref_inc(frame);
            
            /* Copy entry to destination */
            dst_table[i] = frame | flags;
            
        } else if (desc_is_block(entry)) {
            /* Block descriptor (1GB or 2MB) - apply COW semantics */
            uint64_t ap = flags & DESC_AP_MASK;
            if (ap == DESC_AP_RW_ALL || ap == DESC_AP_RW_EL1) {
                flags &= ~DESC_AP_MASK;
                flags |= (ap == DESC_AP_RW_ALL) ? DESC_AP_RO_ALL : DESC_AP_RO_EL1;
                flags |= DESC_COW;
                src_table[i] = frame | flags;
            }
            
            pmm_frame_ref_inc(frame);
            dst_table[i] = frame | flags;
            
        } else if (desc_is_table(entry)) {
            /* Table descriptor, recurse into child table */
            paddr_t child_dst_phys;
            if (!clone_page_table_recursive(frame, level - 1, &child_dst_phys)) {
                /* Clone failed, need to clean up */
                for (uint32_t j = 0; j < i; j++) {
                    if (desc_is_valid(dst_table[j])) {
                        paddr_t child_phys = desc_get_addr(dst_table[j]);
                        if (desc_is_table(dst_table[j])) {
                            free_page_table_recursive(child_phys, level - 1);
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
        } else {
            /* Invalid entry type */
            dst_table[i] = 0;
        }
    }
    
    *dst_table_phys = new_table_phys;
    return true;
}

/**
 * @brief 克隆地址空间 (ARM64, COW 语义)
 * 
 * 创建地址空间的副本，用户空间页面使用 Copy-on-Write 语义：
 * - 用户页面被标记为只读 + COW
 * - 物理页面的引用计数增加
 * - 内核空间直接共享（不复制）
 * 
 * @param src 源地址空间句柄
 * @return 新地址空间句柄，失败返回 HAL_ADDR_SPACE_INVALID
 * 
 * @see Requirements 6.2
 */
hal_addr_space_t hal_mmu_clone_space(hal_addr_space_t src) {
    /* Validate source address space */
    if (src == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    /* Get source L0 */
    paddr_t src_phys = (src == HAL_ADDR_SPACE_CURRENT || src == 0) 
                       ? hal_mmu_get_current_page_table() 
                       : src;
    
    /* Allocate new L0 */
    paddr_t new_l0_phys = alloc_page_table();
    if (new_l0_phys == PADDR_INVALID) {
        LOG_ERROR_MSG("hal_mmu_clone_space: Failed to allocate L0 table\n");
        return HAL_ADDR_SPACE_INVALID;
    }
    
    uint64_t *src_l0 = (uint64_t*)PADDR_TO_KVADDR(src_phys);
    uint64_t *new_l0 = (uint64_t*)PADDR_TO_KVADDR(new_l0_phys);
    
    LOG_DEBUG_MSG("hal_mmu_clone_space: Cloning address space from 0x%llx to 0x%llx\n",
                  (unsigned long long)src_phys, (unsigned long long)new_l0_phys);
    
    /* Clone user space (L0[0..255]) with COW semantics */
    for (uint32_t i = USER_L0_START; i < USER_L0_END; i++) {
        uint64_t l0e = src_l0[i];
        
        if (!desc_is_valid(l0e)) {
            new_l0[i] = 0;
            continue;
        }
        
        if (!desc_is_table(l0e)) {
            /* Invalid L0 entry type */
            new_l0[i] = 0;
            continue;
        }
        
        paddr_t src_l1_phys = desc_get_addr(l0e);
        uint64_t l0e_flags = l0e & 0xFFF;
        
        /* Recursively clone L1 and its children */
        paddr_t new_l1_phys;
        if (!clone_page_table_recursive(src_l1_phys, 3, &new_l1_phys)) {
            LOG_ERROR_MSG("hal_mmu_clone_space: Failed to clone L1 at index %u\n", i);
            
            /* Clean up already cloned entries */
            for (uint32_t j = USER_L0_START; j < i; j++) {
                if (desc_is_valid(new_l0[j]) && desc_is_table(new_l0[j])) {
                    paddr_t l1_phys = desc_get_addr(new_l0[j]);
                    free_page_table_recursive(l1_phys, 3);
                }
            }
            pmm_free_frame(new_l0_phys);
            return HAL_ADDR_SPACE_INVALID;
        }
        
        new_l0[i] = new_l1_phys | l0e_flags;
    }
    
    /* Copy kernel space entries (L0[256..511]) - shared, not cloned */
    for (uint32_t i = KERNEL_L0_START; i < KERNEL_L0_END; i++) {
        new_l0[i] = src_l0[i];
    }
    
    /* Flush TLB for source address space (we modified COW flags) */
    if (src_phys == hal_mmu_get_current_page_table()) {
        hal_mmu_flush_tlb_all();
    }
    
    LOG_DEBUG_MSG("hal_mmu_clone_space: Clone complete\n");
    
    return (hal_addr_space_t)new_l0_phys;
}

/**
 * @brief 创建新页表 (ARM64, 兼容旧接口)
 * @return 页表物理地址，失败返回 PADDR_INVALID
 * @deprecated 使用 hal_mmu_create_space() 代替
 */
paddr_t hal_mmu_create_page_table(void) {
    hal_addr_space_t space = hal_mmu_create_space();
    return (space == HAL_ADDR_SPACE_INVALID) ? PADDR_INVALID : space;
}

/**
 * @brief 销毁页表 (ARM64, 兼容旧接口)
 * @param page_table_phys 页表物理地址
 * @deprecated 使用 hal_mmu_destroy_space() 代替
 */
void hal_mmu_destroy_page_table(paddr_t page_table_phys) {
    hal_mmu_destroy_space((hal_addr_space_t)page_table_phys);
}


/* ============================================================================
 * ARM64 页错误处理
 * 
 * Requirements: 6.4
 * ========================================================================== */

/**
 * @brief ESR_EL1 异常类 (EC) 定义
 */
#define ESR_EC_SHIFT            26
#define ESR_EC_MASK             (0x3FULL << ESR_EC_SHIFT)
#define ESR_EC_UNKNOWN          0x00    /**< Unknown reason */
#define ESR_EC_SVC_A64          0x15    /**< SVC instruction (AArch64) */
#define ESR_EC_IABT_LOW         0x20    /**< Instruction Abort from lower EL */
#define ESR_EC_IABT_CUR         0x21    /**< Instruction Abort from current EL */
#define ESR_EC_PC_ALIGN         0x22    /**< PC alignment fault */
#define ESR_EC_DABT_LOW         0x24    /**< Data Abort from lower EL */
#define ESR_EC_DABT_CUR         0x25    /**< Data Abort from current EL */
#define ESR_EC_SP_ALIGN         0x26    /**< SP alignment fault */

/**
 * @brief ESR_EL1 指令/数据中止 ISS 字段定义
 */
#define ESR_ISS_MASK            0x01FFFFFFULL
#define ESR_ISS_DFSC_MASK       0x3F        /**< Data Fault Status Code */
#define ESR_ISS_WNR             (1ULL << 6) /**< Write not Read */
#define ESR_ISS_CM              (1ULL << 8) /**< Cache Maintenance */
#define ESR_ISS_EA              (1ULL << 9) /**< External Abort */
#define ESR_ISS_FNV             (1ULL << 10) /**< FAR not Valid */
#define ESR_ISS_SET_MASK        (3ULL << 11) /**< Synchronous Error Type */
#define ESR_ISS_VNCR            (1ULL << 13) /**< VNCR */
#define ESR_ISS_AR              (1ULL << 14) /**< Acquire/Release */
#define ESR_ISS_SF              (1ULL << 15) /**< Sixty-Four bit register */
#define ESR_ISS_SRT_MASK        (0x1FULL << 16) /**< Syndrome Register Transfer */
#define ESR_ISS_SSE             (1ULL << 21) /**< Syndrome Sign Extend */
#define ESR_ISS_SAS_MASK        (3ULL << 22) /**< Syndrome Access Size */
#define ESR_ISS_ISV             (1ULL << 24) /**< Instruction Syndrome Valid */

/**
 * @brief Data Fault Status Code (DFSC) 定义
 */
#define DFSC_ADDR_SIZE_L0       0x00    /**< Address size fault, level 0 */
#define DFSC_ADDR_SIZE_L1       0x01    /**< Address size fault, level 1 */
#define DFSC_ADDR_SIZE_L2       0x02    /**< Address size fault, level 2 */
#define DFSC_ADDR_SIZE_L3       0x03    /**< Address size fault, level 3 */
#define DFSC_TRANS_L0           0x04    /**< Translation fault, level 0 */
#define DFSC_TRANS_L1           0x05    /**< Translation fault, level 1 */
#define DFSC_TRANS_L2           0x06    /**< Translation fault, level 2 */
#define DFSC_TRANS_L3           0x07    /**< Translation fault, level 3 */
#define DFSC_ACCESS_L1          0x09    /**< Access flag fault, level 1 */
#define DFSC_ACCESS_L2          0x0A    /**< Access flag fault, level 2 */
#define DFSC_ACCESS_L3          0x0B    /**< Access flag fault, level 3 */
#define DFSC_PERM_L1            0x0D    /**< Permission fault, level 1 */
#define DFSC_PERM_L2            0x0E    /**< Permission fault, level 2 */
#define DFSC_PERM_L3            0x0F    /**< Permission fault, level 3 */
#define DFSC_SYNC_EXT           0x10    /**< Synchronous External abort */
#define DFSC_SYNC_EXT_L0        0x14    /**< Synchronous External abort, level 0 */
#define DFSC_SYNC_EXT_L1        0x15    /**< Synchronous External abort, level 1 */
#define DFSC_SYNC_EXT_L2        0x16    /**< Synchronous External abort, level 2 */
#define DFSC_SYNC_EXT_L3        0x17    /**< Synchronous External abort, level 3 */
#define DFSC_ALIGNMENT          0x21    /**< Alignment fault */
#define DFSC_TLB_CONFLICT       0x30    /**< TLB conflict abort */

/**
 * @brief 检查 DFSC 是否为翻译错误 (页不存在)
 * @param dfsc Data Fault Status Code
 * @return true 如果是翻译错误
 */
static bool is_translation_fault(uint32_t dfsc) {
    return (dfsc >= DFSC_TRANS_L0 && dfsc <= DFSC_TRANS_L3);
}

/**
 * @brief 检查 DFSC 是否为权限错误 (页存在但权限不足)
 * @param dfsc Data Fault Status Code
 * @return true 如果是权限错误
 */
static bool is_permission_fault(uint32_t dfsc) {
    return (dfsc >= DFSC_PERM_L1 && dfsc <= DFSC_PERM_L3);
}

/**
 * @brief 检查 DFSC 是否为访问标志错误
 * @param dfsc Data Fault Status Code
 * @return true 如果是访问标志错误
 */
static bool is_access_flag_fault(uint32_t dfsc) {
    return (dfsc >= DFSC_ACCESS_L1 && dfsc <= DFSC_ACCESS_L3);
}

/**
 * @brief 解析页错误信息 (ARM64)
 * 
 * 从 FAR_EL1 和 ESR_EL1 寄存器中提取页错误详细信息。
 * 
 * @param[out] info 页错误信息结构
 * 
 * @see Requirements 6.4
 */
void hal_mmu_parse_fault(hal_page_fault_info_t *info) {
    if (info == NULL) {
        return;
    }
    
    /* Get fault address from FAR_EL1 */
    info->fault_addr = (vaddr_t)read_far_el1();
    
    /* Get exception syndrome from ESR_EL1 */
    uint64_t esr = read_esr_el1();
    info->raw_error = (uint32_t)esr;
    
    /* Extract exception class */
    uint32_t ec = (esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    
    /* Extract ISS (Instruction Specific Syndrome) */
    uint32_t iss = esr & ESR_ISS_MASK;
    uint32_t dfsc = iss & ESR_ISS_DFSC_MASK;
    
    /* Determine fault type based on exception class */
    bool is_data_abort = (ec == ESR_EC_DABT_LOW || ec == ESR_EC_DABT_CUR);
    bool is_inst_abort = (ec == ESR_EC_IABT_LOW || ec == ESR_EC_IABT_CUR);
    
    /* is_present: true if page exists but permission denied */
    info->is_present = is_permission_fault(dfsc) || is_access_flag_fault(dfsc);
    
    /* is_write: true if write operation caused the fault */
    info->is_write = is_data_abort && ((iss & ESR_ISS_WNR) != 0);
    
    /* is_user: true if fault occurred from EL0 (user mode) */
    info->is_user = (ec == ESR_EC_DABT_LOW || ec == ESR_EC_IABT_LOW);
    
    /* is_exec: true if instruction fetch caused the fault */
    info->is_exec = is_inst_abort;
    
    /* is_reserved: not applicable on ARM64, set to false */
    info->is_reserved = false;
}

/**
 * @brief 使用 ESR 值解析页错误信息 (ARM64)
 * 
 * 此函数应由异常处理程序调用，传入保存的 ESR_EL1 值。
 * 
 * @param[out] info 页错误信息结构
 * @param esr ESR_EL1 寄存器值
 * 
 * @see Requirements 6.4
 */
void hal_mmu_parse_fault_with_esr(hal_page_fault_info_t *info, uint64_t esr) {
    if (info == NULL) {
        return;
    }
    
    /* Get fault address from FAR_EL1 */
    info->fault_addr = (vaddr_t)read_far_el1();
    info->raw_error = (uint32_t)esr;
    
    /* Extract exception class */
    uint32_t ec = (esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    
    /* Extract ISS */
    uint32_t iss = esr & ESR_ISS_MASK;
    uint32_t dfsc = iss & ESR_ISS_DFSC_MASK;
    
    /* Determine fault type */
    bool is_data_abort = (ec == ESR_EC_DABT_LOW || ec == ESR_EC_DABT_CUR);
    bool is_inst_abort = (ec == ESR_EC_IABT_LOW || ec == ESR_EC_IABT_CUR);
    
    info->is_present = is_permission_fault(dfsc) || is_access_flag_fault(dfsc);
    info->is_write = is_data_abort && ((iss & ESR_ISS_WNR) != 0);
    info->is_user = (ec == ESR_EC_DABT_LOW || ec == ESR_EC_IABT_LOW);
    info->is_exec = is_inst_abort;
    info->is_reserved = false;
}

/**
 * @brief 检查是否为 COW 页错误 (ARM64)
 * @param esr ESR_EL1 寄存器值
 * @return true 如果是 COW 页错误
 * 
 * COW 页错误特征：权限错误 + 写操作
 */
bool arm64_is_cow_fault(uint64_t esr) {
    uint32_t ec = (esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    uint32_t iss = esr & ESR_ISS_MASK;
    uint32_t dfsc = iss & ESR_ISS_DFSC_MASK;
    
    /* Must be a data abort */
    if (ec != ESR_EC_DABT_LOW && ec != ESR_EC_DABT_CUR) {
        return false;
    }
    
    /* Must be a permission fault */
    if (!is_permission_fault(dfsc)) {
        return false;
    }
    
    /* Must be a write operation */
    return (iss & ESR_ISS_WNR) != 0;
}

/* ============================================================================
 * ARM64 缓存维护操作
 * 
 * 用于 DMA 操作的缓存一致性维护。
 * ARM64 使用非一致性缓存，需要显式维护操作。
 * 
 * Requirements: 10.2
 * ========================================================================== */

/** @brief 缓存行大小 (ARM64 通常为 64 字节) */
#define CACHE_LINE_SIZE     64

/**
 * @brief 按虚拟地址清理缓存行到 PoC (Point of Coherency)
 * @param addr 虚拟地址
 * 
 * DC CVAC: Data Cache Clean by VA to PoC
 * 将脏数据写回到主存
 */
static inline void dc_cvac(uint64_t addr) {
    __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
}

/**
 * @brief 按虚拟地址使缓存行无效到 PoC
 * @param addr 虚拟地址
 * 
 * DC IVAC: Data Cache Invalidate by VA to PoC
 * 丢弃缓存中的数据，强制从主存重新读取
 */
static inline void dc_ivac(uint64_t addr) {
    __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
}

/**
 * @brief 按虚拟地址清理并使缓存行无效到 PoC
 * @param addr 虚拟地址
 * 
 * DC CIVAC: Data Cache Clean and Invalidate by VA to PoC
 * 先写回脏数据，然后使缓存行无效
 */
static inline void dc_civac(uint64_t addr) {
    __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
}

/**
 * @brief 清理缓存区域 (ARM64)
 * 
 * 将指定内存区域的脏缓存行写回到主存。
 * 用于 DMA 读操作前（设备从内存读取数据）。
 * 
 * @param addr 区域起始虚拟地址
 * @param size 区域大小（字节）
 * 
 * @see Requirements 10.2
 */
void hal_cache_clean(void *addr, size_t size) {
    if (addr == NULL || size == 0) {
        return;
    }
    
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + size;
    
    /* Align start down to cache line boundary */
    start &= ~(CACHE_LINE_SIZE - 1);
    
    /* Clean each cache line */
    dsb_sy();
    for (uint64_t line = start; line < end; line += CACHE_LINE_SIZE) {
        dc_cvac(line);
    }
    dsb_sy();
}

/**
 * @brief 使缓存区域无效 (ARM64)
 * 
 * 使指定内存区域的缓存行无效，强制后续读取从主存获取。
 * 用于 DMA 写操作后（设备向内存写入数据）。
 * 
 * @param addr 区域起始虚拟地址
 * @param size 区域大小（字节）
 * 
 * @warning 可能丢弃脏数据！如果区域可能包含修改过的数据，
 *          请使用 hal_cache_clean_invalidate()。
 * 
 * @see Requirements 10.2
 */
void hal_cache_invalidate(void *addr, size_t size) {
    if (addr == NULL || size == 0) {
        return;
    }
    
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + size;
    
    /* Align start down to cache line boundary */
    start &= ~(CACHE_LINE_SIZE - 1);
    
    /* Invalidate each cache line */
    dsb_sy();
    for (uint64_t line = start; line < end; line += CACHE_LINE_SIZE) {
        dc_ivac(line);
    }
    dsb_sy();
}

/**
 * @brief 清理并使缓存区域无效 (ARM64)
 * 
 * 先将脏数据写回主存，然后使缓存行无效。
 * 用于双向 DMA 缓冲区。
 * 
 * @param addr 区域起始虚拟地址
 * @param size 区域大小（字节）
 * 
 * @see Requirements 10.2
 */
void hal_cache_clean_invalidate(void *addr, size_t size) {
    if (addr == NULL || size == 0) {
        return;
    }
    
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + size;
    
    /* Align start down to cache line boundary */
    start &= ~(CACHE_LINE_SIZE - 1);
    
    /* Clean and invalidate each cache line */
    dsb_sy();
    for (uint64_t line = start; line < end; line += CACHE_LINE_SIZE) {
        dc_civac(line);
    }
    dsb_sy();
}


/**
 * @brief 获取页错误类型描述字符串 (ARM64)
 * @param esr ESR_EL1 寄存器值
 * @return 描述字符串
 */
const char* arm64_page_fault_type_str(uint64_t esr) {
    uint32_t ec = (esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    uint32_t iss = esr & ESR_ISS_MASK;
    uint32_t dfsc = iss & ESR_ISS_DFSC_MASK;
    bool is_write = (iss & ESR_ISS_WNR) != 0;
    bool is_user = (ec == ESR_EC_DABT_LOW || ec == ESR_EC_IABT_LOW);
    
    if (ec == ESR_EC_IABT_LOW || ec == ESR_EC_IABT_CUR) {
        if (is_translation_fault(dfsc)) {
            return is_user ? "User instruction fetch from unmapped page"
                           : "Kernel instruction fetch from unmapped page";
        } else if (is_permission_fault(dfsc)) {
            return is_user ? "User instruction fetch permission denied"
                           : "Kernel instruction fetch permission denied";
        }
        return "Instruction abort";
    }
    
    if (ec == ESR_EC_DABT_LOW || ec == ESR_EC_DABT_CUR) {
        if (is_translation_fault(dfsc)) {
            if (is_write) {
                return is_user ? "User write to unmapped page"
                               : "Kernel write to unmapped page";
            } else {
                return is_user ? "User read from unmapped page"
                               : "Kernel read from unmapped page";
            }
        } else if (is_permission_fault(dfsc)) {
            if (is_write) {
                return is_user ? "User write permission denied"
                               : "Kernel write permission denied";
            } else {
                return is_user ? "User read permission denied"
                               : "Kernel read permission denied";
            }
        } else if (is_access_flag_fault(dfsc)) {
            return "Access flag fault";
        } else if (dfsc == DFSC_ALIGNMENT) {
            return "Alignment fault";
        }
        return "Data abort";
    }
    
    return "Unknown fault";
}


/* ============================================================================
 * 大页映射实现 (2MB Blocks) - ARM64
 * 
 * ARM64 支持 2MB 块（通过 Level 2 块描述符）和 1GB 块（通过 Level 1 块描述符）
 * 此实现支持 2MB 块
 * 
 * @see Requirements 8.1, 8.2
 * ========================================================================== */

/** @brief 2MB 块大小 */
#define BLOCK_SIZE_2MB      (2 * 1024 * 1024)

/** @brief 2MB 块物理地址掩码 (bits 47:21) */
#define DESC_BLOCK_ADDR_MASK_2MB    0x0000FFFFFFE00000ULL

/**
 * @brief 检查是否支持大页 (ARM64)
 * @return true (ARM64 支持 2MB 块)
 */
bool hal_mmu_huge_pages_supported(void) {
    return true;
}

/**
 * @brief 检查地址是否 2MB 对齐
 */
static inline bool is_huge_page_aligned(uint64_t addr) {
    return (addr & (BLOCK_SIZE_2MB - 1)) == 0;
}

/**
 * @brief 映射 2MB 大页 (ARM64)
 * 
 * 在 Level 2 创建 2MB 块描述符
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
    
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    
    /* Convert HAL flags to ARM64 flags */
    uint64_t arm64_flags = hal_flags_to_arm64(flags);
    
    /* Table descriptor flags: valid + table type */
    uint64_t table_flags = DESC_VALID | DESC_TABLE;
    
    /* Level 0 -> Level 1 */
    uint64_t *l1;
    if (!desc_is_valid(l0[l0_idx])) {
        paddr_t l1_phys = alloc_page_table();
        if (l1_phys == PADDR_INVALID) {
            return false;
        }
        l0[l0_idx] = l1_phys | table_flags;
    } else if (!desc_is_table(l0[l0_idx])) {
        LOG_ERROR_MSG("hal_mmu_map_huge: L0 entry is not a table\n");
        return false;
    }
    l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0[l0_idx]));
    
    /* Level 1 -> Level 2 */
    uint64_t *l2;
    if (!desc_is_valid(l1[l1_idx])) {
        paddr_t l2_phys = alloc_page_table();
        if (l2_phys == PADDR_INVALID) {
            return false;
        }
        l1[l1_idx] = l2_phys | table_flags;
    } else if (desc_is_block(l1[l1_idx])) {
        LOG_ERROR_MSG("hal_mmu_map_huge: cannot map 2MB block over 1GB block\n");
        return false;
    }
    l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1[l1_idx]));
    
    /* Level 2: Block descriptor (2MB) */
    /* Check if there's already a L3 table at this location */
    if (desc_is_valid(l2[l2_idx]) && desc_is_table(l2[l2_idx])) {
        LOG_ERROR_MSG("hal_mmu_map_huge: cannot map 2MB block over existing L3 table\n");
        return false;
    }
    
    /* Create 2MB block descriptor */
    l2[l2_idx] = (phys & DESC_BLOCK_ADDR_MASK_2MB) | arm64_flags | DESC_TYPE_BLOCK;
    
    LOG_DEBUG_MSG("hal_mmu_map_huge: Mapped 2MB block virt=0x%llx -> phys=0x%llx\n",
                  (unsigned long long)virt, (unsigned long long)phys);
    
    return true;
}

/**
 * @brief 取消 2MB 大页映射 (ARM64)
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
    
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    
    /* Level 0 */
    uint64_t l0e = l0[l0_idx];
    if (!desc_is_valid(l0e) || !desc_is_table(l0e)) {
        return PADDR_INVALID;
    }
    
    /* Level 1 */
    uint64_t *l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0e));
    uint64_t l1e = l1[l1_idx];
    if (!desc_is_valid(l1e)) {
        return PADDR_INVALID;
    }
    
    /* Cannot unmap if this is a 1GB block */
    if (desc_is_block(l1e)) {
        LOG_ERROR_MSG("hal_mmu_unmap_huge: cannot unmap 1GB block with this function\n");
        return PADDR_INVALID;
    }
    
    if (!desc_is_table(l1e)) {
        return PADDR_INVALID;
    }
    
    /* Level 2 */
    uint64_t *l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1e));
    uint64_t l2e = l2[l2_idx];
    if (!desc_is_valid(l2e)) {
        return PADDR_INVALID;
    }
    
    /* Verify this is a 2MB block */
    if (!desc_is_block(l2e)) {
        LOG_ERROR_MSG("hal_mmu_unmap_huge: entry is not a 2MB block\n");
        return PADDR_INVALID;
    }
    
    /* Get physical address before clearing */
    paddr_t phys = l2e & DESC_BLOCK_ADDR_MASK_2MB;
    
    /* Clear the entry */
    l2[l2_idx] = 0;
    
    LOG_DEBUG_MSG("hal_mmu_unmap_huge: Unmapped 2MB block virt=0x%llx (was phys=0x%llx)\n",
                  (unsigned long long)virt, (unsigned long long)phys);
    
    return phys;
}

/**
 * @brief 查询映射是否为大页 (ARM64)
 * 
 * @param space 地址空间句柄
 * @param virt 虚拟地址
 * @return true 如果是 2MB 块映射
 * 
 * @see Requirements 8.3
 */
bool hal_mmu_is_huge_page(hal_addr_space_t space, vaddr_t virt) {
    uint64_t *l0 = get_l0_table(space);
    
    /* Get indices for each level */
    uint64_t l0_idx = l0_index((uint64_t)virt);
    uint64_t l1_idx = l1_index((uint64_t)virt);
    uint64_t l2_idx = l2_index((uint64_t)virt);
    
    /* Level 0 */
    uint64_t l0e = l0[l0_idx];
    if (!desc_is_valid(l0e) || !desc_is_table(l0e)) {
        return false;
    }
    
    /* Level 1 */
    uint64_t *l1 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l0e));
    uint64_t l1e = l1[l1_idx];
    if (!desc_is_valid(l1e)) {
        return false;
    }
    
    /* Check for 1GB block */
    if (desc_is_block(l1e)) {
        return true;  /* 1GB block */
    }
    
    if (!desc_is_table(l1e)) {
        return false;
    }
    
    /* Level 2 */
    uint64_t *l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1e));
    uint64_t l2e = l2[l2_idx];
    if (!desc_is_valid(l2e)) {
        return false;
    }
    
    /* Check for 2MB block */
    return desc_is_block(l2e);
}

