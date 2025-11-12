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

#endif // _TYPES_H_
