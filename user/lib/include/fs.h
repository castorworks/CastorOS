#ifndef _USERLAND_LIB_FS_H_
#define _USERLAND_LIB_FS_H_

#include <types.h>

// 文件服务：协议和客户端接口。
//
// 文件都在一棵目录树里，用路径来指，各段之间用 '/' 隔开：
//   "/bin/ls"     从根写起
//   "notes/a"     从当前目录写起；"." 是所在的目录，".." 是上一级
//
// 树后面有两个实现同一套协议的服务进程，哪条路径归谁由这个客户端库决定：
//   - 登记为 "diskfs" 的 user/diskfs：磁盘上的文件系统，内容跨重启保留
//   - 登记为 "fs" 的 user/ramfs：内存里的文件系统，开机时从内核带着的启动映像装载
// 磁盘上有我们的文件系统时，根在磁盘上，只有 /tmp 这棵子树在内存里（重启就没了）。
// 没有时（没有磁盘，或者盘上是别的东西），整棵树都在内存里，内容就是启动映像：系统
// 照样能用，只是什么都留不下来。
//
// 文件名、读写的数据都不放在消息里，而是放在客户与服务之间的一块共享缓冲区：
// 客户第一次使用时用 mem_grant 把缓冲区共享给服务，之后每个请求只在消息里
// 带参数，内容在缓冲区里。
//
// 当前目录（fs_chdir / fs_getcwd）是每个进程自己的，一开始是根；命令行启动程序时把自己的
// 当前目录传给它。当前目录和 "."、".." 都由这个客户端库处理掉，服务看到的永远是从根写起
// 的完整路径。文件只能建在已经存在的目录里（根目录总是存在），目录用 fs_mkdir 建，空了才能删。

#define FS_SERVICE_NAME         "fs"
#define FS_DISK_SERVICE_NAME    "diskfs"
/** 根在磁盘上时，这个目录下面的东西在内存文件系统里 */
#define FS_TMP_DIR              "tmp"

/** 路径的最大长度（整条路径，含结尾 NUL） */
#define FS_NAME_MAX     64

/** 共享缓冲区大小，也是单次请求能传输的最大字节数 */
#define FS_BUF_SIZE     4096

// 请求的 label。应答的 data[0] 是结果（负数表示失败，按 int64_t 解释）
enum {
    FS_OPEN   = 1,  // 缓冲区: 路径；data[0]: FS_O_* 标志。应答 data[0]: 文件句柄
    FS_CLOSE  = 2,  // data[0]: 句柄
    FS_READ   = 3,  // data[0]: 句柄, data[1]: 偏移, data[2]: 长度。应答 data[0]: 读到的字节数，内容在缓冲区
    FS_WRITE  = 4,  // data[0]: 句柄, data[1]: 偏移, data[2]: 长度；缓冲区: 内容。应答 data[0]: 写入的字节数
    FS_UNLINK = 5,  // 缓冲区: 路径。删除一个文件，或者一个空目录
    FS_SIZE   = 7,  // data[0]: 句柄。应答 data[0]: 文件大小
    FS_LIST   = 6,  // 缓冲区: 目录的路径（空串 = 根目录）；data[0]: 序号（从 0 开始）。
                    //   应答 data[0]: 0 有这一项 / -1 没有了（或者那不是一个目录），
                    //   data[1]: 文件大小，data[2]: 是不是目录，这一项的名字（不含目录部分）在缓冲区
    FS_MKDIR  = 8,  // 缓冲区: 路径。创建一个目录（它的上一级必须已经存在）
    FS_RENAME = 9,  // 缓冲区: 原来的路径、'\0'、新的路径。改名或者移到别的目录（同一个服务里）；
                    //   目录里的东西跟着走。新路径上已经有东西、或者它的上一级不存在时失败
    FS_STOP   = 10, // 关机前 init 发来：服务应答之后退出。请求是一个一个处理的，所以这时没有做到
                    //   一半的修改。不需要共享缓冲区
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

/** 文件大小（字节），失败返回 -1 */
long fs_size(int fd);

/** 删除一个文件，或者一个空目录 */
int fs_unlink(const char *name);

/** 创建目录；它的上一级必须已经存在。@return 0 成功，-1 失败（已经有同名的东西、上一级不存在…） */
int fs_mkdir(const char *path);

/**
 * 把 from 改名或者移动成 to。目录连同里面的东西一起移。两个路径必须在同一个文件服务里：
 * /tmp 和别处之间不能直接移（要复制再删）。
 * @return 0 成功；-1 from 不存在、to 已经存在、to 的上一级不存在、跨了文件服务…
 */
int fs_rename(const char *from, const char *to);

/**
 * 换当前目录。path 和别的路径一样可以是相对的。
 * @return 0 成功，-1 那不是一个存在的目录
 */
int fs_chdir(const char *path);

/** 当前目录，写成从根开始的完整路径："/"、"/home/notes"。buf 至少 FS_NAME_MAX + 8 字节 */
void fs_getcwd(char *buf);

/**
 * 当前目录的内部写法（"home/notes"，根是空串）/ 照这种写法直接设置，不检查。
 * 命令行用它把自己的当前目录交给它启动的程序（见 stdio.h）。
 */
const char *fs_cwd_spec(void);
void fs_set_cwd_spec(const char *spec);

/**
 * 列出目录 dir 的第 index 个成员
 * @param dir 一个目录的路径；"" 是当前目录
 * @param name 至少 FS_NAME_MAX 字节，得到这一项的名字（不含目录部分）
 * @param size、is_dir 可以是 NULL
 * @return 0 成功，-1 没有这一项（或者 dir 不是一个目录）
 */
int fs_list(const char *dir, int index, char *name, uint32_t *size, bool *is_dir);

#endif // _USERLAND_LIB_FS_H_
