/**
 * @file paging64.h
 * @brief x86_64 架构特定的分页定义
 * 
 * 定义 x86_64 (AMD64/Intel 64-bit) 的 4 级页表结构和相关常量
 */

#ifndef _ARCH_X86_64_PAGING64_H_
#define _ARCH_X86_64_PAGING64_H_

#include <types.h>

/* ============================================================================
 * 页表项类型定义
 * ========================================================================== */

/** 页表项类型 (64-bit) */
typedef uint64_t pte64_t;
typedef uint64_t pde64_t;
typedef uint64_t pdpte64_t;
typedef uint64_t pml4e64_t;

/* ============================================================================
 * 页表项标志位
 * ========================================================================== */

#define PTE64_PRESENT       (1ULL << 0)   /**< 页存在 */
#define PTE64_WRITE         (1ULL << 1)   /**< 可写 */
#define PTE64_USER          (1ULL << 2)   /**< 用户可访问 */
#define PTE64_CACHE_DISABLE (1ULL << 4)   /**< 禁用缓存 */
#define PTE64_ACCESSED      (1ULL << 5)   /**< 已访问 */
#define PTE64_DIRTY         (1ULL << 6)   /**< 已修改 */
#define PTE64_HUGE          (1ULL << 7)   /**< 大页 (2MB/1GB) */
#define PTE64_COW           (1ULL << 9)   /**< COW 标志 (Available bit) */
#define PTE64_NX            (1ULL << 63)  /**< 不可执行 */

/** 物理地址掩码 (bits 12-51 for 4KB pages) */
#define PTE64_ADDR_MASK     0x000FFFFFFFFFF000ULL

/** 页表项数量 */
#define PTE64_ENTRIES       512

/* ============================================================================
 * 页表结构定义
 * ========================================================================== */

/**
 * @brief PML4 (Page Map Level 4) 结构
 * 
 * 顶级页表，包含 512 个 PML4E，每个指向一个 PDPT
 */
typedef struct {
    pml4e64_t entries[PTE64_ENTRIES];
} __attribute__((aligned(PAGE_SIZE))) pml4_t;

/**
 * @brief PDPT (Page Directory Pointer Table) 结构
 * 
 * 第二级页表，包含 512 个 PDPTE，每个指向一个 PD 或 1GB 大页
 */
typedef struct {
    pdpte64_t entries[PTE64_ENTRIES];
} __attribute__((aligned(PAGE_SIZE))) pdpt_t;


/* ============================================================================
 * 页错误信息结构
 * ========================================================================== */

/**
 * @brief x86_64 页错误信息结构
 */
typedef struct {
    bool present;       /**< 页是否存在 (P=1 表示保护违规) */
    bool write;         /**< 是否为写操作 */
    bool user;          /**< 是否为用户模式 */
    bool reserved;      /**< 是否为保留位错误 */
    bool instruction;   /**< 是否为指令获取 */
    bool pk;            /**< 是否为保护密钥违规 */
    bool ss;            /**< 是否为影子栈访问 */
    bool sgx;           /**< 是否为 SGX 违规 */
} x86_64_page_fault_info_t;

/* ============================================================================
 * 函数声明
 * ========================================================================== */

/**
 * @brief 检查虚拟地址是否为规范地址 (canonical)
 * @param virt 虚拟地址
 * @return true 如果是规范地址
 */
bool x86_64_is_canonical_address(uint64_t virt);

#endif /* _ARCH_X86_64_PAGING64_H_ */
