#ifndef _USERLAND_LIB_FS_H_
#define _USERLAND_LIB_FS_H_

#include <types.h>

// 文件服务：协议和客户端接口。
//
// 服务进程以 "fs" 这个名字登记（目前是 user/ramfs，内存文件系统）。
// 文件名、读写的数据都不放在消息里，而是放在客户与服务之间的一块共享缓冲区：
// 客户第一次使用时用 mem_grant 把缓冲区共享给服务，之后每个请求只在消息里
// 带参数，内容在缓冲区里。命名空间是平的，没有目录。

#define FS_SERVICE_NAME "fs"

/** 文件名最大长度（含结尾 NUL） */
#define FS_NAME_MAX     64

/** 共享缓冲区大小，也是单次请求能传输的最大字节数 */
#define FS_BUF_SIZE     4096

// 请求的 label。应答的 data[0] 是结果（负数表示失败，按 int64_t 解释）
enum {
    FS_OPEN   = 1,  // 缓冲区: 文件名；data[0]: FS_O_* 标志。应答 data[0]: 文件句柄
    FS_CLOSE  = 2,  // data[0]: 句柄
    FS_READ   = 3,  // data[0]: 句柄, data[1]: 偏移, data[2]: 长度。应答 data[0]: 读到的字节数，内容在缓冲区
    FS_WRITE  = 4,  // data[0]: 句柄, data[1]: 偏移, data[2]: 长度；缓冲区: 内容。应答 data[0]: 写入的字节数
    FS_UNLINK = 5,  // 缓冲区: 文件名
    FS_LIST   = 6,  // data[0]: 序号（从 0 开始）。应答 data[0]: 0 有这一项 / -1 没有了，
                    //   data[1]: 文件大小，文件名在缓冲区
};

// FS_OPEN 的标志
#define FS_O_CREATE     0x1     // 不存在就创建
#define FS_O_TRUNC      0x2     // 打开时清空

/** 打开文件。@return 句柄 (>= 0)，失败返回 -1 */
int fs_open(const char *name, int flags);
int fs_close(int fd);

/** 从 offset 处读最多 len 字节。@return 读到的字节数（到文件末尾为 0），失败返回 -1 */
long fs_read(int fd, uint32_t offset, void *buf, size_t len);

/** 从 offset 处写 len 字节，文件按需变大。@return 写入的字节数，失败返回 -1 */
long fs_write(int fd, uint32_t offset, const void *buf, size_t len);

int fs_unlink(const char *name);

/**
 * 列出第 index 个文件
 * @param name 至少 FS_NAME_MAX 字节
 * @return 0 成功，-1 没有这一项
 */
int fs_list(int index, char *name, uint32_t *size);

#endif // _USERLAND_LIB_FS_H_
