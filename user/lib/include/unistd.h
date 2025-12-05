/**
 * @file unistd.h
 * @brief POSIX 标准系统调用
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <types.h>

// ============================================================================
// 标准文件描述符
// ============================================================================

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

// ============================================================================
// lseek whence 参数
// ============================================================================

#define SEEK_SET    0   // 从文件开头
#define SEEK_CUR    1   // 从当前位置
#define SEEK_END    2   // 从文件末尾

// ============================================================================
// access 模式
// ============================================================================

#define F_OK    0   // 文件存在
#define R_OK    4   // 可读
#define W_OK    2   // 可写
#define X_OK    1   // 可执行

// ============================================================================
// 进程控制
// ============================================================================

/**
 * @brief 终止当前进程
 * @param status 退出状态码
 */
void _exit(int status);

/**
 * @brief 创建子进程
 * @return 父进程返回子进程 PID，子进程返回 0，失败返回 -1
 */
int fork(void);

/**
 * @brief 执行新程序
 * @param path 程序路径
 * @return 成功不返回，失败返回 -1
 */
int exec(const char *path);

/**
 * @brief 获取当前进程 ID
 */
int getpid(void);

/**
 * @brief 获取父进程 ID
 */
int getppid(void);

// ============================================================================
// 文件操作
// ============================================================================

/**
 * @brief 关闭文件描述符
 * @param fd 文件描述符
 * @return 0 成功，-1 失败
 */
int close(int fd);

/**
 * @brief 读取数据
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 * @return 实际读取字节数，0 表示 EOF，-1 失败
 */
ssize_t read(int fd, void *buf, size_t count);

/**
 * @brief 写入数据
 * @param fd 文件描述符
 * @param buf 数据缓冲区
 * @param count 写入字节数
 * @return 实际写入字节数，-1 失败
 */
ssize_t write(int fd, const void *buf, size_t count);

/**
 * @brief 移动文件读写位置
 * @param fd 文件描述符
 * @param offset 偏移量
 * @param whence 起始位置（SEEK_SET/SEEK_CUR/SEEK_END）
 * @return 新位置，-1 失败
 */
off_t lseek(int fd, off_t offset, int whence);

/**
 * @brief 复制文件描述符
 * @param oldfd 原文件描述符
 * @return 新文件描述符，-1 失败
 */
int dup(int oldfd);

/**
 * @brief 复制文件描述符到指定位置
 * @param oldfd 原文件描述符
 * @param newfd 目标文件描述符
 * @return newfd，-1 失败
 */
int dup2(int oldfd, int newfd);

/**
 * @brief 创建管道
 * @param pipefd 管道文件描述符数组 [读端, 写端]
 * @return 0 成功，-1 失败
 */
int pipe(int pipefd[2]);

/**
 * @brief 截断文件
 * @param fd 文件描述符
 * @param length 新长度
 * @return 0 成功，-1 失败
 */
int ftruncate(int fd, off_t length);

// ============================================================================
// 目录操作
// ============================================================================

/**
 * @brief 改变当前工作目录
 * @param path 目录路径
 * @return 0 成功，-1 失败
 */
int chdir(const char *path);

/**
 * @brief 获取当前工作目录
 * @param buf 缓冲区
 * @param size 缓冲区大小
 * @return buf 指针，失败返回 NULL
 */
char *getcwd(char *buf, size_t size);

/**
 * @brief 删除文件
 * @param pathname 文件路径
 * @return 0 成功，-1 失败
 */
int unlink(const char *pathname);

/**
 * @brief 删除空目录
 * @param pathname 目录路径
 * @return 0 成功，-1 失败
 */
int rmdir(const char *pathname);

// ============================================================================
// 内存管理
// ============================================================================

/**
 * @brief 设置数据段结束地址
 * @param addr 新地址
 * @return 当前数据段结束地址
 */
void *brk(void *addr);

/**
 * @brief 增加/减少数据段大小
 * @param increment 增量（字节）
 * @return 原数据段结束地址
 */
void *sbrk(int increment);

// ============================================================================
// 系统信息
// ============================================================================

/**
 * @brief 获取主机名
 * @param name 输出缓冲区
 * @param len 缓冲区大小
 * @return 0 成功，-1 失败
 */
int gethostname(char *name, size_t len);

/**
 * @brief 休眠指定秒数
 * @param seconds 秒数
 * @return 剩余秒数（被信号中断时）
 */
unsigned int sleep(unsigned int seconds);

/**
 * @brief 休眠指定微秒数
 * @param usec 微秒数
 * @return 0 成功，-1 失败
 */
int usleep(unsigned int usec);

// ============================================================================
// 系统控制（CastorOS 扩展）
// ============================================================================

/**
 * @brief 重启系统
 */
int reboot(void);

/**
 * @brief 关机
 */
int poweroff(void);

// ============================================================================
// 兼容性定义
// ============================================================================

/**
 * @brief 终止当前进程（_exit 的别名）
 */
void exit(int status);

#endif // _UNISTD_H_

