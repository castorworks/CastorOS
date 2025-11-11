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

#endif // _TYPES_H_
