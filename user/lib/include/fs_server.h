#ifndef _USERLAND_LIB_FS_SERVER_H_
#define _USERLAND_LIB_FS_SERVER_H_

#include <types.h>

// 文件服务的服务端骨架。
//
// fs.h 里的协议（客户的共享缓冲区、句柄、请求分发、回收已退出的客户）对每种
// 文件系统都一样，放在这里；目录的规则也在这里（路径怎么写才合法、文件只能建在已有的
// 目录里、目录空了才能删、列出一个目录的直接成员）。具体的文件系统只管存：它看到的是
// 一张平的表，每一项有一个完整的路径（"docs/notes.txt"）、是不是目录、内容。
// 文件号是后端自己定的小整数（>= 0），在这一项存在期间保持不变。

struct fs_backend {
    /** 按完整路径找一项（文件或目录）。@return 文件号，没有返回 -1 */
    int (*find)(const char *path);
    /** 创建一个空文件，或者（directory 为 true）一个目录。@return 文件号，失败返回 -1 */
    int (*create)(const char *path, bool directory);
    /** 这一项是不是目录 */
    bool (*is_dir)(int file);
    /** 这一项的完整路径 */
    const char *(*path)(int file);
    /** 文件大小（目录是 0） */
    long (*size)(int file);
    /** 从 offset 读最多 len 字节到 buf。@return 读到的字节数（文件末尾为 0），失败 -1 */
    long (*read)(int file, uint32_t offset, char *buf, uint32_t len);
    /** 把 buf 的 len 字节写到 offset，文件按需变大。@return 写入的字节数，失败 -1 */
    long (*write)(int file, uint32_t offset, const char *buf, uint32_t len);
    /** 清空文件。@return 0 成功 */
    int (*truncate)(int file);
    /** 删除这一项。@return 0 成功 */
    int (*remove)(int file);
    /** 把这一项的完整路径改成 path（内容不动）。@return 0 成功 */
    int (*rename)(int file, const char *path);
    /** 表里的第 index 项（从 0 开始，顺序任意但稳定）。@return 文件号，-1 没有这么多项 */
    int (*entry)(int index);
};

/**
 * 以 service_name 登记并开始处理请求。只有登记失败时才返回（返回 -1）。
 */
int fs_serve(const char *service_name, const struct fs_backend *backend);

#endif // _USERLAND_LIB_FS_SERVER_H_
