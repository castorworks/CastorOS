#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <types.h>

/**
 * 从文件系统加载并启动用户态 shell
 */
bool load_user_shell(void);

#endif /* KERNEL_LOADER_H */
