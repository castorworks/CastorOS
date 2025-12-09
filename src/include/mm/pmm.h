/**
 * @file pmm.h
 * @brief 物理内存管理器接口（重构版）
 * 
 * 使用位图管理物理页帧的分配和释放。
 * 支持 64-bit 物理地址，兼容 i686、x86_64 和 ARM64 架构。
 * 
 * @see Requirements 2.1, 2.2, 2.3
 */

#ifndef _MM_PMM_H_
#define _MM_PMM_H_

#include <types.h>
#include <mm/mm_types.h>
#include <kernel/multiboot.h>

/*============================================================================
 * 内存区域定义
 *============================================================================*/

/** 
 * @brief 内存区域类型
 * 
 * 用于 DMA 和特殊内存分配需求
 */
typedef enum {
    ZONE_DMA,       /**< DMA 区域 (0-16MB on x86) */
    ZONE_NORMAL,    /**< 普通区域 */
    ZONE_HIGH,      /**< 高端内存 (>896MB on i686) */
    ZONE_COUNT
} pmm_zone_t;

/*============================================================================
 * PMM 信息结构
 *============================================================================*/

/**
 * @brief 物理内存信息结构
 * 
 * 使用 pfn_t 类型支持大于 4GB 的物理内存
 * 
 * @see Requirements 2.2
 */
typedef struct {
    pfn_t total_frames;     ///< 总页帧数
    pfn_t free_frames;      ///< 空闲页帧数
    pfn_t used_frames;      ///< 已使用页帧数
    pfn_t reserved_frames;  ///< 保留页帧数（内核+位图）
    pfn_t kernel_frames;    ///< 内核占用页帧数
    pfn_t bitmap_frames;    ///< 位图占用页帧数
} pmm_info_t;

/*============================================================================
 * PMM 核心接口
 *============================================================================*/

/**
 * @brief 初始化物理内存管理器
 * @param mbi Multiboot信息结构指针（i686/x86_64）或 DTB（ARM64）
 * 
 * 解析内存映射，初始化位图，标记已使用和空闲的页帧
 */
void pmm_init(multiboot_info_t *mbi);

/**
 * @brief 分配一个物理页帧
 * @return 成功返回页帧的物理地址，失败返回 PADDR_INVALID
 * 
 * 分配后会清零页帧内容。返回的地址保证是页对齐的。
 * 
 * @see Requirements 2.1
 */
paddr_t pmm_alloc_frame(void);

/**
 * @brief 从指定区域分配物理页帧
 * @param zone 内存区域
 * @return 成功返回物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_frame_zone(pmm_zone_t zone);

/**
 * @brief 分配连续物理页帧（用于 DMA）
 * @param count 页帧数量
 * @return 成功返回起始物理地址，失败返回 PADDR_INVALID
 * 
 * @see Requirements 2.2
 */
paddr_t pmm_alloc_frames(size_t count);

/**
 * @brief 释放一个物理页帧
 * @param frame 页帧的物理地址
 * 
 * 地址必须是页对齐的。
 * COW 支持：如果帧的引用计数 > 1，只递减计数，不实际释放。
 * 
 * @see Requirements 2.1
 */
void pmm_free_frame(paddr_t frame);

/**
 * @brief 释放连续物理页帧
 * @param frame 起始物理地址
 * @param count 页帧数量
 */
void pmm_free_frames(paddr_t frame, size_t count);

/**
 * @brief 将物理页帧标记为受保护（禁止释放）
 * @param frame 页帧的物理地址
 */
void pmm_protect_frame(paddr_t frame);

/**
 * @brief 取消物理页帧的保护标记
 * @param frame 页帧的物理地址
 */
void pmm_unprotect_frame(paddr_t frame);

/**
 * @brief 查询物理帧是否处于保护状态
 * @param frame 页帧的物理地址
 * @return true 表示受保护，false 表示未受保护
 */
bool pmm_is_frame_protected(paddr_t frame);

/*============================================================================
 * 引用计数接口（COW 支持）
 * @see Requirements 2.3
 *============================================================================*/

/**
 * @brief 增加物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_inc(paddr_t frame);

/**
 * @brief 减少物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_dec(paddr_t frame);

/**
 * @brief 获取物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 引用计数值
 */
uint32_t pmm_frame_get_refcount(paddr_t frame);

/*============================================================================
 * 信息查询接口
 *============================================================================*/

/**
 * @brief 获取物理内存信息
 * @return 物理内存信息结构
 */
pmm_info_t pmm_get_info(void);

/**
 * @brief 打印物理内存使用信息
 */
void pmm_print_info(void);

/**
 * @brief 获取位图结束地址（虚拟地址）
 * @return 位图结束的虚拟地址（页对齐）
 * 
 * 实际返回的是包括引用计数表在内的所有 PMM 数据结构的结束地址。
 */
uintptr_t pmm_get_bitmap_end(void);

/**
 * @brief 设置堆保留区域的物理地址范围
 * @param heap_virt_start 堆虚拟起始地址
 * @param heap_virt_end 堆虚拟结束地址（最大地址）
 * 
 * 将堆虚拟地址范围转换为物理地址范围，并标记这些物理帧不可分配。
 * 这防止了堆扩展时重新映射已分配帧的恒等映射导致的内存损坏。
 */
void pmm_set_heap_reserved_range(uintptr_t heap_virt_start, uintptr_t heap_virt_end);

#endif // _MM_PMM_H_
