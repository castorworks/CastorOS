/**
 * @file pmm.h
 * @brief 物理内存管理器
 * 
 * 使用位图管理物理页帧的分配和释放
 */

#ifndef _MM_PMM_H_
#define _MM_PMM_H_

#include <types.h>
#include <kernel/multiboot.h>

/**
 * @brief 物理内存信息结构
 */
typedef struct {
    uint32_t total_frames;     ///< 总页帧数
    uint32_t free_frames;      ///< 空闲页帧数
    uint32_t used_frames;      ///< 已使用页帧数
    uint32_t reserved_frames;  ///< 保留页帧数（内核+位图）
    uint32_t kernel_frames;    ///< 内核占用页帧数
    uint32_t bitmap_frames;    ///< 位图占用页帧数
} pmm_info_t;

/**
 * @brief 初始化物理内存管理器
 * @param mbi Multiboot信息结构指针
 */
void pmm_init(multiboot_info_t *mbi);

/**
 * @brief 分配一个物理页帧
 * @return 成功返回页帧的物理地址，失败返回 0
 */
uint32_t pmm_alloc_frame(void);

/**
 * @brief 释放一个物理页帧
 * @param frame 页帧的物理地址
 */
void pmm_free_frame(uint32_t frame);

/**
 * @brief 将物理页帧标记为受保护（禁止释放）
 * @param frame 页帧的物理地址
 */
void pmm_protect_frame(uint32_t frame);

/**
 * @brief 取消物理页帧的保护标记
 * @param frame 页帧的物理地址
 */
void pmm_unprotect_frame(uint32_t frame);

/**
 * @brief 查询物理帧是否处于保护状态
 * @param frame 页帧的物理地址
 * @return true 表示受保护，false 表示未受保护
 */
bool pmm_is_frame_protected(uint32_t frame);

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
 */
uint32_t pmm_get_bitmap_end(void);

/**
 * @brief 设置堆保留区域的物理地址范围
 * @param heap_virt_start 堆虚拟起始地址
 * @param heap_virt_end 堆虚拟结束地址（最大地址）
 * 
 * 将堆虚拟地址范围转换为物理地址范围，并标记这些物理帧不可分配。
 * 这防止了堆扩展时重新映射已分配帧的恒等映射导致的内存损坏。
 */
void pmm_set_heap_reserved_range(uint32_t heap_virt_start, uint32_t heap_virt_end);

/**
 * @brief 增加物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_inc(uint32_t frame);

/**
 * @brief 减少物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_dec(uint32_t frame);

/**
 * @brief 获取物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 引用计数值
 */
uint32_t pmm_frame_get_refcount(uint32_t frame);

#endif // _MM_PMM_H_
