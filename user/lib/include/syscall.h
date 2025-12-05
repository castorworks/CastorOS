/**
 * @file syscall.h
 * @brief 系统调用统一入口（推荐使用标准 POSIX 头文件）
 * 
 * 新代码应直接使用标准头文件：
 *   - <unistd.h>     - 进程、文件操作
 *   - <fcntl.h>      - 文件控制
 *   - <sys/socket.h> - Socket API
 *   - <netinet/in.h> - Internet 地址
 *   - <arpa/inet.h>  - 地址转换
 *   - <sys/select.h> - I/O 多路复用
 *   - <net/if.h>     - 网络接口
 */

#ifndef _SYSCALL_H_
#define _SYSCALL_H_

// 基础类型
#include <types.h>

// 系统调用接口
#include <sys/syscall.h>

// POSIX 标准头文件
#include <unistd.h>
#include <fcntl.h>

// Socket 和网络
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

// ============================================================================
// 额外的函数声明
// ============================================================================

// 等待子进程
int waitpid(int pid, int *wstatus, int options);
int wait(int *wstatus);

// 目录操作
int mkdir(const char *path, uint32_t mode);
int getdents(int fd, uint32_t index, struct dirent *dirent);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int rename(const char *oldpath, const char *newpath);

// 内存映射
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);

// 系统信息
int uname(struct utsname *buf);

// 调试
void print(const char *msg);

#endif // _SYSCALL_H_
