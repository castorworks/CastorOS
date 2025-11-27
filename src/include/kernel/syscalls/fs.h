/**
 * 文件系统相关系统调用
 * 
 * 实现 POSIX 标准的文件 I/O 系统调用：
 * - read(2)
 * - write(2)
 * - open(2)
 * - close(2)
 * - lseek(2)
 * 等
 */

#ifndef _KERNEL_SYSCALLS_FILE_H_
#define _KERNEL_SYSCALLS_FILE_H_

#include <types.h>

/* ============================================================================
 * 打开标志（POSIX 标准）
 * ============================================================================ */

/* 文件访问模式（互斥） */
#define O_RDONLY        0x0000  // 只读
#define O_WRONLY        0x0001  // 只写
#define O_RDWR          0x0002  // 读写

/* 文件创建标志 */
#define O_CREAT         0x0040  // 如果文件不存在则创建
#define O_EXCL          0x0080  // 与 O_CREAT 一起使用，如果文件存在则失败
#define O_TRUNC         0x0200  // 如果文件存在且是普通文件，截断为 0
#define O_APPEND        0x0400  // 追加模式

/* seek 基准位置（POSIX 标准） */
#define SEEK_SET        0       // 文件开始
#define SEEK_CUR        1       // 当前位置
#define SEEK_END        2       // 文件末尾

/**
 * sys_read - 从文件描述符读取数据（实现函数）
 * @fd: 文件描述符
 * @buf: 缓冲区地址
 * @count: 要读取的字节数
 * 
 * 返回值：
 *   > 0: 实际读取的字节数
 *   0: 文件末尾
 *   (uint32_t)-1: 错误
 */
uint32_t sys_read(int32_t fd, void *buf, uint32_t count);

/**
 * sys_write - 向文件描述符写入数据（实现函数）
 * @fd: 文件描述符
 * @buf: 数据缓冲区地址
 * @count: 要写入的字节数
 * 
 * 返回值：
 *   > 0: 实际写入的字节数
 *   (uint32_t)-1: 错误
 */
uint32_t sys_write(int32_t fd, const void *buf, uint32_t count);

/**
 * sys_open - 打开或创建文件（实现函数）
 * @path: 文件路径
 * @flags: 打开标志（O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC 等）
 * @mode: 创建文件时的权限（仅当 O_CREAT 标志时使用）
 * 
 * 返回值：
 *   >= 0: 文件描述符
 *   (uint32_t)-1: 错误
 */
uint32_t sys_open(const char *path, int32_t flags, uint32_t mode);

/**
 * sys_close - 关闭文件描述符（实现函数）
 * @fd: 文件描述符
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_close(int32_t fd);

/**
 * sys_lseek - 移动文件指针（实现函数）
 * @fd: 文件描述符
 * @offset: 偏移量
 * @whence: 基准位置（SEEK_SET, SEEK_CUR, SEEK_END）
 * 
 * 返回值：
 *   >= 0: 新的文件位置
 *   (uint32_t)-1: 错误
 */
uint32_t sys_lseek(int32_t fd, int32_t offset, int32_t whence);

/**
 * sys_mkdir - 创建目录
 * @path: 目录路径
 * @mode: 目录权限（暂未使用）
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_mkdir(const char *path, uint32_t mode);

/**
 * sys_unlink - 删除文件或目录
 * @path: 文件或目录路径
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_unlink(const char *path);

/**
 * sys_getdents - 读取目录项（简化版本）
 * @fd: 目录文件描述符
 * @index: 目录项索引
 * @dirent: 用户空间目录项结构指针
 * 
 * 注意：这是简化版本，与 Linux 标准 getdents 接口不同。
 * Linux 的 getdents 是批量读取多个目录项，而这里按索引读取单个目录项。
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误或已到目录末尾
 */
uint32_t sys_getdents(int32_t fd, uint32_t index, void *dirent);

/**
 * sys_chdir - 切换当前工作目录
 * @path: 目录路径
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_chdir(const char *path);

/**
 * sys_getcwd - 获取当前工作目录
 * @buffer: 用户空间缓冲区
 * @size: 缓冲区大小
 * 
 * 返回值：
 *   成功: 缓冲区指针
 *   (uint32_t)-1: 错误
 */
uint32_t sys_getcwd(char *buffer, uint32_t size);

/**
 * sys_stat - 获取文件状态信息
 * @path: 文件路径
 * @buf: 用户空间 stat 结构体指针
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_stat(const char *path, struct stat *buf);

/**
 * sys_fstat - 获取文件描述符状态信息
 * @fd: 文件描述符
 * @buf: 用户空间 stat 结构体指针
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_fstat(int32_t fd, struct stat *buf);

/**
 * sys_ftruncate - 截断文件到指定大小
 * @fd: 文件描述符
 * @length: 新的文件大小
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_ftruncate(int32_t fd, uint32_t length);

/**
 * sys_pipe - 创建管道
 * @fds: 用户空间数组，fds[0] 为读端，fds[1] 为写端
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 */
uint32_t sys_pipe(int32_t *fds);

/**
 * sys_dup - 复制文件描述符
 * @oldfd: 要复制的文件描述符
 * 
 * 返回值：
 *   >= 0: 新的文件描述符
 *   (uint32_t)-1: 错误
 */
uint32_t sys_dup(int32_t oldfd);

/**
 * sys_dup2 - 复制文件描述符到指定编号
 * @oldfd: 要复制的文件描述符
 * @newfd: 目标文件描述符编号
 * 
 * 返回值：
 *   >= 0: 新的文件描述符（即 newfd）
 *   (uint32_t)-1: 错误
 */
uint32_t sys_dup2(int32_t oldfd, int32_t newfd);

/**
 * sys_rename - 重命名文件或目录
 * @oldpath: 原路径
 * @newpath: 新路径
 * 
 * 返回值：
 *   0: 成功
 *   (uint32_t)-1: 错误
 * 
 * 注意：当前仅支持同一目录下的重命名
 */
uint32_t sys_rename(const char *oldpath, const char *newpath);

#endif /* _KERNEL_SYSCALLS_FILE_H_ */
