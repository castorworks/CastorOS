#ifndef _TYPES_H_
#define _TYPES_H_

// 基本类型定义
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

#define UINT32_MAX ((uint32_t)0xFFFFFFFF)

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

// 布尔类型
#ifndef __cplusplus
typedef unsigned char bool;
#define true  1
#define false 0
#endif

// NULL 定义
#ifndef NULL
#define NULL ((void *)0)
#endif

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_MASK           0xFFFFF000
#define KERNEL_VIRTUAL_BASE 0x80000000

#define VIRT_TO_PHYS(addr)  ((uint32_t)(addr) - KERNEL_VIRTUAL_BASE)
#define PHYS_TO_VIRT(addr)  ((uint32_t)(addr) + KERNEL_VIRTUAL_BASE)
#define PAGE_ALIGN_DOWN(addr) ((addr) & PAGE_MASK)
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

// 文件类型常量（用于 dirent.d_type）
#define DT_UNKNOWN       0   // 未知类型
#define DT_FIFO          1   // 命名管道
#define DT_CHR           2   // 字符设备
#define DT_DIR            4   // 目录
#define DT_BLK            6   // 块设备
#define DT_REG            8   // 常规文件
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

// 进程状态（用于 proc_info，与内核 task_state_t 对应）
#define PROC_STATE_UNUSED    0   // 未使用
#define PROC_STATE_READY     1   // 就绪
#define PROC_STATE_RUNNING   2   // 运行中
#define PROC_STATE_BLOCKED   3   // 阻塞
#define PROC_STATE_ZOMBIE    4   // 僵尸（已退出，等待回收）
#define PROC_STATE_TERMINATED 5  // 已终止

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

// 进程信息结构（用于系统调用，用户态和内核态共享）
struct proc_info {
    uint32_t pid;           // 进程 ID
    char name[32];          // 进程名称（以 null 结尾）
    uint8_t state;          // 进程状态（PROC_STATE_*）
    uint32_t priority;      // 优先级
    uint64_t runtime_ms;   // 总运行时间（毫秒）
} __attribute__((packed));

#endif // _TYPES_H_
