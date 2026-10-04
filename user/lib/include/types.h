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
#define NULL nullptr
#endif

#define WNOHANG    1
#define WUNTRACED  2

#define WIFEXITED(status)    (((status) & 0xFF) == 0)
#define WEXITSTATUS(status)  (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status)  (((status) & 0xFF) != 0)
#define WTERMSIG(status)     ((status) & 0x7F)
#define WCOREDUMP(status)    (((status) & 0x80) != 0)

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

#endif /* _USERLAND_LIB_TYPES_H_ */
