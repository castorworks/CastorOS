/**
 * @file fcntl.h
 * @brief 文件控制定义
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _FCNTL_H_
#define _FCNTL_H_

#include <types.h>

// ============================================================================
// open() 标志
// ============================================================================

// 访问模式（互斥）
#define O_RDONLY    0x0000      // 只读
#define O_WRONLY    0x0001      // 只写
#define O_RDWR      0x0002      // 读写
#define O_ACCMODE   0x0003      // 访问模式掩码

// 创建和状态标志
#define O_CREAT     0x0040      // 不存在则创建
#define O_EXCL      0x0080      // 与 O_CREAT 一起使用，文件存在则失败
#define O_TRUNC     0x0200      // 截断为 0
#define O_APPEND    0x0400      // 追加模式
#define O_NONBLOCK  0x0800      // 非阻塞 I/O
#define O_SYNC      0x1000      // 同步写入
#define O_DIRECTORY 0x10000     // 必须是目录
#define O_NOFOLLOW  0x20000     // 不跟随符号链接
#define O_CLOEXEC   0x80000     // exec 时关闭

// ============================================================================
// fcntl() 命令
// ============================================================================

#define F_DUPFD     0           // 复制文件描述符
#define F_GETFD     1           // 获取文件描述符标志
#define F_SETFD     2           // 设置文件描述符标志
#define F_GETFL     3           // 获取文件状态标志
#define F_SETFL     4           // 设置文件状态标志
#define F_GETLK     5           // 获取记录锁
#define F_SETLK     6           // 设置记录锁（非阻塞）
#define F_SETLKW    7           // 设置记录锁（阻塞）

// 文件描述符标志
#define FD_CLOEXEC  1           // exec 时关闭

// ============================================================================
// 文件模式（权限）
// ============================================================================

#define S_IRWXU     0700        // 所有者读写执行
#define S_IRUSR     0400        // 所有者读
#define S_IWUSR     0200        // 所有者写
#define S_IXUSR     0100        // 所有者执行

#define S_IRWXG     0070        // 组读写执行
#define S_IRGRP     0040        // 组读
#define S_IWGRP     0020        // 组写
#define S_IXGRP     0010        // 组执行

#define S_IRWXO     0007        // 其他读写执行
#define S_IROTH     0004        // 其他读
#define S_IWOTH     0002        // 其他写
#define S_IXOTH     0001        // 其他执行

// ============================================================================
// 函数声明
// ============================================================================

/**
 * @brief 打开文件
 * @param pathname 文件路径
 * @param flags 打开标志
 * @param ... 创建文件时的权限模式
 * @return 文件描述符，-1 失败
 */
int open(const char *pathname, int flags, ...);

/**
 * @brief 文件控制
 * @param fd 文件描述符
 * @param cmd 命令
 * @param ... 可选参数
 * @return 依赖于命令，-1 失败
 */
int fcntl(int fd, int cmd, ...);

/**
 * @brief 创建文件
 * @param pathname 文件路径
 * @param mode 权限模式
 * @return 文件描述符，-1 失败
 */
int creat(const char *pathname, mode_t mode);

#endif // _FCNTL_H_

