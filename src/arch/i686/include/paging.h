/**
 * @file paging.h
 * @brief i686 架构特定的分页定义
 * 
 * 定义 i686 (x86 32-bit) 的页表结构和操作
 * 
 * Requirements: 5.2, 12.1, 12.2
 */

#ifndef _ARCH_I686_PAGING_H_
#define _ARCH_I686_PAGING_H_

#include <types.h>

/* ============================================================================
 * i686 页表常量
 * ========================================================================== */

/** @brief i686 页表级数 */
#define I686_PAGE_TABLE_LEVELS  2

/** @brief 页目录项数量 */
#define I686_PDE_COUNT          1024

/** @brief 页表项数量 */
#define I686_PTE_COUNT          1024

/** @brief 每个 PDE 覆盖的地址范围 (4MB) */
#define I686_PDE_COVERAGE       (4 * 1024 * 1024)

/* ============================================================================
 * i686 页表项标志位
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

/** @brief PAT 标志 (用于页表项) */
#define I686_PTE_PAT            (1 << 7)

/** @brief 全局页标志 */
#define I686_PTE_GLOBAL         (1 << 8)

/** @brief COW 标志 (使用 Available bit 9) */
#define I686_PTE_COW            (1 << 9)

/* ============================================================================
 * i686 HAL MMU 函数声明
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目
 * @param virt 虚拟地址
 */
void hal_mmu_flush_tlb(uintptr_t virt);

/**
 * @brief 刷新整个 TLB
 */
void hal_mmu_flush_tlb_all(void);

/**
 * @brief 切换地址空间
 * @param page_table_phys 新页目录的物理地址
 */
void hal_mmu_switch_space(uintptr_t page_table_phys);

/**
 * @brief 获取页错误地址
 * @return CR2 寄存器中的错误地址
 */
uintptr_t hal_mmu_get_fault_addr(void);

/**
 * @brief 获取当前页目录物理地址
 * @return CR3 寄存器的值
 */
uintptr_t hal_mmu_get_current_page_table(void);

/**
 * @brief 启用分页
 */
void hal_mmu_enable_paging(void);

/**
 * @brief 检查分页是否启用
 * @return true 如果分页已启用
 */
bool hal_mmu_is_paging_enabled(void);

/* ============================================================================
 * i686 页表格式验证函数
 * ========================================================================== */

/**
 * @brief 验证 i686 页表项格式
 * @param entry 页表项
 * @return true 如果格式正确
 */
bool i686_validate_pte_format(uint32_t entry);

/**
 * @brief 验证 i686 页目录项格式
 * @param entry 页目录项
 * @return true 如果格式正确
 */
bool i686_validate_pde_format(uint32_t entry);

/**
 * @brief 获取 i686 页表级数
 * @return 2
 */
uint32_t i686_get_page_table_levels(void);

/**
 * @brief 获取 i686 页大小
 * @return 4096
 */
uint32_t i686_get_page_size(void);

/**
 * @brief 获取 i686 内核虚拟基址
 * @return 0x80000000
 */
uintptr_t i686_get_kernel_virtual_base(void);

#endif /* _ARCH_I686_PAGING_H_ */
