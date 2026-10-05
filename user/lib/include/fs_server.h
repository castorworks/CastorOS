#ifndef _USERLAND_LIB_FS_SERVER_H_
#define _USERLAND_LIB_FS_SERVER_H_

#include <types.h>

// 文件服务的服务端骨架。
//
// fs.h 里的协议（客户的共享缓冲区、句柄、请求分发、回收已退出的客户）对每种
// 文件系统都一样，放在这里；具体的文件系统只需要提供下面这组按“文件号”操作的函数。
// 文件号是后端自己定的小整数（>= 0），在文件存在期间保持不变。

struct fs_backend {
    /** 按名字找文件。@return 文件号，没有返回 -1 */
    int (*find)(const char *name);
    /** 创建空文件。@return 文件号，失败返回 -1 */
    int (*create)(const char *name);
    /** 文件大小 */
    long (*size)(int file);
    /** 从 offset 读最多 len 字节到 buf。@return 读到的字节数（文件末尾为 0），失败 -1 */
    long (*read)(int file, uint32_t offset, char *buf, uint32_t len);
    /** 把 buf 的 len 字节写到 offset，文件按需变大。@return 写入的字节数，失败 -1 */
    long (*write)(int file, uint32_t offset, const char *buf, uint32_t len);
    /** 清空文件。@return 0 成功 */
    int (*truncate)(int file);
    /** 删除文件。@return 0 成功 */
    int (*remove)(int file);
    /** 第 index 个文件的名字（至少 FS_NAME_MAX 字节）和大小。@return 0 成功，-1 没有这一项 */
    int (*list)(int index, char *name, uint32_t *size);
};

/**
 * 以 service_name 登记并开始处理请求。只有登记失败时才返回（返回 -1）。
 */
int fs_serve(const char *service_name, const struct fs_backend *backend);

#endif // _USERLAND_LIB_FS_SERVER_H_
