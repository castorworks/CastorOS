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
 * @brief 从页表项提取物理地址 (ARM64)
 * 
 * @param entry 页描述符
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry) {
    return (paddr_t)(entry & ARM64_DESC_ADDR_MASK);
}

/**
 * @brief 检查页表项是否存在 (ARM64)
 */
bool pgtable_is_present(pte_t entry) {
    return (entry & ARM64_DESC_VALID) != 0;
}

