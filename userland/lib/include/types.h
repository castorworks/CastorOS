#ifndef _USERLAND_LIB_TYPES_H_
#define _USERLAND_LIB_TYPES_H_

/*
 * 用户空间与内核共享的类型定义
 * 注意：这里仅包含 ABI 相关的类型，不包含内核私有类型
 */

/* 基础整数类型（与 types.h 保持一致） */
#ifndef _TYPES_H_
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint32_t size_t;
typedef int32_t  ssize_t;
#endif

#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef uint32_t time_t;
#endif

#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;
    uint32_t tv_nsec;
};
#endif

// NULL 定义
#ifndef NULL
#define NULL ((void *)0)
#endif

// 文件类型常量（用于 dirent.d_type）
#define DT_UNKNOWN       0   // 未知类型
#define DT_FIFO          1   // 命名管道
#define DT_CHR           2   // 字符设备
#define DT_DIR            4   // 目录
#define DT_BLK           6   // 块设备
#define DT_REG           8   // 常规文件
#define DT_LNK           10   // 符号链接
#define DT_SOCK          12   // 套接字

// 目录项结构（POSIX 标准）
// 参考：POSIX.1-2008 <dirent.h>
struct dirent {
    uint32_t d_ino;         // inode 编号
    uint32_t d_off;         // 到下一个 dirent 的偏移量（文件系统相关）
    uint16_t d_reclen;      // 此记录的长度（sizeof(struct dirent)）
    uint8_t  d_type;        // 文件类型（DT_* 常量）
    char     d_name[256];    // 文件名（以 null 结尾，最大 255 字符）
};

// waitpid() 选项
#define WNOHANG    1  // 非阻塞等待：如果没有子进程退出，立即返回
#define WUNTRACED  2  // 也报告已停止的子进程状态（暂未实现）

// 进程退出状态宏
// 这些宏用于解析 wait/waitpid 返回的 status 值
#define WIFEXITED(status)    (((status) & 0xFF) == 0)                  // 进程正常退出
#define WEXITSTATUS(status)  (((status) >> 8) & 0xFF)                  // 获取退出码
#define WIFSIGNALED(status)  (((status) & 0xFF) != 0)                  // 进程被信号终止
#define WTERMSIG(status)     ((status) & 0x7F)                         // 获取终止信号
#define WCOREDUMP(status)    (((status) & 0x80) != 0)                  // 是否产生 core dump（暂未实现）

#endif /* _USERLAND_LIB_TYPES_H_ */
