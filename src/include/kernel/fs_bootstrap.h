#ifndef _KERNEL_FS_BOOTSTRAP_H_
#define _KERNEL_FS_BOOTSTRAP_H_

/**
 * 文件系统启动流程
 *
 * 负责探测块设备、解析分区、选择根文件系统，并完成 /dev 挂载。
 */

/**
 * 初始化内核文件系统环境
 */
void fs_init(void);

#endif /* _KERNEL_FS_BOOTSTRAP_H_ */


