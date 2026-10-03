#ifndef _FS_FAT32_H_
#define _FS_FAT32_H_

#include <fs/vfs.h>
#include <fs/blockdev.h>
#include <types.h>

namespace fs {

/**
 * @brief FAT32 文件系统
 */
class Fat32 {
public:
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
    static fs_node_t *init(blockdev_t *dev);

    /**
     * 检查块设备是否为 FAT32 文件系统
     * @param dev 块设备
     * @return true 如果是 FAT32，false 否则
     */
    static bool probe(blockdev_t *dev);

    /**
     * 卸载 FAT32 文件系统并释放所有资源
     * @param root 根目录节点（由 fat32_init 返回）
     */
    static void deinit(fs_node_t *root);
};

} // namespace fs

#endif // _FS_FAT32_H_
