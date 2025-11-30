// ============================================================================
// kprintf.c - 内核格式化输出
// ============================================================================

#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <stdarg.h>

/* 输出目标标志 */
typedef enum {
    OUTPUT_SERIAL = 0x01,
    OUTPUT_VGA   = 0x02,
    OUTPUT_BOTH  = OUTPUT_SERIAL | OUTPUT_VGA
} output_target_t;


/**
 * 内部字符输出函数（根据目标输出）
 * 在图形模式下自动使用帧缓冲终端
 */
static void output_char(char c, output_target_t target) {
    if (target & OUTPUT_SERIAL) {
        serial_putchar(c);
    }
    if (target & OUTPUT_VGA) {
        // 优先使用图形终端，回退到 VGA 文本模式
        if (fb_is_initialized()) {
            fb_terminal_putchar(c);
        } else {
            vga_putchar(c);
        }
    }
}

/**
 * 内部字符串输出函数（根据目标输出）
 * 在图形模式下自动使用帧缓冲终端
 */
static void output_string(const char *msg, output_target_t target) {
    if (target & OUTPUT_SERIAL) {
        serial_print(msg);
    }
    if (target & OUTPUT_VGA) {
        // 优先使用图形终端，回退到 VGA 文本模式
        if (fb_is_initialized()) {
            fb_terminal_write(msg);
        } else {
            vga_print(msg);
        }
    }
}

/* ============================================================================
 * 公共 API - 同时输出到 serial 和 VGA（向后兼容）
 * ============================================================================ */

void kputchar(char c) {
    output_char(c, OUTPUT_BOTH);
}

void kprint(const char *msg) {
    output_string(msg, OUTPUT_BOTH);
}

/* ============================================================================
 * 公共 API - 仅输出到 serial
 * ============================================================================ */

void kputchar_serial(char c) {
    output_char(c, OUTPUT_SERIAL);
}

void kprint_serial(const char *msg) {
    output_string(msg, OUTPUT_SERIAL);
}

/* ============================================================================
 * 公共 API - 仅输出到 VGA
 * ============================================================================ */

void kputchar_vga(char c) {
    output_char(c, OUTPUT_VGA);
}

void kprint_vga(const char *msg) {
    output_string(msg, OUTPUT_VGA);
}

/**
 * 打印字符串（内部辅助函数，根据目标输出）
 */
static void print_string(const char *str, output_target_t target) {
    while (*str) {
        output_char(*str++, target);
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
static void print_formatted(const char *str, int width, bool zero_pad, bool left_align, bool is_hex, output_target_t target) {
    int len = str_len(str);
    int pad_count = width > len ? width - len : 0;
    
    // 对于十六进制数，先输出 "0x" 前缀，然后零填充
    bool has_hex_prefix = (is_hex && len >= 2 && str[0] == '0' && str[1] == 'x');
    if (has_hex_prefix && zero_pad && width > 0 && !left_align) {
        output_char('0', target);
        output_char('x', target);
        str += 2;
        len -= 2;
        // 重新计算填充数量（宽度应该减去 "0x" 的长度）
        pad_count = width > len + 2 ? width - len - 2 : 0;
    }
    
    // 对于负数，先输出负号，然后零填充
    bool has_minus = (str[0] == '-');
    if (has_minus && zero_pad && !left_align) {
        output_char('-', target);
        str++;
        len--;
    }
    
    // 左对齐：先输出内容，再填充空格
    if (left_align) {
        print_string(str, target);
        for (int i = 0; i < pad_count; i++) {
            output_char(' ', target);
        }
    } else {
        // 右对齐：先填充，再输出内容
        char pad_char = zero_pad ? '0' : ' ';
        for (int i = 0; i < pad_count; i++) {
            output_char(pad_char, target);
        }
        print_string(str, target);
    }
}

/**
 * 打印整数（内部辅助函数）
 */
static void print_int(int32_t value, int width, bool zero_pad, bool left_align, output_target_t target) {
    char buffer[12];
    int32_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false, target);
}

/**
 * 打印无符号整数（内部辅助函数）
 */
static void print_uint(uint32_t value, int width, bool zero_pad, bool left_align, output_target_t target) {
    char buffer[12];
    uint32_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false, target);
}

/**
 * 打印 64 位整数（内部辅助函数）
 */
static void print_int64(int64_t value, int width, bool zero_pad, bool left_align, output_target_t target) {
    char buffer[21];
    int64_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false, target);
}

/**
 * 打印 64 位无符号整数（内部辅助函数）
 */
static void print_uint64(uint64_t value, int width, bool zero_pad, bool left_align, output_target_t target) {
    char buffer[21];
    uint64_to_str(value, buffer);
    print_formatted(buffer, width, zero_pad, left_align, false, target);
}

/**
 * 打印十六进制数（内部辅助函数，标准 printf 行为）
 * 注意：%x 不输出 0x 前缀，只有 %p 才输出
 */
static void print_hex(uint32_t value, bool uppercase, int width, bool zero_pad, output_target_t target) {
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
        output_char(pad_char, target);
    }
    
    // 逆序输出数字
    for (int i = len - 1; i >= 0; i--) {
        output_char(buffer[i], target);
    }
}

/**
 * 打印 64 位十六进制数（内部辅助函数，标准 printf 行为）
 * 注意：%llx 不输出 0x 前缀
 */
static void print_hex64(uint64_t value, bool uppercase, int width, bool zero_pad, output_target_t target) {
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
        output_char(pad_char, target);
    }
    
    // 逆序输出数字
    for (int i = len - 1; i >= 0; i--) {
        output_char(buffer[i], target);
    }
}

/**
 * 内部格式化输出函数（根据目标输出）
 */
static void vkprintf_internal(const char *fmt, va_list args, output_target_t target) {
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
                    print_formatted(s, width, false, left_align, false, target);
                    break;
                }
                case 'c': {  // 字符
                    char c = (char)va_arg(args, int);
                    output_char(c, target);
                    break;
                }
                case 'd': {  // 有符号十进制整数
                    if (is_long_long) {
                        int64_t val = va_arg(args, int64_t);
                        print_int64(val, width, zero_pad, left_align, target);
                    } else {
                        int val = va_arg(args, int);
                        print_int(val, width, zero_pad, left_align, target);
                    }
                    break;
                }
                case 'u': {  // 无符号十进制整数
                    if (is_long_long) {
                        uint64_t val = va_arg(args, uint64_t);
                        print_uint64(val, width, zero_pad, left_align, target);
                    } else {
                        uint32_t val = va_arg(args, uint32_t);
                        print_uint(val, width, zero_pad, left_align, target);
                    }
                    break;
                }
                case 'x': {  // 十六进制（小写）
                    if (is_long_long) {
                        uint64_t val = va_arg(args, uint64_t);
                        print_hex64(val, false, width, zero_pad, target);
                    } else {
                        uint32_t val = va_arg(args, uint32_t);
                        print_hex(val, false, width, zero_pad, target);
                    }
                    break;
                }
                case 'X': {  // 十六进制（大写）
                    if (is_long_long) {
                        uint64_t val = va_arg(args, uint64_t);
                        print_hex64(val, true, width, zero_pad, target);
                    } else {
                        uint32_t val = va_arg(args, uint32_t);
                        print_hex(val, true, width, zero_pad, target);
                    }
                    break;
                }
                case 'p': {  // 指针（带 0x 前缀）
                    void *ptr = va_arg(args, void *);
                    output_char('0', target);
                    output_char('x', target);
                    // 指针默认 8 位十六进制，零填充
                    int ptr_width = (width > 2) ? (width - 2) : 8;
                    print_hex((uint32_t)ptr, false, ptr_width, true, target);
                    break;
                }
                case '%': {  // 百分号字面值
                    output_char('%', target);
                    break;
                }
                default: {  // 未知格式说明符
                    output_char('%', target);
                    output_char(*fmt, target);
                    break;
                }
            }
            fmt++;
        } else {
            output_char(*fmt++, target);
        }
    }
    
    // 如果输出到 VGA 且使用图形模式，确保刷新
    if ((target & OUTPUT_VGA) && fb_is_initialized()) {
        fb_flush();
    }
}

/* ============================================================================
 * 公共 API - 格式化输出函数
 * ============================================================================ */

void vkprintf(const char *fmt, va_list args) {
    vkprintf_internal(fmt, args, OUTPUT_BOTH);
}

void vkprintf_serial(const char *fmt, va_list args) {
    vkprintf_internal(fmt, args, OUTPUT_SERIAL);
}

void vkprintf_vga(const char *fmt, va_list args) {
    vkprintf_internal(fmt, args, OUTPUT_VGA);
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
}

void kprintf_serial(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf_serial(fmt, args);
    va_end(args);
}

void kprintf_vga(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vkprintf_vga(fmt, args);
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

/* ============================================================================
 * 控制台颜色和清屏（自动适配 VGA 文本模式和帧缓冲图形模式）
 * ============================================================================ */

/**
 * 设置控制台颜色
 * 自动适配 VGA 文本模式和帧缓冲图形模式
 */
void kconsole_set_color(kcolor_t fg, kcolor_t bg) {
    if (fb_is_initialized()) {
        fb_terminal_set_vga_color((uint8_t)fg, (uint8_t)bg);
    } else {
        vga_set_color((vga_color_t)fg, (vga_color_t)bg);
    }
}

/**
 * 清空控制台屏幕
 * 自动适配 VGA 文本模式和帧缓冲图形模式
 */
void kconsole_clear(void) {
    if (fb_is_initialized()) {
        fb_terminal_clear();
    } else {
        vga_clear();
    }
}


