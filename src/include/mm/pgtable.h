/**
 * @file pgtable.h
 * @brief 页表抽象层
 * 
 * 提供架构无关的页表操作宏，隐藏不同架构的页表级数差异。
 * 支持 i686 (2级), x86_64 (4级), ARM64 (4级) 页表结构。
 * 
 * @see Requirements 3.1, 3.3, 3.4
 */

#ifndef _MM_PGTABLE_H_
#define _MM_PGTABLE_H_

#include <mm/mm_types.h>

/*============================================================================
 * 页表项类型（架构相关）
 * @see Requirements 3.1
 *============================================================================*/

#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
    /** @brief 页表项类型 (64-bit) */
    typedef uint64_t pte_t;
    /** @brief 页目录项类型 (64-bit) */
    typedef uint64_t pde_t;
    /** @brief 物理地址掩码 (bits 12-51 for 4KB pages) */
    #define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL
    /** @brief 标志位掩码 */
    #define PTE_FLAGS_MASK  0xFFF0000000000FFFULL
#else /* i686 */
    /** @brief 页表项类型 (32-bit) */
    typedef uint32_t pte_t;
    /** @brief 页目录项类型 (32-bit) */
    typedef uint32_t pde_t;
    /** @brief 物理地址掩码 (bits 12-31) */
    #define PTE_ADDR_MASK   0xFFFFF000UL
    /** @brief 标志位掩码 */
    #define PTE_FLAGS_MASK  0x00000FFFUL
#endif

/*============================================================================
 * 通用页表项标志位（架构无关）
 * 
 * 这些标志位在所有架构上具有相同的语义，但实际位位置可能不同。
 * 使用 MAKE_PTE 宏时会自动转换为架构特定的标志位。
 *============================================================================*/

/** @brief 页存在标志 */
#define PTE_FLAG_PRESENT    (1 << 0)
/** @brief 页可写标志 */
#define PTE_FLAG_WRITE      (1 << 1)
/** @brief 用户模式可访问标志 */
#define PTE_FLAG_USER       (1 << 2)
/** @brief Write-Through 标志 */
#define PTE_FLAG_PWT        (1 << 3)
/** @brief 禁用缓存标志 */
#define PTE_FLAG_PCD        (1 << 4)
/** @brief 已访问标志 */
#define PTE_FLAG_ACCESSED   (1 << 5)
/** @brief 脏页标志 */
#define PTE_FLAG_DIRTY      (1 << 6)
/** @brief 大页标志 (用于 PDE) */
#define PTE_FLAG_HUGE       (1 << 7)
/** @brief 全局页标志 */
#define PTE_FLAG_GLOBAL     (1 << 8)
/** @brief COW 标志 (使用 Available bit) */
#define PTE_FLAG_COW        (1 << 9)

#if defined(ARCH_X86_64)
/** @brief 不可执行标志 (NX bit, x86_64 only) */
#define PTE_FLAG_NX         (1ULL << 63)
#elif defined(ARCH_ARM64)
/** @brief 不可执行标志 (ARM64 UXN/PXN) */
#define PTE_FLAG_NX         (1ULL << 54)
#else
/** @brief 不可执行标志 (i686 不支持，定义为 0) */
#define PTE_FLAG_NX         0
#endif

/*============================================================================
 * 页表项操作宏
 * @see Requirements 3.3, 3.4
 *============================================================================*/

/**
 * @brief 从页表项提取物理地址
 * @param pte 页表项
 * @return 物理地址
 * 
 * @see Requirements 3.4
 */
#define PTE_ADDR(pte)       ((paddr_t)((pte) & PTE_ADDR_MASK))

/**
 * @brief 从页表项提取标志位
 * @param pte 页表项
 * @return 标志位
 * 
 * @see Requirements 3.4
 */
#define PTE_FLAGS(pte)      ((uint32_t)((pte) & PTE_FLAGS_MASK))

/**
 * @brief 构造页表项
 * @param addr 物理地址（必须页对齐）
 * @param flags 标志位
 * @return 页表项
 * 
 * @see Requirements 3.3
 */
#define MAKE_PTE(addr, flags) \
    ((pte_t)(((paddr_t)(addr) & PTE_ADDR_MASK) | ((pte_t)(flags) & PTE_FLAGS_MASK)))

/**
 * @brief 检查页表项是否存在
 * @param pte 页表项
 * @return true 如果页存在
 */
#define PTE_PRESENT(pte)    (((pte) & PTE_FLAG_PRESENT) != 0)

/**
 * @brief 检查页表项是否可写
 * @param pte 页表项
 * @return true 如果页可写
 */
#define PTE_WRITABLE(pte)   (((pte) & PTE_FLAG_WRITE) != 0)

/**
 * @brief 检查页表项是否用户可访问
 * @param pte 页表项
 * @return true 如果用户可访问
 */
#define PTE_USER(pte)       (((pte) & PTE_FLAG_USER) != 0)

/**
 * @brief 检查页表项是否为 COW 页
 * @param pte 页表项
 * @return true 如果是 COW 页
 */
#define PTE_IS_COW(pte)     (((pte) & PTE_FLAG_COW) != 0)

/**
 * @brief 检查页表项是否为大页
 * @param pte 页表项
 * @return true 如果是大页
 */
#define PTE_IS_HUGE(pte)    (((pte) & PTE_FLAG_HUGE) != 0)

/**
 * @brief 检查页表项是否已访问
 * @param pte 页表项
 * @return true 如果已访问
 */
#define PTE_ACCESSED(pte)   (((pte) & PTE_FLAG_ACCESSED) != 0)

/**
 * @brief 检查页表项是否为脏页
 * @param pte 页表项
 * @return true 如果是脏页
 */
#define PTE_DIRTY(pte)      (((pte) & PTE_FLAG_DIRTY) != 0)

/**
 * @brief 设置页表项标志位
 * @param pte 页表项指针
 * @param flags 要设置的标志位
 */
#define PTE_SET_FLAGS(pte, flags)   (*(pte) |= (flags))

/**
 * @brief 清除页表项标志位
 * @param pte 页表项指针
 * @param flags 要清除的标志位
 */
#define PTE_CLEAR_FLAGS(pte, flags) (*(pte) &= ~(flags))

/**
 * @brief 清空页表项
 * @param pte 页表项指针
 */
#define PTE_CLEAR(pte)      (*(pte) = 0)

/*============================================================================
 * 虚拟地址分解宏（架构相关）
 * @see Requirements 3.5
 *============================================================================*/

#if defined(ARCH_X86_64)
/*----------------------------------------------------------------------------
 * x86_64: 4 级页表，每级 9 位索引
 * 
 * 虚拟地址格式 (48-bit canonical):
 *   [63:48] - Sign extension (must match bit 47)
 *   [47:39] - PML4 index (9 bits, 512 entries)
 *   [38:30] - PDPT index (9 bits, 512 entries)
 *   [29:21] - PD index (9 bits, 512 entries)
 *   [20:12] - PT index (9 bits, 512 entries)
 *   [11:0]  - Page offset (12 bits, 4KB)
 *---------------------------------------------------------------------------*/

/** @brief PML4 索引位移 */
#define VA_PML4_SHIFT   39
/** @brief PDPT 索引位移 */
#define VA_PDPT_SHIFT   30
/** @brief PD 索引位移 */
#define VA_PD_SHIFT     21
/** @brief PT 索引位移 */
#define VA_PT_SHIFT     12
/** @brief 索引掩码 (9 bits) */
#define VA_INDEX_MASK   0x1FFULL

/** @brief 获取 PML4 索引 */
#define VA_PML4_INDEX(va)   (((vaddr_t)(va) >> VA_PML4_SHIFT) & VA_INDEX_MASK)
/** @brief 获取 PDPT 索引 */
#define VA_PDPT_INDEX(va)   (((vaddr_t)(va) >> VA_PDPT_SHIFT) & VA_INDEX_MASK)
/** @brief 获取 PD 索引 */
#define VA_PD_INDEX(va)     (((vaddr_t)(va) >> VA_PD_SHIFT) & VA_INDEX_MASK)
/** @brief 获取 PT 索引 */
#define VA_PT_INDEX(va)     (((vaddr_t)(va) >> VA_PT_SHIFT) & VA_INDEX_MASK)
/** @brief 获取页内偏移 */
#define VA_PAGE_OFFSET(va)  ((vaddr_t)(va) & 0xFFFULL)

/** @brief 页表项数量 */
#define PGTABLE_ENTRIES     512

#elif defined(ARCH_ARM64)
/*----------------------------------------------------------------------------
 * ARM64: 4 级页表，4KB granule
 * 
 * 虚拟地址格式 (48-bit):
 *   [63:48] - TTBR selector (0=TTBR0, 1=TTBR1)
 *   [47:39] - Level 0 index (9 bits, 512 entries)
 *   [38:30] - Level 1 index (9 bits, 512 entries)
 *   [29:21] - Level 2 index (9 bits, 512 entries)
 *   [20:12] - Level 3 index (9 bits, 512 entries)
 *   [11:0]  - Page offset (12 bits, 4KB)
 *---------------------------------------------------------------------------*/

/** @brief Level 0 索引位移 */
#define VA_L0_SHIFT     39
/** @brief Level 1 索引位移 */
#define VA_L1_SHIFT     30
/** @brief Level 2 索引位移 */
#define VA_L2_SHIFT     21
/** @brief Level 3 索引位移 */
#define VA_L3_SHIFT     12
/** @brief 索引掩码 (9 bits) */
#define VA_INDEX_MASK   0x1FFULL

/** @brief 获取 Level 0 索引 */
#define VA_L0_INDEX(va)     (((vaddr_t)(va) >> VA_L0_SHIFT) & VA_INDEX_MASK)
/** @brief 获取 Level 1 索引 */
#define VA_L1_INDEX(va)     (((vaddr_t)(va) >> VA_L1_SHIFT) & VA_INDEX_MASK)
/** @brief 获取 Level 2 索引 */
#define VA_L2_INDEX(va)     (((vaddr_t)(va) >> VA_L2_SHIFT) & VA_INDEX_MASK)
/** @brief 获取 Level 3 索引 */
#define VA_L3_INDEX(va)     (((vaddr_t)(va) >> VA_L3_SHIFT) & VA_INDEX_MASK)
/** @brief 获取页内偏移 */
#define VA_PAGE_OFFSET(va)  ((vaddr_t)(va) & 0xFFFULL)

/* 为了与 x86_64 兼容，提供别名 */
#define VA_PML4_INDEX(va)   VA_L0_INDEX(va)
#define VA_PDPT_INDEX(va)   VA_L1_INDEX(va)
#define VA_PD_INDEX(va)     VA_L2_INDEX(va)
#define VA_PT_INDEX(va)     VA_L3_INDEX(va)

/** @brief 页表项数量 */
#define PGTABLE_ENTRIES     512

#else /* i686 */
/*----------------------------------------------------------------------------
 * i686: 2 级页表，每级 10 位索引
 * 
 * 虚拟地址格式 (32-bit):
 *   [31:22] - PD index (10 bits, 1024 entries)
 *   [21:12] - PT index (10 bits, 1024 entries)
 *   [11:0]  - Page offset (12 bits, 4KB)
 *---------------------------------------------------------------------------*/

/** @brief PD 索引位移 */
#define VA_PD_SHIFT     22
/** @brief PT 索引位移 */
#define VA_PT_SHIFT     12
/** @brief PD 索引掩码 (10 bits) */
#define VA_PD_MASK      0x3FFUL
/** @brief PT 索引掩码 (10 bits) */
#define VA_PT_MASK      0x3FFUL

/** @brief 获取 PD 索引 */
#define VA_PD_INDEX(va)     (((vaddr_t)(va) >> VA_PD_SHIFT) & VA_PD_MASK)
/** @brief 获取 PT 索引 */
#define VA_PT_INDEX(va)     (((vaddr_t)(va) >> VA_PT_SHIFT) & VA_PT_MASK)
/** @brief 获取页内偏移 */
#define VA_PAGE_OFFSET(va)  ((vaddr_t)(va) & 0xFFFUL)

/** @brief 页目录项数量 */
#define PD_ENTRIES          1024
/** @brief 页表项数量 */
#define PT_ENTRIES          1024
/** @brief 页表项数量（通用名称） */
#define PGTABLE_ENTRIES     1024

#endif /* Architecture selection */

/*============================================================================
 * 页表遍历回调
 *============================================================================*/

/**
 * @brief 页表遍历回调函数类型
 * @param virt 虚拟地址
 * @param pte 页表项指针
 * @param level 页表级别（0=最低级/PT，数字越大级别越高）
 * @param data 用户数据
 * @return 0 继续遍历，非 0 停止
 */
typedef int (*pgtable_walk_fn)(vaddr_t virt, pte_t *pte, int level, void *data);

/*============================================================================
 * 辅助宏
 *============================================================================*/

/**
 * @brief 计算覆盖指定地址范围所需的页数
 * @param start 起始地址
 * @param size 大小（字节）
 * @return 页数
 */
#define PAGES_FOR_RANGE(start, size) \
    ((VADDR_ALIGN_UP((start) + (size)) - VADDR_ALIGN_DOWN(start)) / PAGE_SIZE)

/**
 * @brief 检查地址是否在内核空间
 * @param va 虚拟地址
 * @return true 如果在内核空间
 */
#define IS_KERNEL_ADDR(va)  ((vaddr_t)(va) >= KERNEL_VIRTUAL_BASE)

/**
 * @brief 检查地址是否在用户空间
 * @param va 虚拟地址
 * @return true 如果在用户空间
 */
#define IS_USER_ADDR(va)    ((vaddr_t)(va) < KERNEL_VIRTUAL_BASE)

#endif /* _MM_PGTABLE_H_ */
