#include <string.h>
#include <types.h>

// 字符串长度
size_t strlen(const char *str) {
    if (!str) return 0;
    const char *s = str;
    while (*s) s++;
    return (size_t)(s - str);
}

// 字符串比较
int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// 字符串比较（指定长度）
int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// 字符串复制
char *strcpy(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

// 字符串复制（指定长度）
char *strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    char *ret = dest;
    while (n > 0 && (*dest++ = *src++)) {
        n--;
    }
    while (n > 0) {
        *dest++ = '\0';
        n--;
    }
    return ret;
}

// 字符串拼接
char *strcat(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return ret;
}

// 查找字符
char *strchr(const char *str, int c) {
    if (!str) return 0;
    while (*str) {
        if (*str == (char)c) return (char *)str;
        str++;
    }
    return (*str == (char)c) ? (char *)str : 0;
}

// 内存设置
void *memset(void *ptr, int value, size_t num) {
    if (!ptr) return ptr;
    unsigned char *p = (unsigned char *)ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

// 内存复制
void *memcpy(void *dest, const void *src, size_t num) {
    if (!dest || !src) return dest;
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (num--) {
        *d++ = *s++;
    }
    return dest;
}

// 判断是否是数字
int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

// 判断是否是空白字符
int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

// 字符串转整数
int atoi(const char *str) {
    if (!str) return 0;
    int result = 0;
    int sign = 1;
    
    // 跳过空白
    while (isspace(*str)) str++;
    
    // 处理符号
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // 转换数字
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

// 整数转字符串
char *itoa(int value, char *str, int base) {
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;
    int tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    // 处理负数
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        value = -value;
        ptr1 = ptr;
    }
    
    // 转换数字
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    
    // 反转字符串
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    
    return str;
}

// 无符号整数转字符串
char *utoa(unsigned int value, char *str, int base) {
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;
    unsigned int tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    // 转换数字
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    
    // 反转字符串
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    
    return str;
}

