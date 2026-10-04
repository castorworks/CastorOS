#ifndef _DRIVERS_X86_VGA_H_
#define _DRIVERS_X86_VGA_H_

#include <types.h>

/**
 * VGA 文本模式驱动
 * 提供基础的 80x25 文本模式屏幕输出功能
 */

/* VGA 颜色常量 */
typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
} vga_color_t;

namespace drivers {

/**
 * @brief VGA 文本模式驱动
 */
class Vga {
public:
    /**
     * 初始化 VGA 驱动
     */
    static void init();

    /**
     * 清空屏幕
     */
    static void clear();

    /**
     * 输出字符串
     * @param msg 要输出的字符串，支持 '\n' 换行符
     */
    static void print(const char *msg);

    /**
     * 输出一个字符
     * @param c 要输出的字符，支持 '\n' 换行符
     */
    static void putchar(char c);

    /**
     * 设置颜色
     * @param fg 前景色（文字颜色）
     * @param bg 背景色
     */
    static void set_color(vga_color_t fg, vga_color_t bg);

    /**
     * 获取当前颜色属性
     * @return 当前的颜色属性字节
     */
    static uint8_t get_color();

    /**
     * 获取当前光标位置
     * @param row 输出行号（0 起，可为 NULL）
     * @param col 输出列号（0 起，可为 NULL）
     */
    static void get_cursor(int *row, int *col);
};

} // namespace drivers

#endif /* _DRIVERS_X86_VGA_H_ */
