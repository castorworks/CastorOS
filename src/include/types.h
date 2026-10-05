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


// 进程状态（用于 proc_info，与内核 task_state_t 对应）

// waitpid() 选项
#define WNOHANG    1  // 非阻塞等待：如果没有子进程退出，立即返回

// 进程退出状态宏
// 这些宏用于解析 wait/waitpid 返回的 status 值


/* ============================================================================
 * stat 结构体 - 文件状态信息
 * ============================================================================ */

#ifndef _STRUCT_STAT_DEFINED
#define _STRUCT_STAT_DEFINED


#endif // _STRUCT_STAT_DEFINED


/* ============================================================================
 * mmap 相关常量定义
 * ============================================================================ */

#define PROT_WRITE  0x2     // 可写

#define MAP_ANONYMOUS   0x20    // 匿名映射（不关联文件）


#endif // _TYPES_H_
