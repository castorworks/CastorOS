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
 * 将整数转换为十六进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 11 字节）
 * @param uppercase 是否使用大写字母（A-F）
 */
void int32_to_hex(int32_t value, char *buffer, bool uppercase);

/**
 * 将无符号整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 12 字节）
 */
void uint32_to_str(uint32_t value, char *buffer);

/**
 * 将整数转换为十六进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 11 字节）
 * @param uppercase 是否使用大写字母（A-F）
 */
void uint32_to_hex(uint32_t value, char *buffer, bool uppercase);

/**
 * 将64位整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 21 字节）
 */
void int64_to_str(int64_t value, char *buffer);

/**
 * 将64位整数转换为十六进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 19 字节）
 * @param uppercase 是否使用大写字母（A-F）
 */
void int64_to_hex(int64_t value, char *buffer, bool uppercase);

/**
 * 将64位无符号整数转换为十进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 21 字节）
 */
void uint64_to_str(uint64_t value, char *buffer);

/**
 * 将64位整数转换为十六进制字符串
 * @param value 要转换的值
 * @param buffer 输出缓冲区（至少 19 字节）
 * @param uppercase 是否使用大写字母（A-F）
 */
void uint64_to_hex(uint64_t value, char *buffer, bool uppercase);

/**
 * 计算字符串长度
 * @param str 字符串
 * @return 字符串长度
 */
size_t strlen(const char *str);

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
 * 比较两个字符串（不区分大小写）
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @return 0 表示相等，< 0 表示 s1 < s2，> 0 表示 s1 > s2
 */
int strcasecmp(const char *s1, const char *s2);

/**
 * 复制字符串
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @return 目标缓冲区指针
 */
char *strcpy(char *dest, const char *src);

/**
 * 将源字符串追加到目标字符串的末尾
 * @param dest 目标字符串（必须足够大以容纳结果）
 * @param src 源字符串
 * @return 目标缓冲区指针
 */
char *strcat(char *dest, const char *src);

/**
 * 复制最多 n 个字符
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @param n 最多复制的字符数
 * @return 目标缓冲区指针
 */
char *strncpy(char *dest, const char *src, size_t n);

/**
 * 将字符串分解为标记序列
 * @param str 要分解的字符串（首次调用时传入），后续调用传入 NULL
 * @param delim 分隔符字符串
 * @return 指向下一个标记的指针，没有更多标记时返回 NULL
 * @note 该函数会修改原字符串，使用静态变量保存状态，非线程安全
 */
char *strtok(char *str, const char *delim);

/**
 * 在字符串中查找字符
 * @param str 要搜索的字符串
 * @param c 要查找的字符
 * @return 指向第一次出现的字符的指针，如果未找到则返回 NULL
 */
char *strchr(const char *str, int c);

/**
 * 在字符串中从右向左查找字符
 * @param str 要搜索的字符串
 * @param c 要查找的字符
 * @return 指向最后一次出现的字符的指针，如果未找到则返回 NULL
 */
char *strrchr(const char *str, int c);

/**
 * 将字符转换为大写
 * @param c 要转换的字符
 * @return 大写字符，如果不是字母则返回原字符
 */
int toupper(int c);

/**
 * 将字符转换为小写
 * @param c 要转换的字符
 * @return 小写字符，如果不是字母则返回原字符
 */
int tolower(int c);

/**
 * 设置内存区域
 * @param ptr 指向内存的指针
 * @param value 要设置的值
 * @param num 字节数
 * @return ptr
 */
void *memset(void *ptr, int value, size_t num);

/**
 * 复制内存区域
 * @param dest 目标地址
 * @param src 源地址
 * @param num 字节数
 * @return dest
 */
void *memcpy(void *dest, const void *src, size_t num);

/**
 * 比较内存区域
 * @param ptr1 第一个内存区域
 * @param ptr2 第二个内存区域
 * @param num 字节数
 * @return 0 表示相等，< 0 表示 ptr1 < ptr2，> 0 表示 ptr1 > ptr2
 */
int memcmp(const void *ptr1, const void *ptr2, size_t num);

/**
 * 移动内存区域（支持重叠区域）
 * @param dest 目标地址
 * @param src 源地址
 * @param num 字节数
 * @return dest
 */
void *memmove(void *dest, const void *src, size_t num);

/**
 * 格式化字符串输出（带长度限制）
 * @param str 目标缓冲区
 * @param size 缓冲区大小（包括 '\0'）
 * @param format 格式字符串
 * @param ... 可变参数
 * @return 写入的字符数（不包括 '\0'），如果超过 size 则返回应写入的字符数
 * 
 * 支持的格式说明符：
 * - %d, %i: 有符号十进制整数
 * - %u: 无符号十进制整数
 * - %x: 无符号十六进制整数（小写）
 * - %X: 无符号十六进制整数（大写）
 * - %p: 指针（十六进制）
 * - %s: 字符串
 * - %c: 字符
 * - %%: 百分号
 */
int snprintf(char *str, size_t size, const char *format, ...);

#endif /* _LIB_STRING_H_ */

