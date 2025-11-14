#ifndef _USERLAND_LIB_STDIO_H_
#define _USERLAND_LIB_STDIO_H_

#include <syscall.h>
#include <types.h>
#include <libgcc_stub.h>

// 辅助函数声明（内部使用）
void num_to_str_dec(unsigned long long val, int is_signed, char *tmp, int *len);
void num_to_str_hex(unsigned long long val, int uppercase, char *tmp, int *len);
void num_to_str_oct(unsigned long long val, char *tmp, int *len);

// printf 函数声明
void printf(const char *format, ...);

// snprintf 函数声明
int snprintf(char *str, size_t size, const char *format, ...);

#endif /* _USERLAND_LIB_STDIO_H_ */

