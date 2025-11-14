#ifndef _FS_PROCFS_H_
#define _FS_PROCFS_H_

#include <types.h>
#include <fs/vfs.h>

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
fs_node_t *procfs_init(void);

#endif // _FS_PROCFS_H_

