#ifndef _FS_DEVFS_H_
#define _FS_DEVFS_H_

#include <types.h>
#include <fs/vfs.h>

namespace fs {

/**
 * @brief 设备文件系统 (/dev)
 */
class Devfs {
public:
    /**
     * devfs（设备文件系统）
     * 
     * 提供对设备的统一访问接口
     */

    /**
     * 初始化 devfs
     * @return /dev 根目录节点
     */
    static fs_node_t *init();
};

} // namespace fs

#endif // _FS_DEVFS_H_
