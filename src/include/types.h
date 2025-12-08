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
typedef int32_t  off_t;    // POSIX: 文件偏移量类型（有符号）

// 指针大小的整数类型 (架构相关)
// i686: 32-bit, x86_64/arm64: 64-bit
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;
#else
// 默认 i686 (32-bit)
typedef uint32_t uintptr_t;
typedef int32_t  intptr_t;
#endif

#define UINT32_MAX ((uint32_t)0xFFFFFFFF)
#define INT32_MAX ((int32_t)0x7FFFFFFF)

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

/* ============================================================================
 * stat 结构体 - 文件状态信息
 * ============================================================================ */

#ifndef _STRUCT_STAT_DEFINED
#define _STRUCT_STAT_DEFINED

struct stat {
    uint32_t st_dev;      // 设备 ID
    uint32_t st_ino;      // inode 编号
    uint32_t st_mode;     // 文件类型和权限
    uint32_t st_nlink;    // 硬链接数
    uint32_t st_uid;      // 所有者用户 ID
    uint32_t st_gid;      // 所有者组 ID
    uint32_t st_rdev;     // 设备类型（如果是特殊文件）
    uint32_t st_size;     // 文件大小（字节）
    uint32_t st_blksize;  // 文件系统 I/O 块大小
    uint32_t st_blocks;   // 分配的 512B 块数
    uint32_t st_atime;    // 最后访问时间
    uint32_t st_mtime;    // 最后修改时间
    uint32_t st_ctime;    // 最后状态改变时间
};

#endif // _STRUCT_STAT_DEFINED

/* 文件类型掩码（st_mode 字段） */
#define S_IFMT   0170000   // 文件类型掩码
#define S_IFREG  0100000   // 普通文件
#define S_IFDIR  0040000   // 目录
#define S_IFCHR  0020000   // 字符设备
#define S_IFBLK  0060000   // 块设备
#define S_IFIFO  0010000   // FIFO（管道）
#define S_IFLNK  0120000   // 符号链接

/* 权限位 */
#define S_IRUSR  0400      // 所有者读
#define S_IWUSR  0200      // 所有者写
#define S_IXUSR  0100      // 所有者执行
#define S_IRGRP  0040      // 组读
#define S_IWGRP  0020      // 组写
#define S_IXGRP  0010      // 组执行
#define S_IROTH  0004      // 其他用户读
#define S_IWOTH  0002      // 其他用户写
#define S_IXOTH  0001      // 其他用户执行

/* 类型检查宏 */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)   // 是否为普通文件
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)   // 是否为目录
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)   // 是否为字符设备
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)   // 是否为块设备
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)   // 是否为 FIFO
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)   // 是否为符号链接

/* ============================================================================
 * mmap 相关常量定义
 * ============================================================================ */

/* 内存保护标志（prot 参数） */
#define PROT_NONE   0x0     // 不可访问
#define PROT_READ   0x1     // 可读
#define PROT_WRITE  0x2     // 可写
#define PROT_EXEC   0x4     // 可执行

/* 映射标志（flags 参数） */
#define MAP_SHARED      0x01    // 共享映射（修改可见于其他进程）
#define MAP_PRIVATE     0x02    // 私有映射（写时复制）
#define MAP_FIXED       0x10    // 使用指定地址（不推荐）
#define MAP_ANONYMOUS   0x20    // 匿名映射（不关联文件）
#define MAP_ANON        MAP_ANONYMOUS  // 别名

/* 映射失败返回值 */
#define MAP_FAILED      ((void *)-1)

#endif // _TYPES_H_
