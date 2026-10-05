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
#include <hal/pt.h>
#include <mm/mm_types.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>

/* ============================================================================
 * ARM64 页表描述符定义
 * ========================================================================== */

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
#define DESC_AP_RW_EL1          (0ULL << 6)     /**< EL1 读写, EL0 无访问 */
#define DESC_AP_RW_ALL          (1ULL << 6)     /**< EL1/EL0 读写 */
#define DESC_AP_RO_EL1          (2ULL << 6)     /**< EL1 只读, EL0 无访问 */
#define DESC_AP_RO_ALL          (3ULL << 6)     /**< EL1/EL0 只读 */
#define DESC_AP_MASK            (3ULL << 6)
#define DESC_SH_INNER           (3ULL << 8)     /**< Inner Shareable */
#define DESC_AF                 (1ULL << 10)    /**< Access Flag */
#define DESC_NG                 (1ULL << 11)    /**< Not Global */

#define DESC_PXN                (1ULL << 53)    /**< Privileged Execute Never */
#define DESC_UXN                (1ULL << 54)    /**< User Execute Never */
#define DESC_DIRTY              (1ULL << 55)    /**< Dirty (software) */
#define DESC_COW                (1ULL << 56)    /**< COW flag (software) */
#define DESC_SHARED             (1ULL << 57)    /**< Shared mapping: not made COW on fork (software) */

/** 物理地址掩码 (bits 47:12 for 4KB pages) */
#define DESC_ADDR_MASK          0x0000FFFFFFFFF000ULL


/** MAIR 索引定义 */
#define MAIR_IDX_DEVICE_nGnRnE  0   /**< Device-nGnRnE (strongly ordered) */
#define MAIR_IDX_NORMAL_NC      1   /**< Normal Non-Cacheable */
#define MAIR_IDX_NORMAL_WB      3   /**< Normal Write-Back (default) */


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
 * ARM64 页错误处理
 * 
 * ========================================================================== */

/**
 * @brief ESR_EL1 异常类 (EC) 定义
 */
#define ESR_EC_SHIFT            26
#define ESR_EC_MASK             (0x3FULL << ESR_EC_SHIFT)
#define ESR_EC_DABT_LOW         0x24    /**< Data Abort from lower EL */
#define ESR_EC_DABT_CUR         0x25    /**< Data Abort from current EL */

/**
 * @brief ESR_EL1 指令/数据中止 ISS 字段定义
 */
#define ESR_ISS_MASK            0x01FFFFFFULL
#define ESR_ISS_DFSC_MASK       0x3F        /**< Data Fault Status Code */
#define ESR_ISS_WNR             (1ULL << 6) /**< Write not Read */

/**
 * @brief Data Fault Status Code (DFSC) 定义
 */
#define DFSC_PERM_L1            0x0D    /**< Permission fault, level 1 */
#define DFSC_PERM_L3            0x0F    /**< Permission fault, level 3 */

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

/**
 * @brief 让内核的直接映射区覆盖全部物理内存
 *
 * 引导代码只映射了内核附近的一小段。这里用 2MB 块把其余的物理内存映射到
 * KERNEL_VIRTUAL_BASE 之上（PMM 清零新分配的帧时要通过这个区域访问它们）。
 */
void hal::Mmu::map_physical_memory() {
    
    // 获取 PMM 信息以确定需要映射的物理内存范围
    mm::PmmInfo pmm_info = mm::Pmm::get_info();
    uint64_t max_phys = (uint64_t)pmm_info.total_frames * PAGE_SIZE;
    
    LOG_INFO_MSG("VMM: Physical memory: %llu MB (%llu frames)\n", 
                 (unsigned long long)(max_phys / (1024*1024)),
                 (unsigned long long)pmm_info.total_frames);
    
    // ARM64 内核直接映射区：0xFFFF_0000_0000_0000 开始
    // 引导代码已经映射了基本的内核区域，这里扩展映射以覆盖所有物理内存
    // 使用 2MB 块映射提高效率
    LOG_INFO_MSG("VMM: Extending kernel direct mapping using 2MB blocks...\n");
    
    // 计算需要映射的 2MB 块数量
    uint64_t block_size = 2 * 1024 * 1024;  // 2MB
    uint64_t num_blocks = (max_phys + block_size - 1) / block_size;
    uint32_t mapped_blocks = 0;
    
    for (uint64_t i = 0; i < num_blocks; i++) {
        uint64_t phys = i * block_size;
        vaddr_t virt = (vaddr_t)(KERNEL_VIRTUAL_BASE + phys);
        
        // 检查是否已经映射（引导代码可能已经映射了部分区域）
        paddr_t existing_phys;
        if (hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, virt, &existing_phys, NULL)) {
            // 已映射，跳过
            continue;
        }
        
        // 使用 2MB 块映射
        uint32_t hal_flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_EXEC;
        if (hal::Mmu::map_huge(HAL_ADDR_SPACE_CURRENT, virt, (paddr_t)phys, hal_flags)) {
            mapped_blocks++;
        } else {
            // 如果 2MB 块映射失败，尝试使用 4KB 页映射
            LOG_WARN_MSG("VMM: 2MB block mapping failed at 0x%llx, falling back to 4KB pages\n",
                        (unsigned long long)virt);
            for (uint64_t offset = 0; offset < block_size; offset += PAGE_SIZE) {
                vaddr_t page_virt = virt + offset;
                paddr_t page_phys = phys + offset;
                if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, page_virt, NULL, NULL)) {
                    hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, page_virt, page_phys, hal_flags);
                }
            }
        }
    }
    
    // 刷新 TLB
    hal::Mmu::flush_tlb_all();
    
    LOG_INFO_MSG("VMM: Extended mapping by %u 2MB blocks (total %llu MB)\n", 
                 mapped_blocks, (unsigned long long)(num_blocks * 2));
    LOG_INFO_MSG("VMM: ARM64 VMM initialization complete\n");
}

/**
 * @brief 内核地址缺页时同步内核映射：这个架构不需要
 *
 * 内核半区用单独的 TTBR1 页表，所有地址空间共享，没有可同步的东西。
 */
bool hal::Mmu::sync_kernel_mapping(vaddr_t addr) {
    (void)addr;
    return false;
}

/* ============================================================================
 * 页表格式（通用页表代码 src/mm/pagetable.cpp 用，说明见 <hal/pt.h>）
 * ========================================================================== */

bool pt::valid_vaddr(vaddr_t virt) {
    (void)virt;
    return true;
}

pte_t *pt::root(hal_addr_space_t space, vaddr_t virt) {
    return get_l0_table(space, virt);
}

bool pt::present(pte_t entry) {
    return desc_is_valid(entry);
}

bool pt::is_leaf(pte_t entry, int level) {
    /* 上面几级：bit 1 为 0 的有效描述符是块映射 */
    return level == 0 || !desc_is_table(entry);
}

paddr_t pt::addr(pte_t entry) {
    return desc_get_addr(entry);
}

pte_t pt::make_table(paddr_t table_phys, uint32_t hal_flags) {
    (void)hal_flags;
    return table_phys | DESC_VALID | DESC_TABLE;
}

pte_t pt::table_for_user(pte_t entry) {
    return entry;       /* 表描述符不限制权限 */
}

pte_t pt::make_leaf(paddr_t phys, uint32_t hal_flags) {
    return phys | hal_flags_to_arm64(hal_flags) | DESC_TYPE_PAGE;
}

uint32_t pt::leaf_flags(pte_t entry) {
    return arm64_flags_to_hal(entry);
}

pte_t pt::apply_delta(pte_t entry, uint32_t set_flags, uint32_t clear_flags) {
    return desc_apply_flag_delta(entry, set_flags, clear_flags);
}

bool pt::kernel_only_leaf(pte_t entry) {
    /* 用户页表里只有内核能访问的映射不属于某个进程：克隆时改成只读 + COW 会让内核
     * 自己写不进去，所以原样共享，也不参与引用计数 */
    return !desc_is_user(entry);
}

void pt::init_root(pte_t *new_root) {
    (void)new_root;     /* 内核地址走 TTBR1 指向的另一张表，用户的顶层表里没有内核的份 */
}

void pt::top_entry_created(pte_t *root, uint32_t index) {
    (void)root;
    (void)index;
}
