#ifndef _LIB_KPRINTF_H_
#define _LIB_KPRINTF_H_

#include <types.h>
#include <stdarg.h>

/**
 * 内核打印库
 * 
 * 提供类似标准库 printf 的格式化输出功能
 * 支持的格式说明符：
 *   %s - 字符串
 *   %c - 字符
 *   %d - 十进制整数 (int32_t)
 *   %u - 无符号十进制整数 (uint32_t)
 *   %x - 十六进制整数（小写）(uint32_t)
 *   %X - 十六进制整数（大写）(uint32_t)
 *   %lld - 64 位十进制整数 (int64_t)
 *   %llu - 64 位无符号十进制整数 (uint64_t)
 *   %llx - 64 位十六进制整数（小写）(uint64_t)
 *   %llX - 64 位十六进制整数（大写）(uint64_t)
 *   %p - 指针地址（十六进制）
 *   %% - 百分号字面值
 * 
 * 支持的修饰符：
 *   - - 左对齐标志，例如 %-12s 表示左对齐 12 字符宽度的字符串
 *   0 - 零填充标志，例如 %08x 表示 8 位宽度零填充的十六进制数
 *   宽度 - 指定最小字段宽度，例如 %5d 表示至少 5 字符宽度
 *   ll - 长度修饰符，用于 64 位整数
 */

/**
 * 格式化输出到串口和 VGA
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * 格式化输出到串口和 VGA（va_list 版本）
 * @param fmt 格式字符串
 * @param args 可变参数列表
 */
void vkprintf(const char *fmt, va_list args);

/**
 * 基本输出函数（同时输出到串口和 VGA）
 * @param msg 要输出的字符串
 */
void kprint(const char *msg);

/**
 * 输出单个字符
 * @param c 要输出的字符
 */
void kputchar(char c);

/**
 * 格式化输出到字符串缓冲区
 * @param str 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 写入的字符数（不包括 \0）
 */
int ksnprintf(char *str, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif /* _LIB_KPRINTF_H_ */
