/**
 * @file mm_types.h
 * @brief 内存管理类型定义
 * 
 * 提供类型安全的物理/虚拟地址类型，防止地址空间混淆。
 * 所有架构统一使用 64-bit 物理地址类型，虚拟地址类型与架构指针大小匹配。
 * 
 * @see Requirements 1.1, 1.2, 1.4, 1.5
 */

#ifndef _MM_MM_TYPES_H_
#define _MM_MM_TYPES_H_

#include <types.h>

/*============================================================================
 * 物理地址类型 - 所有架构统一使用 64-bit
 *============================================================================*/

/** 
 * @brief 物理地址类型 (64-bit on all architectures)
 * 
 * 使用 64-bit 类型确保在 x86_64 和 ARM64 上可以寻址完整的物理地址空间。
 * 在 i686 上，高 32 位始终为 0。
 * 
 * @see Requirements 1.1
 */
typedef uint64_t paddr_t;

/** 
 * @brief 页帧号类型 (physical page index)
 * 
 * 表示物理页帧的索引号，等于物理地址右移 PAGE_SHIFT 位。
 * 
 * @see Requirements 1.4
 */
typedef uint64_t pfn_t;

/** @brief 无效物理地址标记 */
#define PADDR_INVALID   ((paddr_t)-1)

/** @brief 无效页帧号标记 */
#define PFN_INVALID     ((pfn_t)-1)

/*============================================================================
 * 虚拟地址类型 - 架构相关
 *============================================================================*/

/** 
 * @brief 虚拟地址类型 (matches pointer size)
 * 
 * 使用 uintptr_t 确保与架构指针大小匹配：
 * - i686: 32-bit
 * - x86_64/ARM64: 64-bit
 * 
 * @see Requirements 1.2
 */
typedef uintptr_t vaddr_t;

/** @brief 无效虚拟地址标记 */
#define VADDR_INVALID   ((vaddr_t)-1)

/*============================================================================
 * 地址转换宏
 * @see Requirements 1.5
 *============================================================================*/

/** 
 * @brief 物理地址转页帧号
 * @param pa 物理地址
 * @return 页帧号
 */
#define PADDR_TO_PFN(pa)    ((pfn_t)((pa) >> PAGE_SHIFT))

/** 
 * @brief 页帧号转物理地址
 * @param pfn 页帧号
 * @return 物理地址
 */
#define PFN_TO_PADDR(pfn)   ((paddr_t)((pfn) << PAGE_SHIFT))

/** 
 * @brief 物理地址转内核虚拟地址（仅限直接映射区域）
 * @param pa 物理地址
 * @return 内核虚拟地址
 * 
 * @warning 仅适用于内核直接映射区域，不适用于动态分配的内存
 */
#define PADDR_TO_KVADDR(pa) ((vaddr_t)((pa) + KERNEL_VIRTUAL_BASE))

/** 
 * @brief 内核虚拟地址转物理地址（仅限直接映射区域）
 * @param va 内核虚拟地址
 * @return 物理地址
 * 
 * @warning 仅适用于内核直接映射区域，不适用于动态分配的内存
 */
#define KVADDR_TO_PADDR(va) ((paddr_t)((va) - KERNEL_VIRTUAL_BASE))

/** 
 * @brief 物理地址页对齐（向下）
 * @param pa 物理地址
 * @return 页对齐后的物理地址
 */
#define PADDR_ALIGN_DOWN(pa) ((pa) & ~((paddr_t)PAGE_SIZE - 1))

/** 
 * @brief 物理地址页对齐（向上）
 * @param pa 物理地址
 * @return 页对齐后的物理地址
 */
#define PADDR_ALIGN_UP(pa)   (((pa) + PAGE_SIZE - 1) & ~((paddr_t)PAGE_SIZE - 1))

/** 
 * @brief 虚拟地址页对齐（向下）
 * @param va 虚拟地址
 * @return 页对齐后的虚拟地址
 */
#define VADDR_ALIGN_DOWN(va) ((va) & ~((vaddr_t)PAGE_SIZE - 1))

/** 
 * @brief 虚拟地址页对齐（向上）
 * @param va 虚拟地址
 * @return 页对齐后的虚拟地址
 */
#define VADDR_ALIGN_UP(va)   (((va) + PAGE_SIZE - 1) & ~((vaddr_t)PAGE_SIZE - 1))

/** 
 * @brief 检查物理地址是否页对齐
 * @param pa 物理地址
 * @return true 如果页对齐
 */
#define IS_PADDR_ALIGNED(pa) (((pa) & ((paddr_t)PAGE_SIZE - 1)) == 0)

/** 
 * @brief 检查虚拟地址是否页对齐
 * @param va 虚拟地址
 * @return true 如果页对齐
 */
#define IS_VADDR_ALIGNED(va) (((va) & ((vaddr_t)PAGE_SIZE - 1)) == 0)

/*============================================================================
 * 架构相关常量
 *============================================================================*/

#if defined(ARCH_X86_64)
    /** @brief x86_64 物理地址位数 (最大 4PB) */
    #define PHYS_ADDR_BITS      52
    /** @brief x86_64 虚拟地址位数 (canonical 48-bit) */
    #define VIRT_ADDR_BITS      48
    /** @brief x86_64 页表级数 (PML4 -> PDPT -> PD -> PT) */
    #define PAGE_TABLE_LEVELS   4
#elif defined(ARCH_ARM64)
    /** @brief ARM64 物理地址位数 */
    #define PHYS_ADDR_BITS      48
    /** @brief ARM64 虚拟地址位数 */
    #define VIRT_ADDR_BITS      48
    /** @brief ARM64 页表级数 (L0 -> L1 -> L2 -> L3) */
    #define PAGE_TABLE_LEVELS   4
#else /* i686 */
    /** @brief i686 物理地址位数 (最大 4GB) */
    #define PHYS_ADDR_BITS      32
    /** @brief i686 虚拟地址位数 */
    #define VIRT_ADDR_BITS      32
    /** @brief i686 页表级数 (PD -> PT) */
    #define PAGE_TABLE_LEVELS   2
#endif

/** @brief 最大物理地址 */
#define PHYS_ADDR_MAX   (((paddr_t)1 << PHYS_ADDR_BITS) - 1)

/** @brief 最大虚拟地址 */
#define VIRT_ADDR_MAX   (((vaddr_t)1 << VIRT_ADDR_BITS) - 1)

/** @brief 最大页帧号 */
#define PFN_MAX         (((pfn_t)1 << (PHYS_ADDR_BITS - PAGE_SHIFT)) - 1)

#endif /* _MM_MM_TYPES_H_ */
