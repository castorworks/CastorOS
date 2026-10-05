/**
 * @file pgtable.h
 * @brief HAL 页表抽象层接口
 * 
 * 提供架构无关的页表操作函数，隐藏不同架构的页表格式差异。
 * 支持 i686 (2级), x86_64 (4级), ARM64 (4级) 页表结构。
 * 
 * 此接口允许 VMM 代码使用统一的函数调用进行页表操作，
 * 而无需使用条件编译来处理架构差异。
 * 
 * 注意：此头文件使用 mm/pgtable.h 中定义的 pte_t 类型。
 */

#ifndef _HAL_PGTABLE_H_
#define _HAL_PGTABLE_H_

#include <types.h>
#include <mm/mm_types.h>
#include <mm/pgtable.h>
#include <hal/hal_error.h>

/* ============================================================================
 * 页表项类型
 * 
 * 使用 mm/pgtable.h 中定义的 pte_t 类型。
 * ========================================================================== */

/* pte_t is defined in mm/pgtable.h:
 *   - i686: uint32_t
 *   - x86_64/ARM64: uint64_t
 */

/* ============================================================================
 * 架构无关的页表项标志位
 * 
 * 这些标志位在所有架构上具有相同的语义。
 * pgtable_make_entry() 会将这些标志转换为架构特定的格式。
 * 
 * ========================================================================== */

/**
 * @brief 页表项标志枚举 (架构无关)
 */
typedef enum pte_flags {
    PTE_PRESENT     = (1 << 0),     /**< 页存在标志 */
    PTE_WRITE       = (1 << 1),     /**< 页可写标志 */
    PTE_USER        = (1 << 2),     /**< 用户模式可访问标志 */
    PTE_NOCACHE     = (1 << 3),     /**< 禁用缓存标志 */
    PTE_EXEC        = (1 << 4),     /**< 可执行标志 */
    PTE_COW         = (1 << 5),     /**< Copy-on-Write 标志 */
    PTE_DIRTY       = (1 << 6),     /**< 脏页标志 */
    PTE_ACCESSED    = (1 << 7),     /**< 已访问标志 */
    PTE_HUGE        = (1 << 8),     /**< 大页标志 (2MB/1GB) */
    PTE_GLOBAL      = (1 << 9),     /**< 全局页标志 */
} pte_flags_t;

/* ============================================================================
 * 页表项操作函数
 * 
 * 这些函数提供架构无关的页表项创建和解析功能。
 * 每个架构必须实现这些函数。
 * 
 * ========================================================================== */

/**
 * @brief 从页表项提取物理地址
 * 
 * 从架构特定格式的页表项中提取物理地址。
 * 
 * @param entry 页表项
 * @return 物理地址
 * 
 * @note 如果页表项无效（不存在），返回值未定义
 */
paddr_t pgtable_get_phys(pte_t entry);

/**
 * @brief 检查页表项是否存在
 * 
 * @param entry 页表项
 * @return true 如果页存在，false 如果不存在
 */
bool pgtable_is_present(pte_t entry);

/**
 * @brief 清空页表项
 * 
 * 创建一个无效的（不存在的）页表项。
 * 
 * @return 无效的页表项（值为 0）
 */
static inline pte_t pgtable_clear_entry(void) {
    return 0;
}

/* ============================================================================
 * 虚拟地址索引提取函数
 * 
 * 这些函数从虚拟地址中提取各级页表的索引。
 * 
 * ========================================================================== */

/**
 * @brief 从虚拟地址提取指定级别的页表索引
 * 
 * @param virt 虚拟地址
 * @param level 页表级别（0 = 最低级/PT，数字越大级别越高）
 * @return 指定级别的页表索引
 * 
 * @note 级别编号：
 *       - i686: 0=PT, 1=PD
 *       - x86_64: 0=PT, 1=PD, 2=PDPT, 3=PML4
 *       - ARM64: 0=L3, 1=L2, 2=L1, 3=L0
 */
uint32_t pgtable_get_index(vaddr_t virt, uint32_t level);

/**
 * @brief 从虚拟地址提取页内偏移
 * 
 * @param virt 虚拟地址
 * @return 页内偏移 (bits 11:0)，范围 0-4095
 */
static inline uint32_t pgtable_get_page_offset(vaddr_t virt) {
    return (uint32_t)(virt & 0xFFF);
}

#endif /* _HAL_PGTABLE_H_ */
