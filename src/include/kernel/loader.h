#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <types.h>

/**
 * 加载内嵌在内核映像里的 init 程序（user/init），创建第一个用户进程
 */
bool load_init(void);

#endif /* KERNEL_LOADER_H */
