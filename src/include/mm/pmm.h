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
    uint32_t total_frames;  ///< 总页帧数
    uint32_t free_frames;   ///< 空闲页帧数
    uint32_t used_frames;   ///< 已使用页帧数
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

#endif // _MM_PMM_H_
