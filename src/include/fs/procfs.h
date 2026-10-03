#ifndef _FS_PROCFS_H_
#define _FS_PROCFS_H_

#include <types.h>
#include <fs/vfs.h>

namespace fs {

/**
 * @brief 进程信息文件系统 (/proc)
 */
class Procfs {
public:
    /**
     * procfs（进程文件系统）
     * 
     * 提供对进程信息的统一访问接口
     * 符合 POSIX 标准，通过 /proc 文件系统获取进程信息
     */

    /**
     * 初始化 procfs
     * @return /proc 根目录节点
     */
    static fs_node_t *init();
};

} // namespace fs

#endif // _FS_PROCFS_H_
