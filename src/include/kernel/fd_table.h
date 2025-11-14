/**
 * 文件描述符表
 * 
 * 每个进程维护一个文件描述符表，用于管理打开的文件
 */

#ifndef _KERNEL_FD_TABLE_H_
#define _KERNEL_FD_TABLE_H_

#include <types.h>
#include <fs/vfs.h>

/* 最大文件描述符数 */
#define MAX_FDS 512

/* 文件描述符表项 */
typedef struct {
    fs_node_t *node;        // 指向 VFS 节点
    uint32_t offset;        // 当前文件偏移量
    int32_t flags;          // 打开标志（O_RDONLY, O_WRONLY, O_RDWR 等）
    bool in_use;            // 是否在使用
} fd_entry_t;

/* 文件描述符表 */
typedef struct {
    fd_entry_t entries[MAX_FDS];
} fd_table_t;

/**
 * 初始化文件描述符表
 * @param table 文件描述符表指针
 */
void fd_table_init(fd_table_t *table);

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
int32_t fd_table_alloc(fd_table_t *table, fs_node_t *node, int32_t flags);

/**
 * 获取文件描述符表项
 * @param table 文件描述符表指针
 * @param fd 文件描述符
 * 
 * 返回值：
 *   非 NULL: 表项指针
 *   NULL: 无效的文件描述符
 */
fd_entry_t *fd_table_get(fd_table_t *table, int32_t fd);

/**
 * 释放文件描述符
 * @param table 文件描述符表指针
 * @param fd 文件描述符
 * 
 * 返回值：
 *   0: 成功
 *   -1: 失败（无效的文件描述符）
 */
int32_t fd_table_free(fd_table_t *table, int32_t fd);

/**
 * 复制文件描述符表（用于 fork）
 * @param src 源表
 * @param dst 目标表
 * 
 * 返回值：
 *   0: 成功
 *   -1: 失败
 */
int32_t fd_table_copy(fd_table_t *src, fd_table_t *dst);

#endif /* _KERNEL_FD_TABLE_H_ */
