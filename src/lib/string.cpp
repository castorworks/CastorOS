#include <lib/string.h>

void int32_to_str(int32_t value, char *buffer) {
    if (value < 0) {
        buffer[0] = '-';
        uint32_to_str((uint32_t)(-value), buffer + 1);
    } else {
        uint32_to_str((uint32_t)value, buffer);
    }
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

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

size_t strnlen(const char *str, size_t max) {
    size_t len = 0;
    while (len < max && str[len]) {
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

char *strcpy(char *dest, const char *src) {
    char *original_dest = dest;
    while ((*dest++ = *src++));
    return original_dest;
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

// ============================================================================
// 路径处理
// ============================================================================

