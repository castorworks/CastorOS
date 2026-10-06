/**
 * 数学函数：先把参数化到一个小范围里，再在那个范围上用收敛很快的级数或迭代来算
 */

#include <math.h>

/** 2 的 52 次方：从这里往上，double 表示的都已经是整数 */
#define TWO_52  4503599627370496.0

/** x * 2^k，一步一步乘：中间不会比最后的结果更早溢出或变成 0 */
static double scale_by_power_of_two(double x, int k) {
    for (; k > 0; k--) {
        x *= 2;
    }
    for (; k < 0; k++) {
        x *= 0.5;
    }
    return x;
}

double fabs(double x) {
    return x < 0 ? -x : x;
}

double floor(double x) {
    if (x != x || fabs(x) >= TWO_52) {
        return x;       // NaN、无穷大，或者已经是整数
    }
    double whole = (double)(long long)x;        // 向 0 取整
    return whole > x ? whole - 1 : whole;
}

double ceil(double x) {
    if (x != x || fabs(x) >= TWO_52) {
        return x;
    }
    double whole = (double)(long long)x;
    return whole < x ? whole + 1 : whole;
}

double fmod(double x, double y) {
    if (y == 0 || x != x || y != y || fabs(x) > 1.7976931348623157e308) {
        return NAN;
    }
    if (fabs(y) > 1.7976931348623157e308) {
        return x;       // 除以无穷大：余数就是 x 自己
    }
    double q = x / y;
    q = q < 0 ? ceil(q) : floor(q);             // 向 0 取整
    return x - q * y;
}

double sqrt(double x) {
    if (x != x || x < 0) {
        return NAN;
    }
    if (x == 0 || x > 1.7976931348623157e308) {
        return x;
    }
    // 把 x 写成 m * 4^k，m 在 [1, 4) 里：sqrt(x) = sqrt(m) * 2^k
    double m = x;
    int k = 0;
    while (m >= 4) {
        m *= 0.25;
        k++;
    }
    while (m < 1) {
        m *= 4;
        k--;
    }
    // 牛顿迭代：每一步有效数字翻倍，从 (m + 1) / 2 出发 8 步绰绰有余
    double g = (m + 1) * 0.5;
    for (int i = 0; i < 8; i++) {
        g = (g + m / g) * 0.5;
    }
    return scale_by_power_of_two(g, k);
}

double exp(double x) {
    if (x != x) {
        return x;
    }
    if (x > 709.782712893384) {
        return INFINITY;
    }
    if (x < -745.2) {
        return 0;
    }
    // x = k * ln2 + r，|r| <= ln2 / 2：e^x = 2^k * e^r，e^r 用泰勒级数
    int k = (int)floor(x / M_LN2 + 0.5);
    double r = x - k * M_LN2;
    double term = 1, sum = 1;
    for (int n = 1; n <= 16; n++) {
        term *= r / n;
        sum += term;
    }
    return scale_by_power_of_two(sum, k);
}

double log(double x) {
    if (x != x || x < 0) {
        return NAN;
    }
    if (x == 0) {
        return -INFINITY;
    }
    if (x > 1.7976931348623157e308) {
        return x;
    }
    // x = m * 2^e，m 在 [sqrt(1/2), sqrt(2)) 里：ln x = e * ln2 + ln m
    double m = x;
    int e = 0;
    while (m >= M_SQRT2) {
        m *= 0.5;
        e++;
    }
    while (m < M_SQRT2 / 2) {
        m *= 2;
        e--;
    }
    // ln m = 2 * (s + s^3/3 + s^5/5 + ...)，s = (m - 1) / (m + 1)，|s| < 0.172
    double s = (m - 1) / (m + 1);
    double s2 = s * s, power = s, sum = 0;
    for (int n = 1; n <= 25; n += 2) {
        sum += power / n;
        power *= s2;
    }
    return e * M_LN2 + 2 * sum;
}

double pow(double x, double y) {
    if (y == 0) {
        return 1;
    }
    if (x != x || y != y) {
        return NAN;
    }
    // 整数次方（不太大的）：反复平方，x 是负数也能算，而且比走对数准
    if (y == floor(y) && fabs(y) < 1e9) {
        long long n = (long long)fabs(y);
        double result = 1, base = x;
        for (; n > 0; n >>= 1) {
            if (n & 1) {
                result *= base;
            }
            base *= base;
        }
        return y < 0 ? 1 / result : result;
    }
    if (x < 0) {
        return NAN;
    }
    if (x == 0) {
        return y > 0 ? 0 : INFINITY;
    }
    return exp(y * log(x));
}

// pi/2 分成两段：第一段的二进制位数少，乘上一个不太大的整数仍然是精确的，
// 化简参数时先减它，再减剩下的那一小段，比直接减 pi/2 准得多
#define HALF_PI_HIGH    1.5707963267341256e+00
#define HALF_PI_LOW     6.0771005065061922e-11

/** |r| <= pi/4 时的 sin(r) 和 cos(r)：泰勒级数 */
static double sin_small(double r) {
    double r2 = r * r, term = r, sum = r;
    for (int n = 3; n <= 19; n += 2) {
        term *= -r2 / ((n - 1) * n);
        sum += term;
    }
    return sum;
}

static double cos_small(double r) {
    double r2 = r * r, term = 1, sum = 1;
    for (int n = 2; n <= 20; n += 2) {
        term *= -r2 / ((n - 1) * n);
        sum += term;
    }
    return sum;
}

/** x = k * pi/2 + r，|r| <= pi/4。返回 r，*quadrant 是 k 除以 4 的余数（0-3） */
static double reduce_to_quadrant(double x, int *quadrant) {
    double k = floor(x / (M_PI / 2) + 0.5);
    double r = (x - k * HALF_PI_HIGH) - k * HALF_PI_LOW;
    double q = fmod(k, 4);
    *quadrant = (int)(q < 0 ? q + 4 : q);
    return r;
}

double sin(double x) {
    if (x != x || fabs(x) > 1.7976931348623157e308) {
        return NAN;
    }
    int quadrant;
    double r = reduce_to_quadrant(x, &quadrant);
    switch (quadrant) {
        case 0:  return sin_small(r);
        case 1:  return cos_small(r);
        case 2:  return -sin_small(r);
        default: return -cos_small(r);
    }
}

double cos(double x) {
    if (x != x || fabs(x) > 1.7976931348623157e308) {
        return NAN;
    }
    int quadrant;
    double r = reduce_to_quadrant(x, &quadrant);
    switch (quadrant) {
        case 0:  return cos_small(r);
        case 1:  return -sin_small(r);
        case 2:  return -cos_small(r);
        default: return sin_small(r);
    }
}

double tan(double x) {
    return sin(x) / cos(x);
}
