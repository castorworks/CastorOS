#ifndef _USERLAND_LIB_MATH_H_
#define _USERLAND_LIB_MATH_H_

// 数学函数（double）。全部用普通的浮点运算写成，不依赖 CPU 的专用指令，三个架构结果一致。
// 精度大约是 1e-14 的相对误差，不是逐位正确舍入的；三角函数的参数很大（|x| > 1e9）时
// 化到一个周期里的那一步会损失精度。

#define M_PI        3.14159265358979323846
#define M_E         2.71828182845904523536
#define M_LN2       0.69314718055994530942
#define M_SQRT2     1.41421356237309504880

/** 无穷大和"不是一个数"；x != x 只对 NaN 成立 */
#define INFINITY    (__builtin_inf())
#define NAN         (__builtin_nan(""))

double fabs(double x);
/** 不大于 x 的最大整数 / 不小于 x 的最小整数 */
double floor(double x);
double ceil(double x);
/** x 除以 y 的余数，符号和 x 相同；y 为 0 时是 NaN */
double fmod(double x, double y);

/** 平方根；x < 0 时是 NaN */
double sqrt(double x);
/** x 的 y 次方。x < 0 时 y 必须是整数，否则是 NaN */
double pow(double x, double y);
/** e 的 x 次方 */
double exp(double x);
/** 自然对数；x < 0 时是 NaN，x == 0 时是负无穷 */
double log(double x);

/** 三角函数，参数是弧度 */
double sin(double x);
double cos(double x);
double tan(double x);

#endif // _USERLAND_LIB_MATH_H_
