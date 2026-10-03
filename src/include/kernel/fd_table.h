/**
 * 文件描述符表
 * 
 * 每个进程维护一个文件描述符表，用于管理打开的文件
 */

#ifndef _KERNEL_FD_TABLE_H_
#define _KERNEL_FD_TABLE_H_

#include <types.h>
#include <fs/vfs.h>
#include <kernel/sync/spinlock.h>

/* 最大文件描述符数 */
#define MAX_FDS 512

namespace kernel {

/* 文件描述符表项 */
struct FdEntry {
    fs_node_t *node;        // 指向 VFS 节点
    uint32_t offset;        // 当前文件偏移量
    int32_t flags;          // 打开标志（O_RDONLY, O_WRONLY, O_RDWR 等）
    bool in_use;            // 是否在使用
};

/* 文件描述符表 */
struct FdTable {
    sync::Spinlock lock;                // 保护 FD 表的自旋锁
    FdEntry entries[MAX_FDS];

    /**
     * 初始化文件描述符表
     * @param table 文件描述符表指针
     */
    static void init(FdTable *table);

    /**
     * 分配一个文件描述符
     * @param table 文件描述符表指针
     * @param node VFS 节点
     * @param flags 打开标志
     * 
     * 返回值：
     *   >= 0: 文件描述符
     *   -1: 失败（表满）
     */
    static int32_t alloc(FdTable *table, fs_node_t *node, int32_t flags);

    /**
     * 获取文件描述符表项
     * @param table 文件描述符表指针
     * @param fd 文件描述符
     * 
     * 返回值：
     *   非 NULL: 表项指针
     *   NULL: 无效的文件描述符
     */
    static FdEntry *get(FdTable *table, int32_t fd);

    /**
     * 释放文件描述符
     * @param table 文件描述符表指针
     * @param fd 文件描述符
     * 
     * 返回值：
     *   0: 成功
     *   -1: 失败（无效的文件描述符）
     */
    static int32_t free(FdTable *table, int32_t fd);

    /**
     * 复制文件描述符表（用于 fork）
     * @param src 源表
     * @param dst 目标表
     * 
     * 返回值：
     *   0: 成功
     *   -1: 失败
     */
    static int32_t copy(FdTable *src, FdTable *dst);
};

} // namespace kernel

#endif /* _KERNEL_FD_TABLE_H_ */
