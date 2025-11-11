// ============================================================================
// kprintf.c - 内核格式化输出
// ============================================================================

#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <stdarg.h>

void kputchar(char c) {
    serial_putchar(c);
    vga_putchar(c);
}

void kprint(const char *msg) {
    serial_print(msg);
    vga_print(msg);
}

/**
 * 打印字符串（内部辅助函数）
 */
static void print_string(const char *str) {
    while (*str) {
        kputchar(*str++);
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
        kputchar('0');
        kputchar('x');
        str += 2;
        len -= 2;
        // 重新计算填充数量（宽度应该减去 "0x" 的长度）
        pad_count = width > len + 2 ? width - len - 2 : 0;
    }
    
    // 对于负数，先输出负号，然后零填充
    bool has_minus = (str[0] == '-');
    if (has_minus && zero_pad && !left_align) {
        kputchar('-');
        str++;
        len--;
    }
    
    // 左对齐：先输出内容，再填充空格
    if (left_align) {
        print_string(str);
        for (int i = 0; i < pad_count; i++) {
            kputchar(' ');
        }
    } else {
        // 右对齐：先填充，再输出内容
        char pad_char = zero_pad ? '0' : ' ';
        for (int i = 0; i < pad_count; i++) {
            kputchar(pad_char);
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
 * 打印十六进制数（内部辅助函数，带智能宽度处理）
 */
static void print_hex(uint32_t value, bool uppercase, int width, bool zero_pad) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    
    // 总是输出 "0x" 前缀
    kputchar('0');
    kputchar('x');
    
    // 计算需要的数字位数
    int digit_width = (width > 2) ? width - 2 : 8;  // 默认 8 位
    if (digit_width > 8) digit_width = 8;  // 最多 8 位
    
    // 生成十六进制数字
    char buffer[9];
    for (int i = 7; i >= 0; i--) {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }
    buffer[8] = '\0';
    
    // 输出填充的零（如果需要）
    int start_pos = 8 - digit_width;
    if (zero_pad && width > 2) {
        // 从计算的起始位置开始输出
        for (int i = start_pos; i < 8; i++) {
            kputchar(buffer[i]);
        }
    } else {
        // 不使用零填充，找到第一个非零数字
        int first_non_zero = 0;
        while (first_non_zero < 7 && buffer[first_non_zero] == '0') {
            first_non_zero++;
        }
        // 但至少保证 digit_width 位
        if (8 - first_non_zero < digit_width) {
            first_non_zero = 8 - digit_width;
        }
        for (int i = first_non_zero; i < 8; i++) {
            kputchar(buffer[i]);
        }
    }
}

/**
 * 打印 64 位十六进制数（内部辅助函数，带智能宽度处理）
 */
static void print_hex64(uint64_t value, bool uppercase, int width, bool zero_pad) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    
    // 总是输出 "0x" 前缀
    kputchar('0');
    kputchar('x');
    
    // 计算需要的数字位数
    int digit_width = (width > 2) ? width - 2 : 16;  // 默认 16 位
    if (digit_width > 16) digit_width = 16;  // 最多 16 位
    
    // 生成十六进制数字
    char buffer[17];
    for (int i = 15; i >= 0; i--) {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }
    buffer[16] = '\0';
    
    // 输出填充的零（如果需要）
    int start_pos = 16 - digit_width;
    if (zero_pad && width > 2) {
        // 从计算的起始位置开始输出
        for (int i = start_pos; i < 16; i++) {
            kputchar(buffer[i]);
        }
    } else {
        // 不使用零填充，找到第一个非零数字
        int first_non_zero = 0;
        while (first_non_zero < 15 && buffer[first_non_zero] == '0') {
            first_non_zero++;
        }
        // 但至少保证 digit_width 位
        if (16 - first_non_zero < digit_width) {
            first_non_zero = 16 - digit_width;
        }
        for (int i = first_non_zero; i < 16; i++) {
            kputchar(buffer[i]);
        }
    }
}

void vkprintf(const char *fmt, va_list args) {
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
                    kputchar(c);
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
                case 'p': {  // 指针
                    void *ptr = va_arg(args, void *);
                    print_hex((uint32_t)ptr, false, width, zero_pad);
                    break;
                }
                case '%': {  // 百分号字面值
                    kputchar('%');
                    break;
                }
                default: {  // 未知格式说明符
                    kputchar('%');
                    kputchar(*fmt);
                    break;
                }
            }
            fmt++;
        } else {
            kputchar(*fmt++);
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
}

