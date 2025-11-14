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

/**
 * 向缓冲区添加填充字符（辅助函数）
 */
static size_t add_padding(char *str, size_t size, size_t pos, char pad_char, int count) {
    for (int i = 0; i < count && pos < size - 1; i++) {
        str[pos++] = pad_char;
    }
    return pos;
}

/**
 * 格式化输出到字符串缓冲区（内部辅助函数，用于 ksnprintf）
 */
static int vsnprintf_internal(char *str, size_t size, const char *fmt, va_list args) {
    if (!str || size == 0) {
        return 0;
    }
    
    size_t pos = 0;
    
    while (*fmt && pos < size - 1) {
        if (*fmt != '%') {
            // 普通字符
            str[pos++] = *fmt++;
            continue;
        }
        
        fmt++; // 跳过 '%'
        
        // 解析标志位和宽度
        bool left_align = false;
        bool zero_pad = false;
        int width = 0;
        bool is_long_long = false;
        
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
        bool is_long = false;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                is_long_long = true;
                fmt++;
            } else {
                is_long = true;  // 单个 l 表示 long
                // 注意：这里不递增 fmt，因为 fmt 已经指向格式字符（如 'u'）
            }
        }
        
        // 格式化值到临时缓冲区
        char value_buf[64];
        size_t value_len = 0;
        bool is_negative = false;
        
        switch (*fmt) {
            case 's': {  // 字符串
                const char *s = va_arg(args, const char *);
                if (!s) {
                    s = "(null)";
                }
                value_len = strlen(s);
                size_t copy_len = (value_len < sizeof(value_buf) - 1) ? value_len : (sizeof(value_buf) - 1);
                memcpy(value_buf, s, copy_len);
                value_buf[copy_len] = '\0';
                value_len = copy_len;
                break;
            }
            case 'c': {  // 字符
                char c = (char)va_arg(args, int);
                value_buf[0] = c;
                value_buf[1] = '\0';
                value_len = 1;
                break;
            }
            case 'd': {  // 有符号十进制整数
                if (is_long_long) {
                    int64_t val = va_arg(args, int64_t);
                    if (val < 0) {
                        is_negative = true;
                        val = -val;
                    }
                    int64_to_str(val, value_buf);
                } else if (is_long) {
                    // long 在 32 位系统上通常是 32 位，但 va_arg 需要精确类型
                    long val = va_arg(args, long);
                    if (val < 0) {
                        is_negative = true;
                        val = -val;
                    }
                    // 转换为 int32_t 以便使用现有的转换函数
                    int32_t val32 = (int32_t)val;
                    int32_to_str(val32, value_buf);
                } else {
                    int32_t val = va_arg(args, int32_t);
                    if (val < 0) {
                        is_negative = true;
                        val = -val;
                    }
                    int32_to_str(val, value_buf);
                }
                value_len = strlen(value_buf);
                break;
            }
            case 'u': {  // 无符号十进制整数
                if (is_long_long) {
                    uint64_t val = va_arg(args, uint64_t);
                    uint64_to_str(val, value_buf);
                } else if (is_long) {
                    // long 在 32 位系统上通常是 32 位，但 va_arg 需要精确类型
                    unsigned long val = va_arg(args, unsigned long);
                    // 转换为 uint32_t 以便使用现有的转换函数
                    uint32_t val32 = (uint32_t)val;
                    uint32_to_str(val32, value_buf);
                } else {
                    uint32_t val = va_arg(args, uint32_t);
                    uint32_to_str(val, value_buf);
                }
                value_len = strlen(value_buf);
                break;
            }
            case 'x': {  // 十六进制（小写）
                if (is_long_long) {
                    uint64_t val = va_arg(args, uint64_t);
                    uint64_to_hex(val, value_buf, false);
                } else if (is_long) {
                    unsigned long val = va_arg(args, unsigned long);
                    uint32_t val32 = (uint32_t)val;
                    uint32_to_hex(val32, value_buf, false);
                } else {
                    uint32_t val = va_arg(args, uint32_t);
                    uint32_to_hex(val, value_buf, false);
                }
                value_len = strlen(value_buf);
                break;
            }
            case 'X': {  // 十六进制（大写）
                if (is_long_long) {
                    uint64_t val = va_arg(args, uint64_t);
                    uint64_to_hex(val, value_buf, true);
                } else if (is_long) {
                    unsigned long val = va_arg(args, unsigned long);
                    uint32_t val32 = (uint32_t)val;
                    uint32_to_hex(val32, value_buf, true);
                } else {
                    uint32_t val = va_arg(args, uint32_t);
                    uint32_to_hex(val, value_buf, true);
                }
                value_len = strlen(value_buf);
                break;
            }
            case 'p': {  // 指针
                void *ptr = va_arg(args, void *);
                uint32_to_hex((uint32_t)ptr, value_buf, false);
                value_len = strlen(value_buf);
                break;
            }
            case '%': {  // 百分号字面值
                value_buf[0] = '%';
                value_buf[1] = '\0';
                value_len = 1;
                break;
            }
            default: {  // 未知格式说明符
                // 如果之前有长度修饰符，需要包含它
                if (is_long || is_long_long) {
                    value_buf[0] = '%';
                    if (is_long_long) {
                        value_buf[1] = 'l';
                        value_buf[2] = 'l';
                        value_buf[3] = *fmt;
                        value_buf[4] = '\0';
                        value_len = 4;
                    } else {
                        value_buf[1] = 'l';
                        value_buf[2] = *fmt;
                        value_buf[3] = '\0';
                        value_len = 3;
                    }
                } else {
                    value_buf[0] = '%';
                    value_buf[1] = *fmt;
                    value_buf[2] = '\0';
                    value_len = 2;
                }
                break;
            }
        }
        
        // 计算需要填充的字符数
        int pad_count = (width > (int)value_len) ? (width - (int)value_len) : 0;
        if (is_negative) {
            pad_count--;  // 负号占用一个字符位置
        }
        if (pad_count < 0) {
            pad_count = 0;
        }
        
        // 应用格式化和填充
        if (left_align) {
            // 左对齐：先输出值，再填充空格
            if (is_negative && pos < size - 1) {
                str[pos++] = '-';
            }
            // 输出值
            size_t copy_len = (pos + value_len < size - 1) ? value_len : (size - 1 - pos);
            memcpy(str + pos, value_buf, copy_len);
            pos += copy_len;
            // 填充空格
            pos = add_padding(str, size, pos, ' ', pad_count);
        } else {
            // 右对齐：先填充，再输出值
            if (zero_pad && is_negative) {
                // 零填充时，负号在填充之前
                if (pos < size - 1) {
                    str[pos++] = '-';
                }
                pos = add_padding(str, size, pos, '0', pad_count);
            } else {
                // 空格填充，或零填充但无负号
                char pad_char = zero_pad ? '0' : ' ';
                pos = add_padding(str, size, pos, pad_char, pad_count);
                if (is_negative && pos < size - 1) {
                    str[pos++] = '-';
                }
            }
            // 输出值
            size_t copy_len = (pos + value_len < size - 1) ? value_len : (size - 1 - pos);
            memcpy(str + pos, value_buf, copy_len);
            pos += copy_len;
        }
        
        fmt++;
    }
    
    // 添加结尾的 '\0'
    if (pos < size) {
        str[pos] = '\0';
    } else {
        str[size - 1] = '\0';
    }
    
    return (int)pos;
}

/**
 * 格式化输出到字符串缓冲区
 */
int ksnprintf(char *str, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf_internal(str, size, fmt, args);
    va_end(args);
    return result;
}

