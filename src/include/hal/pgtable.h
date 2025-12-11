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
 * 
 * @see Requirements 3.1, 3.2, 3.3, 3.4
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
 * @see Requirements 3.1
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
 * @see Requirements 3.1, 3.2
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
 * @see Requirements 3.1, 3.2
 * ========================================================================== */

/**
 * @brief 创建页表项
 * 
 * 将物理地址和架构无关的标志组合成架构特定格式的页表项。
 * 
 * @param phys 物理地址（必须页对齐）
 * @param flags 架构无关的页表项标志 (pte_flags_t 的组合)
 * @return 架构特定格式的页表项
 * 
 * @note 物理地址必须页对齐（4KB 边界）
 * @note 对于不支持的标志（如 i686 的 NX 位），实现应忽略该标志
 * 
 * @see Requirements 3.1
 */
pte_t pgtable_make_entry(paddr_t phys, uint32_t flags);

/**
 * @brief 从页表项提取物理地址
 * 
 * 从架构特定格式的页表项中提取物理地址。
 * 
 * @param entry 页表项
 * @return 物理地址
 * 
 * @note 如果页表项无效（不存在），返回值未定义
 * 
 * @see Requirements 3.2
 */
paddr_t pgtable_get_phys(pte_t entry);

/**
 * @brief 从页表项提取标志
 * 
 * 从架构特定格式的页表项中提取标志，并转换为架构无关的格式。
 * 
 * @param entry 页表项
 * @return 架构无关的标志 (pte_flags_t 的组合)
 * 
 * @see Requirements 3.2
 */
uint32_t pgtable_get_flags(pte_t entry);

/**
 * @brief 检查页表项是否存在
 * 
 * @param entry 页表项
 * @return true 如果页存在，false 如果不存在
 */
bool pgtable_is_present(pte_t entry);

/**
 * @brief 检查页表项是否可写
 * 
 * @param entry 页表项
 * @return true 如果页可写，false 如果只读
 */
bool pgtable_is_writable(pte_t entry);

/**
 * @brief 检查页表项是否用户可访问
 * 
 * @param entry 页表项
 * @return true 如果用户可访问，false 如果仅内核可访问
 */
bool pgtable_is_user(pte_t entry);

/**
 * @brief 检查页表项是否为 COW 页
 * 
 * @param entry 页表项
 * @return true 如果是 COW 页，false 如果不是
 */
bool pgtable_is_cow(pte_t entry);

/**
 * @brief 检查页表项是否为大页
 * 
 * @param entry 页表项
 * @return true 如果是大页（2MB 或 1GB），false 如果是普通 4KB 页
 */
bool pgtable_is_huge(pte_t entry);

/**
 * @brief 检查页表项是否可执行
 * 
 * @param entry 页表项
 * @return true 如果可执行，false 如果不可执行
 * 
 * @note 在 i686 上（无 NX 位），总是返回 true
 */
bool pgtable_is_executable(pte_t entry);

/**
 * @brief 修改页表项标志
 * 
 * 在现有页表项上设置或清除指定的标志，不改变物理地址。
 * 
 * @param entry 原页表项
 * @param set_flags 要设置的标志 (pte_flags_t 的组合)
 * @param clear_flags 要清除的标志 (pte_flags_t 的组合)
 * @return 修改后的页表项
 * 
 * @note 如果同一标志同时出现在 set_flags 和 clear_flags 中，
 *       行为是先清除后设置（即最终会被设置）
 * 
 * @see Requirements 3.2
 */
pte_t pgtable_modify_flags(pte_t entry, uint32_t set_flags, uint32_t clear_flags);

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
 * 页表配置查询函数
 * 
 * 这些函数返回当前架构的页表配置信息。
 * 
 * @see Requirements 3.3
 * ========================================================================== */

/**
 * @brief 获取页表级数
 * 
 * @return 页表级数：
 *         - i686: 2 (PD -> PT)
 *         - x86_64: 4 (PML4 -> PDPT -> PD -> PT)
 *         - ARM64: 4 (L0 -> L1 -> L2 -> L3)
 */
uint32_t pgtable_get_levels(void);

/**
 * @brief 获取每级页表的条目数
 * 
 * @return 每级页表的条目数：
 *         - i686: 1024
 *         - x86_64: 512
 *         - ARM64: 512
 */
uint32_t pgtable_get_entries_per_level(void);

/**
 * @brief 获取页表项大小（字节）
 * 
 * @return 页表项大小：
 *         - i686: 4
 *         - x86_64: 8
 *         - ARM64: 8
 */
uint32_t pgtable_get_entry_size(void);

/**
 * @brief 检查当前架构是否支持 NX（不可执行）位
 * 
 * @return true 如果支持 NX 位，false 如果不支持
 *         - i686: false（标准模式不支持）
 *         - x86_64: true
 *         - ARM64: true
 */
bool pgtable_supports_nx(void);

/**
 * @brief 检查当前架构是否支持大页
 * 
 * @return true 如果支持 2MB 大页，false 如果不支持
 *         - i686: false（此实现不支持）
 *         - x86_64: true
 *         - ARM64: true
 */
bool pgtable_supports_huge_pages(void);

/* ============================================================================
 * 虚拟地址索引提取函数
 * 
 * 这些函数从虚拟地址中提取各级页表的索引。
 * 
 * @see Requirements 3.3
 * ========================================================================== */

/**
 * @brief 从虚拟地址提取顶级页表索引
 * 
 * @param virt 虚拟地址
 * @return 顶级页表索引：
 *         - i686: PD 索引 (bits 31:22)
 *         - x86_64: PML4 索引 (bits 47:39)
 *         - ARM64: L0 索引 (bits 47:39)
 */
uint32_t pgtable_get_top_index(vaddr_t virt);

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

/* ============================================================================
 * 调试和验证函数
 * ========================================================================== */

/**
 * @brief 验证页表项格式
 * 
 * 检查页表项是否符合当前架构的格式要求。
 * 
 * @param entry 页表项
 * @return true 如果格式正确，false 如果格式错误
 * 
 * @note 此函数主要用于调试和测试
 */
bool pgtable_validate_entry(pte_t entry);

/**
 * @brief 获取页表项的字符串描述
 * 
 * 生成页表项的人类可读描述，用于调试输出。
 * 
 * @param entry 页表项
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 写入的字符数（不包括终止符）
 */
int pgtable_entry_to_string(pte_t entry, char *buf, size_t buf_size);

#endif /* _HAL_PGTABLE_H_ */
