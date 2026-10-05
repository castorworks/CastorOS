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
#define DESC_SHARED             (1ULL << 57)    /**< Shared mapping: not made COW on fork (software) */

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

/**
 * @brief 页/块描述符是否允许 EL0（用户态）访问
 *
 * AP[1]（bit 6）置位表示 EL0 可访问。只有用户页才参与 COW 和引用计数；
 * 低半区里仅内核可访问的映射（例如引导阶段的恒等映射）在克隆时原样共享。
 */
static inline bool desc_is_user(uint64_t desc) {
    return (desc & (1ULL << 6)) != 0;
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

/** @brief 读取 MAIR_EL1 (Memory Attribute Indirection Register) */
static inline uint64_t read_mair_el1(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(val));
    return val;
}

/* ============================================================================
 * TLB 和屏障操作
 * ========================================================================== */

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
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目 (ARM64)
 * @param virt 虚拟地址
 */
void hal::Mmu::flush_tlb(vaddr_t virt) {
    dsb_ish();
    tlbi_vaae1is((uint64_t)virt);
    dsb_ish();
    isb();
}

/**
 * @brief 刷新整个 TLB (ARM64)
 */
void hal::Mmu::flush_tlb_all() {
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
 */
void hal::Mmu::switch_space(paddr_t space) {
    dsb_ish();
    write_ttbr0_el1((uint64_t)space);
    isb();
    /* ASIDs are not used, so every address space shares ASID 0: entries cached
     * from the previous TTBR0 must be dropped here or the new space would keep
     * executing the old one's pages (x86 gets this for free from the CR3 load). */
    tlbi_vmalle1is();
    dsb_ish();
    isb();
}

/**
 * @brief 获取当前用户空间页表物理地址 (ARM64)
 * @return TTBR0_EL1 寄存器的值 (物理地址部分)
 */
paddr_t hal::Mmu::get_current_page_table() {
    /* TTBR0_EL1 bits [47:1] contain the physical address */
    return (paddr_t)(read_ttbr0_el1() & 0x0000FFFFFFFFFFFCULL);
}

/**
 * @brief 获取当前地址空间 (ARM64)
 * @return 当前 Level 0 表的物理地址
 */
hal_addr_space_t hal::Mmu::current_space() {
    return (hal_addr_space_t)hal::Mmu::get_current_page_table();
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


/* ============================================================================
 * 页表操作辅助函数
 * ========================================================================== */

/**
 * @brief 获取翻译 virt 所用的 Level 0 表的虚拟地址
 *
 * 高半区地址（内核）由 TTBR1 的页表翻译，这张表所有地址空间共用，与 space 无关；
 * 低半区地址（用户）由 space 自己的页表翻译。
 *
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @param virt  要翻译的虚拟地址
 * @return Level 0 表的虚拟地址
 */
static uint64_t* get_l0_table(hal_addr_space_t space, vaddr_t virt) {
    paddr_t l0_phys;
    
    if ((uint64_t)virt >= KERNEL_VIRTUAL_BASE) {
        l0_phys = (paddr_t)(read_ttbr1_el1() & DESC_ADDR_MASK);
    } else if (space == HAL_ADDR_SPACE_CURRENT || space == 0) {
        l0_phys = hal::Mmu::get_current_page_table();
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
    paddr_t frame = mm::Pmm::alloc_frame();
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
    if (hal_flags & HAL_PAGE_SHARED) {
        arm64_flags |= DESC_SHARED;
    }
    
    /* Non-global for user pages */
    if (hal_flags & HAL_PAGE_USER) {
        arm64_flags |= DESC_NG;
    }
    
    return arm64_flags;
}

/**
 * @brief 在现有描述符上应用 HAL 标志的增量（protect 用）
 *
 * 只改动 set/clear 中提到的属性，其余位（AF、AttrIndx、SH、nG、地址）保持不变。
 * 不能用 hal_flags_to_arm64() 编码增量：它编码的是完整描述符，
 * 会把 AF/AttrIndx/SH 等也算进要清除的位里。
 */
static uint64_t desc_apply_flag_delta(uint64_t desc, uint32_t set_flags, uint32_t clear_flags) {
    /* AP[2] (bit 7) = 只读，AP[1] (bit 6) = EL0 可访问 */
    if (set_flags & HAL_PAGE_WRITE)   desc &= ~(1ULL << 7);
    if (clear_flags & HAL_PAGE_WRITE) desc |= (1ULL << 7);
    if (set_flags & HAL_PAGE_USER)    desc |= (1ULL << 6) | DESC_NG;
    if (clear_flags & HAL_PAGE_USER)  desc &= ~((1ULL << 6) | DESC_NG);

    if (set_flags & HAL_PAGE_EXEC)    desc &= ~(DESC_UXN | DESC_PXN);
    if (clear_flags & HAL_PAGE_EXEC)  desc |= (DESC_UXN | DESC_PXN);

    if (set_flags & HAL_PAGE_COW)     desc |= DESC_COW;
    if (clear_flags & HAL_PAGE_COW)   desc &= ~DESC_COW;

    if (set_flags & HAL_PAGE_SHARED)   desc |= DESC_SHARED;
    if (clear_flags & HAL_PAGE_SHARED) desc &= ~DESC_SHARED;

    if (set_flags & HAL_PAGE_DIRTY)   desc |= DESC_DIRTY;
    if (clear_flags & HAL_PAGE_DIRTY) desc &= ~DESC_DIRTY;

    if (set_flags & HAL_PAGE_ACCESSED)   desc |= DESC_AF;
    if (clear_flags & HAL_PAGE_ACCESSED) desc &= ~DESC_AF;

    return desc;
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
    if (arm64_flags & DESC_SHARED) {
        hal_flags |= HAL_PAGE_SHARED;
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
 */
bool hal::Mmu::query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags) {
    uint64_t *l0 = get_l0_table(space, virt);
    
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
 * @note 调用者需要在映射后调用 hal::Mmu::flush_tlb()
 */
bool hal::Mmu::map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    /* Validate addresses */
    if (!IS_VADDR_ALIGNED(virt) || !IS_PADDR_ALIGNED(phys)) {
        LOG_ERROR_MSG("hal::Mmu::map: addresses not page-aligned\n");
        return false;
    }
    
    uint64_t *l0 = get_l0_table(space, virt);
    
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
        LOG_ERROR_MSG("hal::Mmu::map: L0 entry is not a table\n");
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
        LOG_ERROR_MSG("hal::Mmu::map: cannot map 4KB page over 1GB block\n");
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
        LOG_ERROR_MSG("hal::Mmu::map: cannot map 4KB page over 2MB block\n");
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
 * @note 调用者需要在取消映射后调用 hal::Mmu::flush_tlb()
 * @note 此函数不释放中间页表级别
 */
paddr_t hal::Mmu::unmap(hal_addr_space_t space, vaddr_t virt) {
    uint64_t *l0 = get_l0_table(space, virt);
    
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
        LOG_ERROR_MSG("hal::Mmu::unmap: cannot unmap 1GB block\n");
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
        LOG_ERROR_MSG("hal::Mmu::unmap: cannot unmap 2MB block\n");
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
 * @note 调用者需要在修改后调用 hal::Mmu::flush_tlb()
 */
bool hal::Mmu::protect(hal_addr_space_t space, vaddr_t virt, 
                     uint32_t set_flags, uint32_t clear_flags) {
    uint64_t *l0 = get_l0_table(space, virt);
    
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
        l1[l1_idx] = desc_apply_flag_delta(l1e, set_flags, clear_flags);
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
        l2[l2_idx] = desc_apply_flag_delta(l2e, set_flags, clear_flags);
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
    
    *l3e = desc_apply_flag_delta(*l3e, set_flags, clear_flags);
    
    return true;
}


/* ============================================================================
 * ARM64 地址空间管理实现
 * 
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
 * IMPORTANT: The kernel currently runs at physical addresses (0x40xxxxxx) which
 * are in the TTBR0 region. We must copy L0[0] (which contains the identity mapping
 * for 0x00000000-0x7FFFFFFFFF) to ensure the kernel code remains accessible after
 * switching TTBR0.
 * 
 * @return 新地址空间句柄 (L0 表物理地址)，失败返回 HAL_ADDR_SPACE_INVALID
 */
hal_addr_space_t hal::Mmu::create_space() {
    /* Allocate a new Level 0 table */
    paddr_t l0_phys = alloc_page_table();
    if (l0_phys == PADDR_INVALID) {
        LOG_ERROR_MSG("hal::Mmu::create_space: Failed to allocate L0 table\n");
        return HAL_ADDR_SPACE_INVALID;
    }
    
    uint64_t *new_l0 = (uint64_t*)PADDR_TO_KVADDR(l0_phys);
    
    /*
     * 用户地址空间（TTBR0）里只有用户映射，新建时整张表为空。内核和设备寄存器
     * 都在高半区，由 TTBR1 的页表翻译，所有进程共用，和这张表无关：
     * 整个低半区（包括 0x40000000 以上）都留给用户程序。
     */
    memset(new_l0, 0, PAGE_SIZE);

    LOG_DEBUG_MSG("hal::Mmu::create_space: Created new L0 table at phys 0x%llx\n", 
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
        
        if ((level == 1 || desc_is_block(entry)) && !desc_is_user(entry)) {
            /* Kernel-only mapping: shared, not reference counted (see clone) */
            continue;
        }
        
        if (level == 1) {
            /* Level 3 (L3): entries point to physical pages.
             * free_frame() drops one reference and returns the frame to the
             * allocator when it was the last one (frame_ref_dec() only
             * decrements the counter and would leak the frame). */
            mm::Pmm::free_frame(frame);
        } else if (desc_is_block(entry)) {
            /* Block descriptor (1GB or 2MB) - decrement refcount */
            uint32_t refcount = mm::Pmm::frame_get_refcount(frame);
            if (refcount > 0) {
                mm::Pmm::frame_ref_dec(frame);
            }
        } else if (desc_is_table(entry)) {
            /* Table descriptor, recurse into child table */
            free_page_table_recursive(frame, level - 1);
        }
    }
    
    /* Free this page table itself */
    mm::Pmm::free_frame(table_phys);
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
 */
void hal::Mmu::destroy_space(hal_addr_space_t space) {
    if (space == HAL_ADDR_SPACE_INVALID || space == 0) {
        return;
    }
    
    /* Don't destroy current address space */
    hal_addr_space_t current = hal::Mmu::current_space();
    if (space == current) {
        LOG_ERROR_MSG("hal::Mmu::destroy_space: Cannot destroy current address space\n");
        return;
    }
    
    uint64_t *l0 = (uint64_t*)PADDR_TO_KVADDR(space);
    
    LOG_DEBUG_MSG("hal::Mmu::destroy_space: Destroying address space at phys 0x%llx\n",
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
    mm::Pmm::free_frame(space);
    
    LOG_DEBUG_MSG("hal::Mmu::destroy_space: Address space destroyed\n");
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
        
        if ((level == 1 || desc_is_block(entry)) && !desc_is_user(entry)) {
            /* Kernel-only mapping: share it unchanged. Making it read-only + COW
             * would take write access away from the kernel itself. */
            dst_table[i] = entry;
            continue;
        }
        
        if (level == 1) {
            /* Level 3 (L3): entries point to physical pages */
            /* Apply COW semantics: mark writable pages as read-only + COW */
            /* Shared mappings stay as they are: both sides keep the same frame. */
            uint64_t ap = flags & DESC_AP_MASK;
            if ((ap == DESC_AP_RW_ALL || ap == DESC_AP_RW_EL1) && !(flags & DESC_SHARED)) {
                /* Change to read-only */
                flags &= ~DESC_AP_MASK;
                flags |= (ap == DESC_AP_RW_ALL) ? DESC_AP_RO_ALL : DESC_AP_RO_EL1;
                flags |= DESC_COW;  /* Mark as COW */
                
                /* Update source entry as well (both parent and child are COW) */
                src_table[i] = frame | flags;
            }
            
            /* Increment reference count for shared physical page */
            mm::Pmm::frame_ref_share(frame);
            
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
            
            mm::Pmm::frame_ref_inc(frame);
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
                            mm::Pmm::frame_ref_dec(child_phys);
                        }
                    }
                }
                mm::Pmm::free_frame(new_table_phys);
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
 */
hal_addr_space_t hal::Mmu::clone_space(hal_addr_space_t src) {
    /* Validate source address space */
    if (src == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    
    /* Get source L0 */
    paddr_t src_phys = (src == HAL_ADDR_SPACE_CURRENT || src == 0) 
                       ? hal::Mmu::get_current_page_table() 
                       : src;
    
    /* Allocate new L0 */
    paddr_t new_l0_phys = alloc_page_table();
    if (new_l0_phys == PADDR_INVALID) {
        LOG_ERROR_MSG("hal::Mmu::clone_space: Failed to allocate L0 table\n");
        return HAL_ADDR_SPACE_INVALID;
    }
    
    uint64_t *src_l0 = (uint64_t*)PADDR_TO_KVADDR(src_phys);
    uint64_t *new_l0 = (uint64_t*)PADDR_TO_KVADDR(new_l0_phys);
    
    LOG_DEBUG_MSG("hal::Mmu::clone_space: Cloning address space from 0x%llx to 0x%llx\n",
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
            LOG_ERROR_MSG("hal::Mmu::clone_space: Failed to clone L1 at index %u\n", i);
            
            /* Clean up already cloned entries */
            for (uint32_t j = USER_L0_START; j < i; j++) {
                if (desc_is_valid(new_l0[j]) && desc_is_table(new_l0[j])) {
                    paddr_t l1_phys = desc_get_addr(new_l0[j]);
                    free_page_table_recursive(l1_phys, 3);
                }
            }
            mm::Pmm::free_frame(new_l0_phys);
            return HAL_ADDR_SPACE_INVALID;
        }
        
        new_l0[i] = new_l1_phys | l0e_flags;
    }
    
    /* Copy kernel space entries (L0[256..511]) - shared, not cloned */
    for (uint32_t i = KERNEL_L0_START; i < KERNEL_L0_END; i++) {
        new_l0[i] = src_l0[i];
    }
    
    /* Flush TLB for source address space (we modified COW flags) */
    if (src_phys == hal::Mmu::get_current_page_table()) {
        hal::Mmu::flush_tlb_all();
    }
    
    LOG_DEBUG_MSG("hal::Mmu::clone_space: Clone complete\n");
    
    return (hal_addr_space_t)new_l0_phys;
}


/* ============================================================================
 * ARM64 页错误处理
 * 
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
 * @brief 检查 DFSC 是否为权限错误 (页存在但权限不足)
 * @param dfsc Data Fault Status Code
 * @return true 如果是权限错误
 */
static bool is_permission_fault(uint32_t dfsc) {
    return (dfsc >= DFSC_PERM_L1 && dfsc <= DFSC_PERM_L3);
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
 * ========================================================================== */

/** @brief 缓存行大小 (ARM64 通常为 64 字节) */
#define CACHE_LINE_SIZE     64


/* ============================================================================
 * 大页映射实现 (2MB Blocks) - ARM64
 * 
 * ARM64 支持 2MB 块（通过 Level 2 块描述符）和 1GB 块（通过 Level 1 块描述符）
 * 此实现支持 2MB 块
 * 
 * ========================================================================== */

/** @brief 2MB 块大小 */
#define BLOCK_SIZE_2MB      (2 * 1024 * 1024)

/** @brief 2MB 块物理地址掩码 (bits 47:21) */
#define DESC_BLOCK_ADDR_MASK_2MB    0x0000FFFFFFE00000ULL

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
 */
bool hal::Mmu::map_huge(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    /* Validate 2MB alignment */
    if (!is_huge_page_aligned((uint64_t)virt) || !is_huge_page_aligned((uint64_t)phys)) {
        LOG_ERROR_MSG("hal::Mmu::map_huge: addresses not 2MB-aligned (virt=0x%llx, phys=0x%llx)\n",
                      (unsigned long long)virt, (unsigned long long)phys);
        return false;
    }
    
    uint64_t *l0 = get_l0_table(space, virt);
    
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
        LOG_ERROR_MSG("hal::Mmu::map_huge: L0 entry is not a table\n");
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
        LOG_ERROR_MSG("hal::Mmu::map_huge: cannot map 2MB block over 1GB block\n");
        return false;
    }
    l2 = (uint64_t*)PADDR_TO_KVADDR(desc_get_addr(l1[l1_idx]));
    
    /* Level 2: Block descriptor (2MB) */
    /* Check if there's already a L3 table at this location */
    if (desc_is_valid(l2[l2_idx]) && desc_is_table(l2[l2_idx])) {
        LOG_ERROR_MSG("hal::Mmu::map_huge: cannot map 2MB block over existing L3 table\n");
        return false;
    }
    
    /* Create 2MB block descriptor */
    l2[l2_idx] = (phys & DESC_BLOCK_ADDR_MASK_2MB) | arm64_flags | DESC_TYPE_BLOCK;
    
    LOG_DEBUG_MSG("hal::Mmu::map_huge: Mapped 2MB block virt=0x%llx -> phys=0x%llx\n",
                  (unsigned long long)virt, (unsigned long long)phys);
    
    return true;
}

