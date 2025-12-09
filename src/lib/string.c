#include <lib/string.h>

void int32_to_str(int32_t value, char *buffer) {
    if (value < 0) {
        buffer[0] = '-';
        uint32_to_str((uint32_t)(-value), buffer + 1);
    } else {
        uint32_to_str((uint32_t)value, buffer);
    }
}

void int32_to_hex(int32_t value, char *buffer, bool uppercase) {
    uint32_to_hex((uint32_t)value, buffer, uppercase);
}

void uint32_to_str(uint32_t value, char *buffer) {
    char temp[11];  // 最多 10 位 + '\0'
    int i = 0;
    
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    
    // 反转字符串
    for (int j = 0; j < i; j++) {
        buffer[j] = temp[i - j - 1];
    }
    buffer[i] = '\0';
}

void uint32_to_hex(uint32_t value, char *buffer, bool uppercase) {
    const char *hex = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char temp[9];
    int i = 0;
    
    // 处理 0 的特殊情况
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    // 转换为十六进制（逆序）
    while (value > 0 && i < 8) {
        temp[i++] = hex[value & 0xF];
        value >>= 4;
    }
    
    // 反转并复制到输出缓冲区
    for (int j = 0; j < i; j++) {
        buffer[j] = temp[i - j - 1];
    }
    buffer[i] = '\0';
}

void uint64_to_str(uint64_t value, char *buffer) {
    char temp[21];  // 最多 20 位 + '\0'
    int i = 0;
    
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    
    // 反转字符串
    for (int j = 0; j < i; j++) {
        buffer[j] = temp[i - j - 1];
    }
    buffer[i] = '\0';
}

void int64_to_str(int64_t value, char *buffer) {
    if (value < 0) {
        buffer[0] = '-';
        uint64_to_str((uint64_t)(-value), buffer + 1);
    } else {
        uint64_to_str((uint64_t)value, buffer);
    }
}

void uint64_to_hex(uint64_t value, char *buffer, bool uppercase) {
    const char *hex = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char temp[17];
    int i = 0;
    
    // 处理 0 的特殊情况
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    // 转换为十六进制（逆序）
    while (value > 0 && i < 16) {
        temp[i++] = hex[value & 0xF];
        value >>= 4;
    }
    
    // 反转并复制到输出缓冲区
    for (int j = 0; j < i; j++) {
        buffer[j] = temp[i - j - 1];
    }
    buffer[i] = '\0';
}

void int64_to_hex(int64_t value, char *buffer, bool uppercase) {
    uint64_to_hex((uint64_t)value, buffer, uppercase);
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) {
        return 0;
    }
    
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    
    if (n == 0) {
        return 0;
    }
    
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// 辅助函数：将字符转换为小写
static inline char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

int strcasecmp(const char *s1, const char *s2) {
    unsigned char c1, c2;
    
    while (*s1 && *s2) {
        c1 = (unsigned char)to_lower(*s1);
        c2 = (unsigned char)to_lower(*s2);
        
        if (c1 != c2) {
            return c1 - c2;
        }
        
        s1++;
        s2++;
    }
    
    // 处理其中一个字符串已结束的情况
    c1 = (unsigned char)to_lower(*s1);
    c2 = (unsigned char)to_lower(*s2);
    
    return c1 - c2;
}

char *strcpy(char *dest, const char *src) {
    char *original_dest = dest;
    while ((*dest++ = *src++));
    return original_dest;
}

char *strcat(char *dest, const char *src) {
    char *original_dest = dest;
    // 找到目标字符串的末尾
    while (*dest) {
        dest++;
    }
    // 从末尾开始复制源字符串
    while ((*dest++ = *src++));
    return original_dest;
}

char *strchr(const char *str, int c) {
    while (*str != '\0') {
        if (*str == (char)c) {
            return (char *)str;
        }
        str++;
    }
    // 检查是否查找 '\0'
    if ((char)c == '\0') {
        return (char *)str;
    }
    return NULL;
}

char *strrchr(const char *str, int c) {
    const char *last = NULL;
    while (*str != '\0') {
        if (*str == (char)c) {
            last = str;
        }
        str++;
    }
    // 检查是否查找 '\0'
    if ((char)c == '\0') {
        return (char *)str;
    }
    return (char *)last;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

void *memset(void *ptr, int value, size_t num) {
    unsigned char *p = (unsigned char *)ptr;
    unsigned char v = (unsigned char)value;
    
    // 处理未对齐的头部
    while (((uintptr_t)p & 3) && num > 0) {
        *p++ = v;
        num--;
    }
    
    // 32 位字填充（主要部分）
    if (num >= 4) {
        uint32_t v32 = v | (v << 8) | (v << 16) | (v << 24);
        uint32_t *p32 = (uint32_t *)p;
        size_t count = num / 4;
        
        // 展开循环，一次处理 4 个字（16 字节）
        while (count >= 4) {
            p32[0] = v32;
            p32[1] = v32;
            p32[2] = v32;
            p32[3] = v32;
            p32 += 4;
            count -= 4;
        }
        while (count--) {
            *p32++ = v32;
        }
        
        p = (unsigned char *)p32;
        num &= 3;
    }
    
    // 处理剩余字节
    while (num--) {
        *p++ = v;
    }
    
    return ptr;
}

void *memcpy(void *dest, const void *src, size_t num) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    // 如果源和目标对齐相同，使用 32 位复制
    if ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0) {
        // 处理未对齐的头部
        while (((uintptr_t)d & 3) && num > 0) {
            *d++ = *s++;
            num--;
        }
        
        // 32 位字复制（主要部分）
        if (num >= 4) {
            uint32_t *d32 = (uint32_t *)d;
            const uint32_t *s32 = (const uint32_t *)s;
            size_t count = num / 4;
            
            // 展开循环，一次处理 4 个字（16 字节）
            while (count >= 4) {
                d32[0] = s32[0];
                d32[1] = s32[1];
                d32[2] = s32[2];
                d32[3] = s32[3];
                d32 += 4;
                s32 += 4;
                count -= 4;
            }
            while (count--) {
                *d32++ = *s32++;
            }
            
            d = (unsigned char *)d32;
            s = (const unsigned char *)s32;
            num &= 3;
        }
    }
    
    // 处理剩余字节（或未对齐情况）
    while (num--) {
        *d++ = *s++;
    }
    
    return dest;
}

int memcmp(const void *ptr1, const void *ptr2, size_t num) {
    const unsigned char *p1 = (const unsigned char *)ptr1;
    const unsigned char *p2 = (const unsigned char *)ptr2;
    while (num--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t num) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    if (d < s || d >= s + num) {
        // 不重叠或目标在源之前，可以使用优化的 memcpy
        return memcpy(dest, src, num);
    }
    
    // 目标和源重叠，需要从后向前复制
    d += num;
    s += num;
    
    // 如果源和目标对齐相同，使用 32 位复制
    if ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0) {
        // 处理未对齐的尾部
        while (((uintptr_t)d & 3) && num > 0) {
            *--d = *--s;
            num--;
        }
        
        // 32 位字复制（主要部分）
        if (num >= 4) {
            uint32_t *d32 = (uint32_t *)d;
            const uint32_t *s32 = (const uint32_t *)s;
            size_t count = num / 4;
            
            // 展开循环
            while (count >= 4) {
                d32 -= 4;
                s32 -= 4;
                d32[3] = s32[3];
                d32[2] = s32[2];
                d32[1] = s32[1];
                d32[0] = s32[0];
                count -= 4;
            }
            while (count--) {
                *--d32 = *--s32;
            }
            
            d = (unsigned char *)d32;
            s = (const unsigned char *)s32;
            num &= 3;
        }
    }
    
    // 处理剩余字节
    while (num--) {
        *--d = *--s;
    }
    
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *original_dest = dest;
    size_t i;
    
    // 复制字符，直到遇到 '\0' 或达到 n
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    // 如果源字符串长度小于 n，用 '\0' 填充剩余空间
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return original_dest;
}

char *strtok(char *str, const char *delim) {
    static char *saved_str = NULL;  // 保存状态的静态变量
    
    // 如果传入了新字符串，使用它；否则继续处理上次的字符串
    if (str != NULL) {
        saved_str = str;
    }
    
    // 如果没有字符串可处理，返回 NULL
    if (saved_str == NULL) {
        return NULL;
    }
    
    // 跳过开头的分隔符
    while (*saved_str != '\0') {
        const char *d = delim;
        bool is_delim = false;
        while (*d != '\0') {
            if (*saved_str == *d) {
                is_delim = true;
                break;
            }
            d++;
        }
        if (!is_delim) {
            break;
        }
        saved_str++;
    }
    
    // 如果已经到达字符串末尾，没有更多标记
    if (*saved_str == '\0') {
        saved_str = NULL;
        return NULL;
    }
    
    // 记录标记的起始位置
    char *token_start = saved_str;
    
    // 查找标记的结束位置（下一个分隔符或字符串末尾）
    while (*saved_str != '\0') {
        const char *d = delim;
        bool is_delim = false;
        while (*d != '\0') {
            if (*saved_str == *d) {
                is_delim = true;
                break;
            }
            d++;
        }
        if (is_delim) {
            *saved_str = '\0';  // 用 '\0' 替换分隔符
            saved_str++;
            return token_start;
        }
        saved_str++;
    }
    
    // 已经到达字符串末尾
    saved_str = NULL;
    return token_start;
}

// snprintf 的辅助函数：向缓冲区添加字符
static int snprintf_putchar(char *buf, size_t size, size_t pos, char c) {
    if (pos < size - 1) {
        buf[pos] = c;
    }
    return 1;
}

// snprintf 的辅助函数：向缓冲区添加字符串
static int snprintf_putstr(char *buf, size_t size, size_t pos, const char *str) {
    int count = 0;
    if (!str) {
        str = "(null)";
    }
    while (*str) {
        if (pos + count < size - 1) {
            buf[pos + count] = *str;
        }
        str++;
        count++;
    }
    return count;
}

/**
 * 内部辅助：将无符号整数转换为十六进制字符串（不带 0x 前缀）
 */
static void uint32_to_hex_simple(uint32_t value, char *buffer, bool uppercase, int min_digits) {
    const char *hex = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char temp[9];
    int i = 0;
    
    // 处理 0 的特殊情况
    if (value == 0) {
        // 填充最小位数的 0
        for (int j = 0; j < min_digits && j < 8; j++) {
            buffer[j] = '0';
        }
        buffer[min_digits > 0 ? min_digits : 1] = '\0';
        return;
    }
    
    // 转换为十六进制（逆序）
    while (value > 0 && i < 8) {
        temp[i++] = hex[value & 0xF];
        value >>= 4;
    }
    
    // 如果需要，添加前导零
    while (i < min_digits && i < 8) {
        temp[i++] = '0';
    }
    
    // 反转并复制到输出缓冲区
    for (int j = 0; j < i; j++) {
        buffer[j] = temp[i - j - 1];
    }
    buffer[i] = '\0';
}

int snprintf(char *str, size_t size, const char *format, ...) {
    __builtin_va_list args;
    size_t pos = 0;
    char temp_buf[32];
    
    if (size == 0) {
        return 0;
    }
    
    __builtin_va_start(args, format);
    
    while (*format && pos < size - 1) {
        if (*format != '%') {
            // 普通字符
            pos += snprintf_putchar(str, size, pos, *format);
            format++;
            continue;
        }
        
        // 处理格式说明符
        format++; // 跳过 '%'
        
        // 解析标志
        bool zero_pad = false;
        bool is_long_long = false;
        if (*format == '0') {
            zero_pad = true;
            format++;
        }
        
        // 解析宽度
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        
        // 解析长度修饰符
        if (*format == 'l') {
            format++;
            if (*format == 'l') {
                is_long_long = true;
                format++;
            }
        }
        
        switch (*format) {
            case 'd':
            case 'i': {
                int32_t val = __builtin_va_arg(args, int32_t);
                int32_to_str(val, temp_buf);
                size_t len = strlen(temp_buf);
                // 处理宽度和填充
                while (width > (int)len && pos < size - 1) {
                    pos += snprintf_putchar(str, size, pos, zero_pad ? '0' : ' ');
                    width--;
                }
                pos += snprintf_putstr(str, size, pos, temp_buf);
                break;
            }
            case 'u': {
                if (is_long_long) {
                    uint64_t val = __builtin_va_arg(args, uint64_t);
                    uint64_to_str(val, temp_buf);
                } else {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    uint32_to_str(val, temp_buf);
                }
                size_t len = strlen(temp_buf);
                while (width > (int)len && pos < size - 1) {
                    pos += snprintf_putchar(str, size, pos, zero_pad ? '0' : ' ');
                    width--;
                }
                pos += snprintf_putstr(str, size, pos, temp_buf);
                break;
            }
            case 'x': {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                uint32_to_hex_simple(val, temp_buf, false, zero_pad ? width : 0);
                size_t len = strlen(temp_buf);
                while (width > (int)len && pos < size - 1) {
                    pos += snprintf_putchar(str, size, pos, zero_pad ? '0' : ' ');
                    width--;
                }
                pos += snprintf_putstr(str, size, pos, temp_buf);
                break;
            }
            case 'X': {
                uint32_t val = __builtin_va_arg(args, uint32_t);
                uint32_to_hex_simple(val, temp_buf, true, zero_pad ? width : 0);
                size_t len = strlen(temp_buf);
                while (width > (int)len && pos < size - 1) {
                    pos += snprintf_putchar(str, size, pos, zero_pad ? '0' : ' ');
                    width--;
                }
                pos += snprintf_putstr(str, size, pos, temp_buf);
                break;
            }
            case 'p': {
                void *val = __builtin_va_arg(args, void *);
                // 指针格式保持 0x 前缀
                uint32_to_hex((uint32_t)val, temp_buf, false);
                pos += snprintf_putstr(str, size, pos, temp_buf);
                break;
            }
            case 's': {
                const char *val = __builtin_va_arg(args, const char *);
                if (!val) val = "(null)";
                size_t len = strlen(val);
                // 处理宽度
                while (width > (int)len && pos < size - 1) {
                    pos += snprintf_putchar(str, size, pos, ' ');
                    width--;
                }
                pos += snprintf_putstr(str, size, pos, val);
                break;
            }
            case 'c': {
                char val = (char)__builtin_va_arg(args, int);
                pos += snprintf_putchar(str, size, pos, val);
                break;
            }
            case '%': {
                pos += snprintf_putchar(str, size, pos, '%');
                break;
            }
            default:
                // 未知格式说明符，输出原样
                pos += snprintf_putchar(str, size, pos, '%');
                pos += snprintf_putchar(str, size, pos, *format);
                break;
        }
        
        format++;
    }
    
    // 添加结尾的 '\0'
    if (pos < size) {
        str[pos] = '\0';
    } else {
        str[size - 1] = '\0';
    }
    
    __builtin_va_end(args);
    
    return pos;
}
