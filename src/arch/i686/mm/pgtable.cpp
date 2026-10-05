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
 * @brief 从页表项提取物理地址 (i686)
 * 
 * @param entry 页表项
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry) {
    return (paddr_t)(entry & I686_PTE_ADDR_MASK);
}

/**
 * @brief 检查页表项是否存在 (i686)
 */
bool pgtable_is_present(pte_t entry) {
    return (entry & I686_PTE_PRESENT) != 0;
}

/* ============================================================================
 * 虚拟地址索引提取函数实现
 * ========================================================================== */

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

