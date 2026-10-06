// printf 一族：格式化输出
//
// 只有一个格式化函数（format）。它不知道字符最后去哪里，只管往一个有界的缓冲区里放；
// printf / eprintf 用一块静态缓冲区，格式化完再写到标准输出或标准错误，snprintf 直接用
// 调用者给的缓冲区。
//
// 支持的格式符: %s %c %d %i %u %x %X %o %p %f %%
// 长度修饰:     l、ll（%ld %lu %lx %lld %llu %llx ...）
// 标志:         -（左对齐）、0（用 0 填充）
// 宽度:         %5d、%-10s
// 精度:         只对 %f 有意义（%.2f；默认 6 位，最多 15 位）

#include <stdio.h>
#include <syscall.h>
#include <types.h>
#include <libgcc_stub.h>
#include <string.h>

// ============================================================================
// 数字 -> 字符串。结果不含符号，以 '\0' 结尾，返回长度
// ============================================================================

/** 一个 64 位整数最多有多少位数字（八进制 22 位），加上结尾的 '\0' */
#define INT_STR_MAX     24
/** 最大的 double 有 309 位整数，加上小数点、15 位小数和结尾的 '\0' */
#define FLOAT_STR_MAX   352

/** base 是 8、10 或 16 */
static int uint_to_str(unsigned long long val, unsigned base, bool uppercase, char *out) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char rev[INT_STR_MAX];
    int n = 0;
    do {
        // i686 上 64 位除法不是一条指令，要调用户库里的 __udivdi3 / __umoddi3
        rev[n++] = digits[__umoddi3(val, base)];
        val = __udivdi3(val, base);
    } while (val > 0);

    int len = 0;
    while (n > 0) {
        out[len++] = rev[--n];
    }
    out[len] = '\0';
    return len;
}

/** mag 是非负数（符号由调用者输出），precision 是小数点后的位数（最多 15） */
static int float_to_str(double mag, int precision, char *out) {
    int len = 0;
    if (mag != mag) {
        memcpy(out, "nan", 4);
        return 3;
    }
    if (mag > 1.7976931348623157e308) {
        memcpy(out, "inf", 4);
        return 3;
    }

    // 整数部分要放进 64 位整数里逐位取。太大的数先缩小，缩掉的位补 0：
    // double 只有十六七位有效数字，那些位本来就不准
    int scaled = 0;
    while (mag >= 1e18) {
        mag /= 10;
        scaled++;
    }
    if (scaled == 0) {
        // 四舍五入到要输出的最后一位
        double half = 0.5;
        for (int p = 0; p < precision; p++) {
            half /= 10;
        }
        mag += half;
    }
    long long whole = (long long)mag;
    double frac = scaled == 0 ? mag - (double)whole : 0;

    len = uint_to_str((unsigned long long)whole, 10, false, out);
    while (scaled-- > 0) {
        out[len++] = '0';
    }
    if (precision > 0) {
        out[len++] = '.';
        for (int p = 0; p < precision; p++) {
            frac *= 10;
            int digit = (int)frac;
            out[len++] = (char)('0' + digit);
            frac -= digit;
        }
    }
    out[len] = '\0';
    return len;
}

// ============================================================================
// 格式化
// ============================================================================

/** 输出的去处：一块有界的缓冲区。放不下的字符丢掉，始终给结尾的 '\0' 留着位置 */
struct sink {
    char *buf;
    size_t size;        // 至少是 1
    size_t pos;
};

static void put(struct sink *out, char c) {
    if (out->pos < out->size - 1) {
        out->buf[out->pos++] = c;
    }
}

static void put_n(struct sink *out, const char *s, int len) {
    for (int i = 0; i < len; i++) {
        put(out, s[i]);
    }
}

static void put_repeat(struct sink *out, char c, int count) {
    while (count-- > 0) {
        put(out, c);
    }
}

/** 一个 % 说明里除了格式符本身之外的部分 */
struct spec {
    bool left_align;
    bool zero_pad;
    int width;
    int precision;      // -1 表示没有给
    int longs;          // 'l' 的个数：0、1 或 2
};

/**
 * 输出一个字段：前缀（符号或 "0x"）加正文，不够宽度就填充。
 * 填充的位置：右对齐用空格时在最前面，右对齐用 0 时在前缀和正文之间，左对齐时在最后面。
 */
static void put_field(struct sink *out, const struct spec *sp, const char *prefix, const char *body, int body_len) {
    int prefix_len = (int)strlen(prefix);
    int pad = sp->width - prefix_len - body_len;

    if (!sp->left_align && !sp->zero_pad) {
        put_repeat(out, ' ', pad);
    }
    put_n(out, prefix, prefix_len);
    if (!sp->left_align && sp->zero_pad) {
        put_repeat(out, '0', pad);
    }
    put_n(out, body, body_len);
    if (sp->left_align) {
        put_repeat(out, ' ', pad);
    }
}

/** 解析 '%' 后面的标志、宽度、精度和长度修饰；返回指向格式符的指针 */
static const char *parse_spec(const char *p, struct spec *sp) {
    *sp = {};
    sp->precision = -1;

    for (; *p == '-' || *p == '0'; p++) {
        if (*p == '-') sp->left_align = true;
        if (*p == '0') sp->zero_pad = true;
    }
    for (; *p >= '0' && *p <= '9'; p++) {
        sp->width = sp->width * 10 + (*p - '0');
    }
    if (*p == '.') {
        sp->precision = 0;
        for (p++; *p >= '0' && *p <= '9'; p++) {
            sp->precision = sp->precision * 10 + (*p - '0');
        }
    }
    for (; *p == 'l' && sp->longs < 2; p++) {
        sp->longs++;
    }
    return p;
}

static void format(struct sink *out, const char *fmt, __builtin_va_list args) {
    for (; *fmt; fmt++) {
        // 末尾孤零零的一个 '%' 照原样输出
        if (*fmt != '%' || fmt[1] == '\0') {
            put(out, *fmt);
            continue;
        }

        struct spec sp;
        fmt = parse_spec(fmt + 1, &sp);

        switch (*fmt) {
            case 's': {
                const char *s = __builtin_va_arg(args, const char *);
                if (!s) s = "(null)";
                put_field(out, &sp, "", s, (int)strlen(s));
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                put(out, c);
                break;
            }
            case 'd':
            case 'i': {
                long long val = sp.longs == 2 ? __builtin_va_arg(args, long long)
                              : sp.longs == 1 ? __builtin_va_arg(args, long)
                                              : __builtin_va_arg(args, int);
                // 用无符号运算取绝对值，LLONG_MIN 也不会溢出
                bool neg = val < 0;
                unsigned long long mag = neg ? 0ULL - (unsigned long long)val : (unsigned long long)val;
                char tmp[INT_STR_MAX];
                int len = uint_to_str(mag, 10, false, tmp);
                put_field(out, &sp, neg ? "-" : "", tmp, len);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                unsigned long long val = sp.longs == 2 ? __builtin_va_arg(args, unsigned long long)
                                       : sp.longs == 1 ? __builtin_va_arg(args, unsigned long)
                                                       : __builtin_va_arg(args, unsigned int);
                unsigned base = *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16;
                char tmp[INT_STR_MAX];
                int len = uint_to_str(val, base, *fmt == 'X', tmp);
                put_field(out, &sp, "", tmp, len);
                break;
            }
            case 'p': {
                unsigned long val = (unsigned long)__builtin_va_arg(args, void *);
                char tmp[INT_STR_MAX];
                int len = uint_to_str(val, 16, false, tmp);
                put_field(out, &sp, "0x", tmp, len);
                break;
            }
            case 'f': {
                // 变参里的 float 也是按 double 传的
                double val = __builtin_va_arg(args, double);
                bool neg = val < 0;
                int precision = sp.precision < 0 ? 6 : sp.precision > 15 ? 15 : sp.precision;
                char tmp[FLOAT_STR_MAX];
                int len = float_to_str(neg ? -val : val, precision, tmp);
                put_field(out, &sp, neg ? "-" : "", tmp, len);
                break;
            }
            case '%':
                put(out, '%');
                break;
            default:
                // 不认识的格式符照原样输出
                put(out, '%');
                if (*fmt) {
                    put(out, *fmt);
                }
                break;
        }
        if (*fmt == '\0') {
            break;      // 格式串在一个 % 说明的中间结束了
        }
    }
    out->buf[out->pos] = '\0';
}

// ============================================================================
// 对外的函数
// ============================================================================

/** 格式化到一块静态缓冲区，再整个写出去（一次 printf 最多输出这么多） */
static void format_and_write(bool to_err, const char *fmt, __builtin_va_list args) {
    static char buffer[8192];
    struct sink out = { buffer, sizeof(buffer), 0 };
    format(&out, fmt, args);
    if (to_err) {
        write_err(buffer, out.pos);
    } else {
        write_out(buffer, out.pos);
    }
}

void printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    format_and_write(false, fmt, args);
    __builtin_va_end(args);
}

void eprintf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    format_and_write(true, fmt, args);
    __builtin_va_end(args);
}

void print(const char *msg) {
    if (!msg) return;
    write_out(msg, strlen(msg));
}

/** @return 写进 str 的字符数（不含结尾的 '\0'）；放不下的部分被截掉 */
int snprintf(char *str, size_t size, const char *fmt, ...) {
    if (!str || size == 0) {
        return 0;
    }
    struct sink out = { str, size, 0 };
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    format(&out, fmt, args);
    __builtin_va_end(args);
    return (int)out.pos;
}
