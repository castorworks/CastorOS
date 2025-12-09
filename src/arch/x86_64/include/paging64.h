/**
 * @file paging64.h
 * @brief x86_64 架构特定的分页定义
 * 
 * 定义 x86_64 (AMD64/Intel 64-bit) 的 4 级页表结构和相关常量
 * 
 * Requirements: 5.2, 12.1
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
#define PTE64_WRITE_THROUGH (1ULL << 3)   /**< 写穿透 */
#define PTE64_CACHE_DISABLE (1ULL << 4)   /**< 禁用缓存 */
#define PTE64_ACCESSED      (1ULL << 5)   /**< 已访问 */
#define PTE64_DIRTY         (1ULL << 6)   /**< 已修改 */
#define PTE64_HUGE          (1ULL << 7)   /**< 大页 (2MB/1GB) */
#define PTE64_GLOBAL        (1ULL << 8)   /**< 全局页 */
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

/**
 * @brief PD (Page Directory) 结构
 * 
 * 第三级页表，包含 512 个 PDE，每个指向一个 PT 或 2MB 大页
 */
typedef struct {
    pde64_t entries[PTE64_ENTRIES];
} __attribute__((aligned(PAGE_SIZE))) pd64_t;

/**
 * @brief PT (Page Table) 结构
 * 
 * 第四级页表，包含 512 个 PTE，每个映射一个 4KB 页
 */
typedef struct {
    pte64_t entries[PTE64_ENTRIES];
} __attribute__((aligned(PAGE_SIZE))) pt64_t;

/* ============================================================================
 * 地址分解宏
 * ========================================================================== */

/** @brief 获取 PML4 索引 (bits 47:39) */
#define PML4_INDEX(virt)    (((uint64_t)(virt) >> 39) & 0x1FF)

/** @brief 获取 PDPT 索引 (bits 38:30) */
#define PDPT_INDEX(virt)    (((uint64_t)(virt) >> 30) & 0x1FF)

/** @brief 获取 PD 索引 (bits 29:21) */
#define PD_INDEX(virt)      (((uint64_t)(virt) >> 21) & 0x1FF)

/** @brief 获取 PT 索引 (bits 20:12) */
#define PT_INDEX(virt)      (((uint64_t)(virt) >> 12) & 0x1FF)

/** @brief 获取页内偏移 (bits 11:0) */
#define PAGE_OFFSET(virt)   ((uint64_t)(virt) & 0xFFF)

/* ============================================================================
 * 页表项操作宏
 * ========================================================================== */

/** @brief 从页表项中提取物理地址 */
#define PTE64_GET_FRAME(entry)      ((entry) & PTE64_ADDR_MASK)

/** @brief 检查页表项是否存在 */
#define PTE64_IS_PRESENT(entry)     (((entry) & PTE64_PRESENT) != 0)

/** @brief 检查是否为大页 */
#define PTE64_IS_HUGE(entry)        (((entry) & PTE64_HUGE) != 0)

/** @brief 检查是否可写 */
#define PTE64_IS_WRITABLE(entry)    (((entry) & PTE64_WRITE) != 0)

/** @brief 检查是否为用户页 */
#define PTE64_IS_USER(entry)        (((entry) & PTE64_USER) != 0)

/** @brief 检查是否为 COW 页 */
#define PTE64_IS_COW(entry)         (((entry) & PTE64_COW) != 0)

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
 * @brief 验证 x86_64 页表项格式
 * @param entry 页表项
 * @return true 如果格式正确
 */
bool x86_64_validate_pte_format(pte64_t entry);

/**
 * @brief 获取 x86_64 页表级数
 * @return 4 (x86_64 使用 4 级页表)
 */
uint32_t x86_64_get_page_table_levels(void);

/**
 * @brief 获取 x86_64 页大小
 * @return 4096 (4KB)
 */
uint32_t x86_64_get_page_size(void);

/**
 * @brief 获取 x86_64 内核虚拟基址
 * @return 0xFFFF800000000000 (高半核)
 */
uint64_t x86_64_get_kernel_virtual_base(void);

/**
 * @brief 检查虚拟地址是否为规范地址 (canonical)
 * @param virt 虚拟地址
 * @return true 如果是规范地址
 */
bool x86_64_is_canonical_address(uint64_t virt);

/**
 * @brief 检查地址是否在内核空间
 * @param virt 虚拟地址
 * @return true 如果在内核空间
 */
bool x86_64_is_kernel_address(uint64_t virt);

/**
 * @brief 检查地址是否在用户空间
 * @param virt 虚拟地址
 * @return true 如果在用户空间
 */
bool x86_64_is_user_address(uint64_t virt);

/**
 * @brief 解析 x86_64 页错误错误码
 * @param error_code 错误码
 * @return 解析后的页错误信息
 */
x86_64_page_fault_info_t x86_64_parse_page_fault_error(uint64_t error_code);

/**
 * @brief 检查是否为 COW 页错误
 * @param error_code 错误码
 * @return true 如果是 COW 页错误
 */
bool x86_64_is_cow_fault(uint64_t error_code);

/**
 * @brief 获取页错误类型描述字符串
 * @param error_code 错误码
 * @return 描述字符串
 */
const char* x86_64_page_fault_type_str(uint64_t error_code);

#endif /* _ARCH_X86_64_PAGING64_H_ */
