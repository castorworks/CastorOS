// ============================================================================
// kprintf.c - 内核格式化输出
// ============================================================================

#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/serial.h>
#include <kernel/interrupt.h>
#include <stdarg.h>

/**
 * 内部字符输出函数
 */
static void output_char(char c) {
    drivers::Serial::putchar(c);
}

/**
 * 内部字符串输出函数
 */
static void output_string(const char *msg) {
    drivers::Serial::print(msg);
}

/* 中断处理函数也会打日志。下面每个公共输出入口都整体关中断，
 * 保证一次输出不会被另一次输出从中间插入。 */


void kputchar(char c) {
    kernel::InterruptGuard guard;
    output_char(c);
}

void kprint(const char *msg) {
    kernel::InterruptGuard guard;
    output_string(msg);
}

/**
 * 打印字符串（内部辅助函数）
 */
static void print_string(const char *str) {
    while (*str) {
        output_char(*str++);
    }
}

/**
 * 获取字符串长度（内部辅助函数）
 */
static int str_len(const char *str) {
    int len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

/**
 * 打印格式化的字符串（带宽度和填充）
 */
static void print_formatted(const char *str, int width, bool zero_pad, bool left_align, bool is_hex) {
    int len = str_len(str);
    int pad_count = width > len ? width - len : 0;
    
    // 对于十六进制数，先输出 "0x" 前缀，然后零填充
    bool has_hex_prefix = (is_hex && len >= 2 && str[0] == '0' && str[1] == 'x');
    if (has_hex_prefix && zero_pad && width > 0 && !left_align) {
        output_char('0');
        output_char('x');
        str += 2;
        len -= 2;
        // 重新计算填充数量（宽度应该减去 "0x" 的长度）
        pad_count = width > len + 2 ? width - len - 2 : 0;
    }
    
    // 对于负数，先输出负号，然后零填充
    bool has_minus = (str[0] == '-');
    if (has_minus && zero_pad && !left_align) {
        output_char('-');
        str++;
        len--;
    }
    
    // 左对齐：先输出内容，再填充空格
    if (left_align) {
        print_string(str);
        for (int i = 0; i < pad_count; i++) {
            output_char(' ');
        }
    } else {
        // 右对齐：先填充，再输出内容
        char pad_char = zero_pad ? '0' : ' ';
        for (int i = 0; i < pad_count; i++) {
            output_char(pad_char);
        }
        print_string(str);
    }
}

/**
 * 打印整数（内部辅助函数）
 */
static void print_int(int32_t value, int width, bool zero_pad, bool left_align) {
    char buffer[12];
    int32_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false);
}

/**
 * 打印无符号整数（内部辅助函数）
 */
static void print_uint(uint32_t value, int width, bool zero_pad, bool left_align) {
    char buffer[12];
    uint32_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false);
}

/**
 * 打印 64 位整数（内部辅助函数）
 */
static void print_int64(int64_t value, int width, bool zero_pad, bool left_align) {
    char buffer[21];
    int64_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false);
}

/**
 * 打印 64 位无符号整数（内部辅助函数）
 */
static void print_uint64(uint64_t value, int width, bool zero_pad, bool left_align) {
    char buffer[21];
    uint64_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false);
}

/**
 * 打印十六进制数（内部辅助函数，标准 printf 行为）
 * 注意：%x 不输出 0x 前缀，只有 %p 才输出
 */
static void print_hex(uint32_t value, bool uppercase, int width, bool zero_pad) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    
    // 生成十六进制数字（从右到左）
    char buffer[9];
    int len = 0;
    
    if (value == 0) {
        buffer[0] = '0';
        len = 1;
    } else {
        uint32_t tmp = value;
        while (tmp > 0) {
            buffer[len++] = digits[tmp & 0xF];
            tmp >>= 4;
        }
    }
    
    // 计算需要的填充
    int pad_count = (width > len) ? (width - len) : 0;
    
    // 输出填充（如果需要）
    char pad_char = zero_pad ? '0' : ' ';
    for (int i = 0; i < pad_count; i++) {
        output_char(pad_char);
    }
    
    // 逆序输出数字
    for (int i = len - 1; i >= 0; i--) {
        output_char(buffer[i]);
    }
}

/**
 * 打印 64 位十六进制数（内部辅助函数，标准 printf 行为）
 * 注意：%llx 不输出 0x 前缀
 */
static void print_hex64(uint64_t value, bool uppercase, int width, bool zero_pad) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    
    // 生成十六进制数字（从右到左）
    char buffer[17];
    int len = 0;
    
    if (value == 0) {
        buffer[0] = '0';
        len = 1;
    } else {
        uint64_t tmp = value;
        while (tmp > 0) {
            buffer[len++] = digits[tmp & 0xF];
            tmp >>= 4;
        }
    }
    
    // 计算需要的填充
    int pad_count = (width > len) ? (width - len) : 0;
    
    // 输出填充（如果需要）
    char pad_char = zero_pad ? '0' : ' ';
    for (int i = 0; i < pad_count; i++) {
        output_char(pad_char);
    }
    
    // 逆序输出数字
    for (int i = len - 1; i >= 0; i--) {
        output_char(buffer[i]);
    }
}

/**
 * 内部格式化输出函数
 */
static void vkprintf_internal(const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            
            // 解析标志位：左对齐、零填充
            bool left_align = false;
            bool zero_pad = false;
            int width = 0;
            
            // 检查左对齐标志
            if (*fmt == '-') {
                left_align = true;
                fmt++;
            }
            
            // 检查零填充标志（左对齐时忽略零填充）
            if (*fmt == '0' && !left_align) {
                zero_pad = true;
                fmt++;
            }
            
            // 解析宽度
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            // 解析长度修饰符
            bool is_long_long = false;
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    is_long_long = true;
                    fmt++;
                }
            }
            
            switch (*fmt) {
                case 's': {  // 字符串
                    const char *s = va_arg(args, const char *);
                    if (!s) {
                        s = "(null)";
                    }
                    print_formatted(s, width, false, left_align, false);
                    break;
                }
                case 'c': {  // 字符
                    char c = (char)va_arg(args, int);
                    output_char(c);
                    break;
                }
                case 'd': {  // 有符号十进制整数
                    if (is_long_long) {
                        int64_t val = va_arg(args, int64_t);
                        print_int64(val, width, zero_pad, left_align);
                    } else {
                        int val = va_arg(args, int);
                        print_int(val, width, zero_pad, left_align);
                    }
                    break;
                }
                case 'u': {  // 无符号十进制整数
                    if (is_long_long) {
                        uint64_t val = va_arg(args, uint64_t);
                        print_uint64(val, width, zero_pad, left_align);
                    } else {
                        uint32_t val = va_arg(args, uint32_t);
                        print_uint(val, width, zero_pad, left_align);
                    }
                    break;
                }
                case 'x': {  // 十六进制（小写）
                    if (is_long_long) {
                        uint64_t val = va_arg(args, uint64_t);
                        print_hex64(val, false, width, zero_pad);
                    } else {
                        uint32_t val = va_arg(args, uint32_t);
                        print_hex(val, false, width, zero_pad);
                    }
                    break;
                }
                case 'X': {  // 十六进制（大写）
                    if (is_long_long) {
                        uint64_t val = va_arg(args, uint64_t);
                        print_hex64(val, true, width, zero_pad);
                    } else {
                        uint32_t val = va_arg(args, uint32_t);
                        print_hex(val, true, width, zero_pad);
                    }
                    break;
                }
                case 'p': {  // 指针（带 0x 前缀）
                    void *ptr = va_arg(args, void *);
                    output_char('0');
                    output_char('x');
                    // 指针默认 8 位十六进制，零填充
                    int ptr_width = (width > 2) ? (width - 2) : 8;
                    print_hex((uint32_t)(uintptr_t)ptr, false, ptr_width, true);
                    break;
                }
                case '%': {  // 百分号字面值
                    output_char('%');
                    break;
                }
                default: {  // 未知格式说明符
                    output_char('%');
                    output_char(*fmt);
                    break;
                }
            }
            fmt++;
        } else {
            output_char(*fmt++);
        }
    }
}

/* ============================================================================
 * 公共 API - 格式化输出函数
 * ============================================================================ */

void vkprintf(const char *fmt, va_list args) {
    kernel::InterruptGuard guard;
    vkprintf_internal(fmt, args);
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
}

/* 控制台颜色：串口控制台不支持，保留为空操作 */

