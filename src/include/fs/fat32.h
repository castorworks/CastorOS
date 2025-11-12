#ifndef _FS_FAT32_H_
#define _FS_FAT32_H_

#include <fs/vfs.h>
#include <fs/blockdev.h>
#include <types.h>

/**
 * FAT32 文件系统
 * 
 * 支持：
 * - 文件读取
 * - 目录遍历
 * - 长文件名（部分支持）
 */

/**
 * 初始化 FAT32 文件系统
 * @param dev 块设备（可以是分区）
 * @return 根目录节点，失败返回 NULL
 */
fs_node_t *fat32_init(blockdev_t *dev);

/**
 * 检查块设备是否为 FAT32 文件系统
 * @param dev 块设备
 * @return true 如果是 FAT32，false 否则
 */
bool fat32_probe(blockdev_t *dev);

#endif // _FS_FAT32_H_

