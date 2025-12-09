/**
 * @file heap.h
 * @brief 内核堆内存管理器
 * 
 * 实现动态内存分配和释放功能，使用双向链表管理内存块
 */

#ifndef _MM_HEAP_H_
#define _MM_HEAP_H_

#include <types.h>

/**
 * @brief 堆内存块结构
 * 
 * 每个内存块包含元数据（大小、状态、链表指针）和实际数据区域
 */
typedef struct heap_block {
    size_t size;                    ///< 块大小（不包括元数据）
    bool is_free;                   ///< 是否空闲
    struct heap_block *next;        ///< 下一个块
    struct heap_block *prev;        ///< 上一个块
    uint32_t magic;                 ///< 魔数，用于检测内存损坏
} heap_block_t;

/** @brief 魔数，用于验证内存块的有效性 */
#define HEAP_MAGIC 0xDEADBEEF

/**
 * @brief 初始化堆内存管理器
 * @param start 堆起始地址
 * @param size 堆最大大小（字节）
 */
void heap_init(uintptr_t start, uint32_t size);

/**
 * @brief 分配内存
 * @param size 要分配的字节数
 * @return 成功返回分配的内存地址，失败返回 NULL
 */
void* kmalloc(size_t size);

/**
 * @brief 释放内存
 * @param ptr 要释放的内存指针
 */
void kfree(void* ptr);

/**
 * @brief 重新分配内存
 * @param ptr 原内存指针
 * @param size 新的字节数
 * @return 成功返回新内存地址，失败返回 NULL（原内存仍有效）
 */
void* krealloc(void* ptr, size_t size);

/**
 * @brief 分配并清零内存
 * @param num 元素数量
 * @param size 每个元素的大小
 * @return 成功返回分配的内存地址，失败返回 NULL
 */
void* kcalloc(size_t num, size_t size);

/**
 * @brief 分配对齐内存
 * @param size 要分配的字节数
 * @param alignment 对齐边界（必须是 2 的幂）
 * @return 成功返回对齐的内存地址，失败返回 NULL
 */
void* kmalloc_aligned(size_t size, size_t alignment);

/**
 * @brief 释放对齐内存
 * @param ptr 由 kmalloc_aligned 返回的指针
 */
void kfree_aligned(void* ptr);

/**
 * @brief 堆统计信息结构体
 */
typedef struct {
    size_t total;       ///< 堆总大小（字节）
    size_t used;        ///< 已使用大小（字节）
    size_t free;        ///< 空闲大小（字节）
    size_t max;         ///< 堆最大大小（字节）
    uint32_t block_count;  ///< 总块数
    uint32_t free_block_count;  ///< 空闲块数
} heap_info_t;

/**
 * @brief 获取堆使用统计信息
 * @param info 输出参数，用于存储堆统计信息
 * @return 成功返回 0，失败返回 -1
 */
int heap_get_info(heap_info_t *info);

/**
 * @brief 打印堆使用信息
 */
void heap_print_info(void);

#endif // _MM_HEAP_H_
