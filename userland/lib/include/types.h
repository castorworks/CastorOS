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

#endif /* _USERLAND_LIB_TYPES_H_ */
