#include <stdio.h>
#include <syscall.h>
#include <types.h>
#include <libgcc_stub.h>
#include <string.h>

// 辅助函数：将数字转换为字符串（十进制）
void num_to_str_dec(unsigned long long val, int is_signed, char *tmp, int *len) {
    int i = 0;
    if (is_signed && (long long)val < 0) {
        tmp[i++] = '-';
        val = -(long long)val;
    }
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        char rev[32];
        int j = 0;
        while (val > 0) {
            rev[j++] = '0' + (char)(__umoddi3(val, 10));
            val = __udivdi3(val, 10);
        }
        while (j > 0) {
            tmp[i++] = rev[--j];
        }
    }
    tmp[i] = '\0';
    *len = i;
}

// 辅助函数：将数字转换为字符串（十六进制）
void num_to_str_hex(unsigned long long val, int uppercase, char *tmp, int *len) {
    int i = 0;
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        char rev[32];
        int j = 0;
        const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        while (val > 0) {
            rev[j++] = digits[val & 0xF];  // 使用位操作代替 % 16
            val >>= 4;  // 使用位操作代替 / 16
        }
        while (j > 0) {
            tmp[i++] = rev[--j];
        }
    }
    tmp[i] = '\0';
    *len = i;
}

// 辅助函数：将数字转换为字符串（八进制）
void num_to_str_oct(unsigned long long val, char *tmp, int *len) {
    int i = 0;
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        char rev[32];
        int j = 0;
        while (val > 0) {
            rev[j++] = '0' + (char)(val & 0x7);  // 使用位操作代替 % 8
            val >>= 3;  // 使用位操作代替 / 8
        }
        while (j > 0) {
            tmp[i++] = rev[--j];
        }
    }
    tmp[i] = '\0';
    *len = i;
}

// 增强的 printf 实现
// 支持格式符: %s, %d, %i, %u, %c, %x, %X, %o, %p, %ld, %lu, %lld, %llu, %%
// 支持标志: -, 0 (左对齐, 零填充)
// 支持宽度: %5d, %-10s 等
void printf(const char *format, ...) {
    static char buffer[8192];  // 足够大的静态缓冲区
    size_t pos = 0;
    
    // 使用内置的变参宏
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    while (*format && pos < sizeof(buffer) - 1) {
        if (*format == '%' && *(format + 1)) {
            format++;
            
            // 解析标志和宽度
            int left_align = 0;
            int zero_pad = 0;
            int width = 0;
            int long_flag = 0;  // 1 for 'l', 2 for 'll'
            
            // 解析标志
            while (*format == '-' || *format == '0') {
                if (*format == '-') left_align = 1;
                if (*format == '0') zero_pad = 1;
                format++;
            }
            
            // 解析宽度
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
            
            // 解析长度修饰符
            if (*format == 'l') {
                long_flag = 1;
                format++;
                if (*format == 'l') {
                    long_flag = 2;  // ll = long long
                    format++;
                }
            }
            
            // 处理格式符
            switch (*format) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    
                    // 计算字符串长度
                    size_t len = 0;
                    const char *p = s;
                    while (*p) {
                        p++;
                        len++;
                    }
                    
                    int pad = (width > 0 && (int)width > (int)len) ? ((int)width - (int)len) : 0;
                    
                    // 左对齐：先输出字符串，再填充
                    if (left_align) {
                        p = s;
                        while (*p && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = *p++;
                        }
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = ' ';
                        }
                    } else {
                        // 右对齐：先填充，再输出字符串
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        p = s;
                        while (*p && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = *p++;
                        }
                    }
                    break;
                }
                case 'd':
                case 'i': {
                    long long val;
                    if (long_flag == 2) {
                        val = __builtin_va_arg(args, long long);
                    } else if (long_flag == 1) {
                        val = __builtin_va_arg(args, long);
                    } else {
                        val = __builtin_va_arg(args, int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_dec((unsigned long long)(val < 0 ? -val : val), 
                                   val < 0, tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        // 左对齐
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = ' ';
                        }
                    } else {
                        // 右对齐
                        if (zero_pad && val < 0 && pad > 0) {
                            buffer[pos++] = '-';
                            pad--;
                        }
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        if (!zero_pad && val < 0) {
                            buffer[pos++] = '-';
                        }
                        for (int i = (val < 0 && !zero_pad) ? 1 : 0; 
                             i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'u': {
                    unsigned long long val;
                    if (long_flag == 2) {
                        val = __builtin_va_arg(args, unsigned long long);
                    } else if (long_flag == 1) {
                        val = __builtin_va_arg(args, unsigned long);
                    } else {
                        val = __builtin_va_arg(args, unsigned int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_dec(val, 0, tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = ' ';
                        }
                    } else {
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned long long val;
                    if (long_flag) {
                        val = __builtin_va_arg(args, unsigned long);
                    } else {
                        val = __builtin_va_arg(args, unsigned int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_hex(val, (*format == 'X'), tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = ' ';
                        }
                    } else {
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'o': {
                    unsigned long long val;
                    if (long_flag) {
                        val = __builtin_va_arg(args, unsigned long);
                    } else {
                        val = __builtin_va_arg(args, unsigned int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_oct(val, tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = ' ';
                        }
                    } else {
                        while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                            buffer[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                            buffer[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'p': {
                    void *ptr = __builtin_va_arg(args, void *);
                    unsigned long val = (unsigned long)ptr;
                    
                    char tmp[32];
                    int len;
                    num_to_str_hex(val, 0, tmp, &len);
                    
                    // 指针格式：0x + 十六进制
                    if (pos < sizeof(buffer) - 1) buffer[pos++] = '0';
                    if (pos < sizeof(buffer) - 1) buffer[pos++] = 'x';
                    
                    int pad = (width > len + 2) ? (width - len - 2) : 0;
                    while (pad-- > 0 && pos < sizeof(buffer) - 1) {
                        buffer[pos++] = (zero_pad) ? '0' : ' ';
                    }
                    
                    for (int i = 0; i < len && pos < sizeof(buffer) - 1; i++) {
                        buffer[pos++] = tmp[i];
                    }
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    buffer[pos++] = c;
                    break;
                }
                case '%': {
                    buffer[pos++] = '%';
                    break;
                }
                default:
                    buffer[pos++] = '%';
                    if (*format) {
                        buffer[pos++] = *format;
                    }
                    break;
            }
        } else {
            buffer[pos++] = *format;
        }
        format++;
    }
    
    __builtin_va_end(args);
    
    buffer[pos] = '\0';
    print(buffer);
}

// snprintf 实现
// 支持格式符: %s, %d, %i, %u, %c, %x, %X, %o, %p, %ld, %lu, %lld, %llu, %%
// 支持标志: -, 0 (左对齐, 零填充)
// 支持宽度: %5d, %-10s 等
int snprintf(char *str, size_t size, const char *format, ...) {
    if (!str || size == 0) {
        return 0;
    }
    
    size_t pos = 0;
    const char *fmt = format;
    
    // 使用内置的变参宏
    __builtin_va_list args;
    __builtin_va_start(args, format);
    
    while (*fmt && pos < size - 1) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            
            // 解析标志和宽度
            int left_align = 0;
            int zero_pad = 0;
            int width = 0;
            int long_flag = 0;  // 1 for 'l', 2 for 'll'
            
            // 解析标志
            while (*fmt == '-' || *fmt == '0') {
                if (*fmt == '-') left_align = 1;
                if (*fmt == '0') zero_pad = 1;
                fmt++;
            }
            
            // 解析宽度
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            // 解析长度修饰符
            if (*fmt == 'l') {
                long_flag = 1;
                fmt++;
                if (*fmt == 'l') {
                    long_flag = 2;  // ll = long long
                    fmt++;
                }
            }
            
            // 处理格式符
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    
                    // 计算字符串长度
                    size_t len = 0;
                    const char *p = s;
                    while (*p) {
                        p++;
                        len++;
                    }
                    
                    int pad = (width > 0 && (int)width > (int)len) ? ((int)width - (int)len) : 0;
                    
                    // 左对齐：先输出字符串，再填充
                    if (left_align) {
                        p = s;
                        while (*p && pos < size - 1) {
                            str[pos++] = *p++;
                        }
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = ' ';
                        }
                    } else {
                        // 右对齐：先填充，再输出字符串
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        p = s;
                        while (*p && pos < size - 1) {
                            str[pos++] = *p++;
                        }
                    }
                    break;
                }
                case 'd':
                case 'i': {
                    long long val;
                    if (long_flag == 2) {
                        val = __builtin_va_arg(args, long long);
                    } else if (long_flag == 1) {
                        val = __builtin_va_arg(args, long);
                    } else {
                        val = __builtin_va_arg(args, int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_dec((unsigned long long)(val < 0 ? -val : val), 
                                   val < 0, tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        // 左对齐
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = ' ';
                        }
                    } else {
                        // 右对齐
                        if (zero_pad && val < 0 && pad > 0) {
                            if (pos < size - 1) str[pos++] = '-';
                            pad--;
                        }
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        if (!zero_pad && val < 0 && pos < size - 1) {
                            str[pos++] = '-';
                        }
                        for (int i = (val < 0 && !zero_pad) ? 1 : 0; 
                             i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'u': {
                    unsigned long long val;
                    if (long_flag == 2) {
                        val = __builtin_va_arg(args, unsigned long long);
                    } else if (long_flag == 1) {
                        val = __builtin_va_arg(args, unsigned long);
                    } else {
                        val = __builtin_va_arg(args, unsigned int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_dec(val, 0, tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = ' ';
                        }
                    } else {
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned long long val;
                    if (long_flag) {
                        val = __builtin_va_arg(args, unsigned long);
                    } else {
                        val = __builtin_va_arg(args, unsigned int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_hex(val, (*fmt == 'X'), tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = ' ';
                        }
                    } else {
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'o': {
                    unsigned long long val;
                    if (long_flag) {
                        val = __builtin_va_arg(args, unsigned long);
                    } else {
                        val = __builtin_va_arg(args, unsigned int);
                    }
                    
                    char tmp[32];
                    int len;
                    num_to_str_oct(val, tmp, &len);
                    
                    int pad = (width > len) ? (width - len) : 0;
                    
                    if (left_align) {
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = ' ';
                        }
                    } else {
                        while (pad-- > 0 && pos < size - 1) {
                            str[pos++] = (zero_pad) ? '0' : ' ';
                        }
                        for (int i = 0; i < len && pos < size - 1; i++) {
                            str[pos++] = tmp[i];
                        }
                    }
                    break;
                }
                case 'p': {
                    void *ptr = __builtin_va_arg(args, void *);
                    unsigned long val = (unsigned long)ptr;
                    
                    char tmp[32];
                    int len;
                    num_to_str_hex(val, 0, tmp, &len);
                    
                    // 指针格式：0x + 十六进制
                    if (pos < size - 1) str[pos++] = '0';
                    if (pos < size - 1) str[pos++] = 'x';
                    
                    int pad = (width > len + 2) ? (width - len - 2) : 0;
                    while (pad-- > 0 && pos < size - 1) {
                        str[pos++] = (zero_pad) ? '0' : ' ';
                    }
                    
                    for (int i = 0; i < len && pos < size - 1; i++) {
                        str[pos++] = tmp[i];
                    }
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    if (pos < size - 1) {
                        str[pos++] = c;
                    }
                    break;
                }
                case '%': {
                    if (pos < size - 1) {
                        str[pos++] = '%';
                    }
                    break;
                }
                default:
                    if (pos < size - 1) {
                        str[pos++] = '%';
                    }
                    if (*fmt && pos < size - 1) {
                        str[pos++] = *fmt;
                    }
                    break;
            }
        } else {
            if (pos < size - 1) {
                str[pos++] = *fmt;
            }
        }
        fmt++;
    }
    
    __builtin_va_end(args);
    
    // 确保字符串以 null 结尾
    if (pos < size) {
        str[pos] = '\0';
    } else if (size > 0) {
        str[size - 1] = '\0';
    }
    
    // 返回应该写入的字符数（不包括 null 终止符）
    return (int)pos;
}

