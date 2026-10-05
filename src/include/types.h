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

// 指针大小的整数类型和 size_t (架构相关)
// i686: 32-bit, x86_64/arm64: 64-bit
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;
typedef uint64_t size_t;
typedef int64_t  ssize_t;
typedef int64_t  off_t;    // POSIX: 文件偏移量类型（有符号）
#else
// 默认 i686 (32-bit)
typedef uint32_t uintptr_t;
typedef int32_t  intptr_t;
typedef uint32_t size_t;
typedef int32_t  ssize_t;
typedef int32_t  off_t;    // POSIX: 文件偏移量类型（有符号）
#endif

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

// 布尔类型：使用 C++ 内建 bool/true/false

// NULL 定义（新代码请直接使用 nullptr）
#ifndef NULL
#define NULL nullptr
#endif

#define PAGE_SIZE           4096
#define PAGE_SHIFT          12

/* 架构相关的内核虚拟地址基址和页掩码 */
#if defined(ARCH_X86_64)
    #define KERNEL_VIRTUAL_BASE 0xFFFF800000000000ULL
    #define PAGE_MASK           0xFFFFFFFFFFFFF000ULL
    #define VIRT_TO_PHYS(addr)  ((uintptr_t)(addr) - KERNEL_VIRTUAL_BASE)
    #define PHYS_TO_VIRT(addr)  ((uintptr_t)(addr) + KERNEL_VIRTUAL_BASE)
#elif defined(ARCH_ARM64)
    #define KERNEL_VIRTUAL_BASE 0xFFFF000000000000ULL
    #define PAGE_MASK           0xFFFFFFFFFFFFF000ULL
    #define VIRT_TO_PHYS(addr)  ((uintptr_t)(addr) - KERNEL_VIRTUAL_BASE)
    #define PHYS_TO_VIRT(addr)  ((uintptr_t)(addr) + KERNEL_VIRTUAL_BASE)
#else
    /* i686 (32-bit) */
    #define KERNEL_VIRTUAL_BASE 0x80000000
    #define PAGE_MASK           0xFFFFF000
    #define VIRT_TO_PHYS(addr)  ((uint32_t)(addr) - KERNEL_VIRTUAL_BASE)
    #define PHYS_TO_VIRT(addr)  ((uint32_t)(addr) + KERNEL_VIRTUAL_BASE)
#endif

#define PAGE_ALIGN_DOWN(addr) ((addr) & PAGE_MASK)
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

// 文件类型常量（用于 dirent.d_type）

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

// waitpid() 选项
#define WNOHANG    1  // 非阻塞等待：如果没有子进程退出，立即返回

// 进程退出状态宏
// 这些宏用于解析 wait/waitpid 返回的 status 值

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


/* ============================================================================
 * mmap 相关常量定义
 * ============================================================================ */

#define PROT_WRITE  0x2     // 可写

#define MAP_ANONYMOUS   0x20    // 匿名映射（不关联文件）


#endif // _TYPES_H_
