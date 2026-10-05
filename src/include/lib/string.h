#ifndef _LIB_STRING_H_
#define _LIB_STRING_H_

#include <types.h>

/**
 * 字符串和数字转换工具函数
 */

 /**
 * 将有符号整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 12 字节）
 */
void int32_to_str(int32_t value, char *buffer);

/**
 * 将无符号整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 12 字节）
 */
void uint32_to_str(uint32_t value, char *buffer);

/**
 * 将64位整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 21 字节）
 */
void int64_to_str(int64_t value, char *buffer);

/**
 * 将64位无符号整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 21 字节）
 */
void uint64_to_str(uint64_t value, char *buffer);

/**
 * 计算字符串长度
 * @param str 字符串
 * @return 字符串长度
 */
size_t strlen(const char *str);

/** 字符串长度，最多看 max 个字节（字符串可能没有结尾的 NUL） */
size_t strnlen(const char *str, size_t max);

/**
 * 比较两个字符串
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @return 0 表示相等，< 0 表示 s1 < s2，> 0 表示 s1 > s2
 */
int strcmp(const char *s1, const char *s2);

/**
 * 比较两个字符串的前 n 个字符
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @param n 要比较的最大字符数
 * @return 0 表示相等，< 0 表示 s1 < s2，> 0 表示 s1 > s2
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * 复制字符串
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @return 目标缓冲区指针
 */
char *strcpy(char *dest, const char *src);

/**
 * 复制最多 n 个字符
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @param n 最多复制的字符数
 * @return 目标缓冲区指针
 */
char *strncpy(char *dest, const char *src, size_t n);

/**
 * 设置内存区域
 * @param ptr 指向内存的指针
 * @param value 要设置的值
 * @param num 字节数
 * @return ptr
 */
extern "C" void *memset(void *ptr, int value, size_t num);

/**
 * 复制内存区域
 * @param dest 目标地址
 * @param src 源地址
 * @param num 字节数
 * @return dest
 */
extern "C" void *memcpy(void *dest, const void *src, size_t num);

/**
 * 比较内存区域
 * @param ptr1 第一个内存区域
 * @param ptr2 第二个内存区域
 * @param num 字节数
 * @return 0 表示相等，< 0 表示 ptr1 < ptr2，> 0 表示 ptr1 > ptr2
 */
extern "C" int memcmp(const void *ptr1, const void *ptr2, size_t num);

/**
 * 移动内存区域（支持重叠区域）
 * @param dest 目标地址
 * @param src 源地址
 * @param num 字节数
 * @return dest
 */
extern "C" void *memmove(void *dest, const void *src, size_t num);

#endif /* _LIB_STRING_H_ */

