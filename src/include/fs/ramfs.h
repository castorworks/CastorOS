#ifndef _FS_RAMFS_H_
#define _FS_RAMFS_H_

#include <fs/vfs.h>

/**
 * RAMFS - 基于 RAM 的简单文件系统
 * 
 * 特性：
 * - 完全存储在内存中
 * - 支持文件和目录的创建、读写、删除
 * - 动态分配内存
 * - 适合作为根文件系统或临时文件系统
 */

/**
 * 初始化 ramfs
 * @return 根目录节点
 */
fs_node_t *ramfs_init(void);

/**
 * 创建一个新的 ramfs 挂载点
 * @param name 挂载点名称
 * @return 根目录节点
 */
fs_node_t *ramfs_create(const char *name);

#endif // _FS_RAMFS_H_
