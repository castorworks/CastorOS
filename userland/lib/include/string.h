#ifndef _USERLAND_LIB_STRING_H_
#define _USERLAND_LIB_STRING_H_

#include <types.h>

// 字符串函数声明
size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strchr(const char *str, int c);

// 内存函数声明
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);

// 字符分类函数
int isdigit(int c);
int isspace(int c);

// 转换函数
int atoi(const char *str);
char *itoa(int value, char *str, int base);
char *utoa(unsigned int value, char *str, int base);

#endif /* _USERLAND_LIB_STRING_H_ */

