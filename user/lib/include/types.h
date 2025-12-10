#ifndef _USERLAND_LIB_TYPES_H_
#define _USERLAND_LIB_TYPES_H_

/*
 * 用户空间与内核共享的类型定义
 * 支持架构: i686, x86_64, arm64
 */

/* 基础整数类型 */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* 架构相关的类型定义 */
#if defined(ARCH_X86_64) || defined(__x86_64__) || \
    defined(ARCH_ARM64) || defined(__aarch64__)
/* 64 位架构 */
typedef uint64_t size_t;
typedef int64_t  ssize_t;
typedef int64_t  off_t;
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;
#else
/* 32 位架构 (i686 默认) */
typedef uint32_t size_t;
typedef int32_t  ssize_t;
typedef int32_t  off_t;
typedef uint32_t uintptr_t;
typedef int32_t  intptr_t;
#endif

typedef uint32_t mode_t;
typedef uint32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;

#ifndef __cplusplus
#ifndef _BOOL_DEFINED
#define _BOOL_DEFINED
typedef _Bool bool;
#define true  1
#define false 0
#endif
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

#ifndef NULL
#define NULL ((void *)0)
#endif

#define DT_UNKNOWN       0
#define DT_FIFO          1
#define DT_CHR           2
#define DT_DIR           4
#define DT_BLK           6
#define DT_REG           8
#define DT_LNK           10
#define DT_SOCK          12

struct dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};

#define WNOHANG    1
#define WUNTRACED  2

#define WIFEXITED(status)    (((status) & 0xFF) == 0)
#define WEXITSTATUS(status)  (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status)  (((status) & 0xFF) != 0)
#define WTERMSIG(status)     ((status) & 0x7F)
#define WCOREDUMP(status)    (((status) & 0x80) != 0)

#ifndef _STRUCT_STAT_DEFINED
#define _STRUCT_STAT_DEFINED
struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
};
#endif

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFLNK  0120000

#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS

#define MAP_FAILED      ((void *)-1)

#ifndef _STRUCT_UTSNAME_DEFINED
#define _STRUCT_UTSNAME_DEFINED
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};
#endif

#endif /* _USERLAND_LIB_TYPES_H_ */
