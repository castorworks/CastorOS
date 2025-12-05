#ifndef _KERNEL_SYSCALL_H_
#define _KERNEL_SYSCALL_H_

#include <types.h>   // uint32_t, size_t, pid_t 等定义

// ============================================================================
// 系统调用号定义（与用户态保持一致）
// ============================================================================

// ---- 系统调用分类 ----------------------------------------------------------
// 0x00xx - 进程与线程
// 0x01xx - 文件与文件系统
// 0x02xx - 内存管理
// 0x03xx - 时间与时钟
// 0x04xx - 信号与进程控制
// 0x05xx - 系统信息、杂项
// 0x06xx - 网络（BSD Socket API）
// ----------------------------------------------------------------------------

enum {
    // -------------------- 进程与线程 (0x00xx) --------------------
    SYS_EXIT            = 0x0000,
    SYS_FORK            = 0x0001,
    SYS_EXECVE          = 0x0002,
    SYS_WAITPID         = 0x0003,
    SYS_GETPID          = 0x0004,
    SYS_GETPPID         = 0x0005,
    SYS_SCHED_YIELD     = 0x0006,
    SYS_CLONE           = 0x0007,

    // -------------------- 文件与文件系统 (0x01xx) --------------------
    SYS_OPEN            = 0x0100,
    SYS_CLOSE           = 0x0101,
    SYS_READ            = 0x0102,
    SYS_WRITE           = 0x0103,
    SYS_LSEEK           = 0x0104,
    SYS_STAT            = 0x0105,
    SYS_FSTAT           = 0x0106,
    SYS_MKDIR           = 0x0107,
    SYS_RMDIR           = 0x0108,
    SYS_UNLINK          = 0x0109,
    SYS_RENAME          = 0x010A,
    SYS_GETCWD          = 0x010B,
    SYS_CHDIR           = 0x010C,
    SYS_GETDENTS        = 0x010D,  // 读取目录项（简化版本，与 Linux getdents 接口不同）
    SYS_FTRUNCATE       = 0x010E,  // 截断文件到指定大小
    SYS_PIPE            = 0x010F,  // 创建管道
    SYS_DUP             = 0x0110,  // 复制文件描述符
    SYS_DUP2            = 0x0111,  // 复制文件描述符到指定编号
    SYS_IOCTL           = 0x0112,  // 设备控制

    // -------------------- 内存管理 (0x02xx) --------------------
    SYS_BRK             = 0x0200,
    SYS_MMAP            = 0x0201,
    SYS_MUNMAP          = 0x0202,
    SYS_MPROTECT        = 0x0203,

    // -------------------- 时间与时钟 (0x03xx) --------------------
    SYS_TIME            = 0x0300,
    SYS_GETTIMEOFDAY    = 0x0301,
    SYS_NANOSLEEP       = 0x0302,
    SYS_CLOCK_GETTIME   = 0x0303,

    // -------------------- 信号与进程控制 (0x04xx) --------------------
    SYS_KILL            = 0x0400,
    SYS_SIGACTION       = 0x0401,
    SYS_SIGPROCMASK     = 0x0402,
    SYS_SIGRETURN       = 0x0403,

    // -------------------- 系统信息 / 杂项 (0x05xx) --------------------
    SYS_UNAME           = 0x0500,
    SYS_GETRANDOM       = 0x0501,
    SYS_DEBUG_PRINT     = 0x0502,
    SYS_REBOOT          = 0x0503,
    SYS_POWEROFF        = 0x0504,

    // -------------------- 网络 BSD Socket API (0x06xx) --------------------
    // 符合 POSIX.1-2008 标准的 socket 系统调用
    SYS_SOCKET          = 0x0600,  // 创建套接字
    SYS_BIND            = 0x0601,  // 绑定地址
    SYS_LISTEN          = 0x0602,  // 监听连接
    SYS_ACCEPT          = 0x0603,  // 接受连接
    SYS_CONNECT         = 0x0604,  // 发起连接
    SYS_SEND            = 0x0605,  // 发送数据
    SYS_SENDTO          = 0x0606,  // 发送数据到指定地址
    SYS_RECV            = 0x0607,  // 接收数据
    SYS_RECVFROM        = 0x0608,  // 接收数据并获取源地址
    SYS_SHUTDOWN        = 0x0609,  // 关闭连接
    SYS_SETSOCKOPT      = 0x060A,  // 设置套接字选项
    SYS_GETSOCKOPT      = 0x060B,  // 获取套接字选项
    SYS_GETSOCKNAME     = 0x060C,  // 获取本地地址
    SYS_GETPEERNAME     = 0x060D,  // 获取对端地址
    SYS_SELECT          = 0x060E,  // I/O 多路复用
    SYS_FCNTL           = 0x060F,  // 文件控制

    SYS_MAX
};

// 初始化 syscall 表
void syscall_init(void);

/**
 * 系统调用处理函数（汇编调用）
 */
extern void syscall_handler(void);

#endif // _KERNEL_SYSCALL_H_
