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
 * @brief 从页表项提取物理地址 (x86_64)
 * 
 * @param entry 页表项
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry) {
    return (paddr_t)(entry & X64_PTE_ADDR_MASK);
}

/**
 * @brief 检查页表项是否存在 (x86_64)
 */
bool pgtable_is_present(pte_t entry) {
    return (entry & X64_PTE_PRESENT) != 0;
}

