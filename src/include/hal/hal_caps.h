/**
 * @file hal_caps.h
 * @brief HAL 能力查询接口
 * 
 * 提供运行时查询当前架构硬件能力的接口，使内核代码能够
 * 根据实际硬件特性调整行为，而无需使用条件编译。
 * 
 * @see Requirements 1.1, 1.2, 1.3, 1.4
 */

#ifndef _HAL_HAL_CAPS_H_
#define _HAL_HAL_CAPS_H_

#include <types.h>
#include <stdbool.h>

/**
 * @brief 能力标识符枚举
 * 
 * 用于 hal_has_capability() 快速查询单个能力
 */
typedef enum hal_cap_id {
    HAL_CAP_HUGE_PAGES,         /**< 支持 2MB/1GB 大页 */
    HAL_CAP_NX_BIT,             /**< 支持不可执行位 (NX/XD/PXN) */
    HAL_CAP_PORT_IO,            /**< 支持端口 I/O (x86 only) */
    HAL_CAP_CACHE_COHERENT_DMA, /**< DMA 缓存一致性 */
    HAL_CAP_IOMMU,              /**< 支持 IOMMU */
    HAL_CAP_SMP,                /**< 支持多处理器 */
    HAL_CAP_FPU,                /**< 支持浮点单元 */
    HAL_CAP_SIMD,               /**< 支持 SIMD 指令 (SSE/NEON) */
    HAL_CAP_MAX
} hal_cap_id_t;

/**
 * @brief 最大支持的页大小数量
 */
#define HAL_MAX_PAGE_SIZES 4

/**
 * @brief HAL 能力结构体
 * 
 * 描述当前架构支持的硬件特性和限制。
 * 通过 hal_get_capabilities() 获取。
 */
typedef struct hal_capabilities {
    /* ---- 硬件特性 ---- */
    bool has_huge_pages;        /**< 支持 2MB/1GB 大页 */
    bool has_nx_bit;            /**< 支持不可执行位 */
    bool has_port_io;           /**< 支持端口 I/O (x86 only) */
    bool cache_coherent_dma;    /**< DMA 缓存一致性 */
    bool has_iommu;             /**< 支持 IOMMU */
    bool has_smp;               /**< 支持多处理器 */
    bool has_fpu;               /**< 支持浮点单元 */
    bool has_simd;              /**< 支持 SIMD 指令 */
    
    /* ---- 页表配置 ---- */
    uint32_t page_table_levels; /**< 页表级数 (2 for i686, 4 for x86_64/arm64) */
    uint32_t page_sizes[HAL_MAX_PAGE_SIZES]; /**< 支持的页大小数组 (bytes) */
    uint32_t page_size_count;   /**< 支持的页大小数量 */
    
    /* ---- 地址空间限制 ---- */
    uint64_t phys_addr_bits;    /**< 物理地址位数 */
    uint64_t virt_addr_bits;    /**< 虚拟地址位数 */
    uint64_t phys_addr_max;     /**< 最大物理地址 */
    uint64_t virt_addr_max;     /**< 最大虚拟地址 */
    uint64_t kernel_base;       /**< 内核虚拟地址基址 */
    uint64_t user_space_end;    /**< 用户空间结束地址 */
    
    /* ---- 寄存器信息 ---- */
    uint32_t gpr_count;         /**< 通用寄存器数量 */
    uint32_t gpr_size;          /**< 通用寄存器大小 (bytes) */
    uint32_t context_size;      /**< 上下文结构大小 (bytes) */
    
    /* ---- 架构标识 ---- */
    const char *arch_name;      /**< 架构名称字符串 */
    uint32_t arch_bits;         /**< 架构位数 (32 or 64) */
} hal_capabilities_t;

/**
 * @brief 获取 HAL 能力信息
 * 
 * 填充 hal_capabilities_t 结构体，包含当前架构的所有能力信息。
 * 此函数在内核初始化早期即可调用。
 * 
 * @param[out] caps 指向要填充的能力结构体的指针
 * 
 * @note 每个架构必须实现此函数
 * @see Requirements 1.1, 1.3
 */
void hal_get_capabilities(hal_capabilities_t *caps);

/**
 * @brief 检查特定能力是否支持
 * 
 * 快速查询单个能力，无需获取完整的能力结构体。
 * 
 * @param cap 能力标识符
 * @return true 如果当前架构支持该能力
 * 
 * @see Requirements 1.2
 */
bool hal_has_capability(hal_cap_id_t cap);

/**
 * @brief 获取默认页大小
 * 
 * @return 默认页大小 (通常为 4096 bytes)
 */
static inline uint32_t hal_get_page_size(void) {
    return 4096;  /* 所有支持的架构都使用 4KB 基本页 */
}

/**
 * @brief 获取大页大小
 * 
 * @return 大页大小 (2MB)，如果不支持大页返回 0
 */
uint32_t hal_get_huge_page_size(void);

#endif /* _HAL_HAL_CAPS_H_ */
