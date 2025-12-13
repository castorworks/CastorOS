/**
 * @file framebuffer.h
 * @brief 帧缓冲驱动 - 提供图形模式支持
 * 
 * 实现基于 Multiboot 的帧缓冲图形驱动，支持：
 * - 多种像素格式（RGB565, RGB888, ARGB8888, BGRA8888）
 * - 基础图形操作（像素、线条、矩形）
 * - 文本渲染（基于位图字体）
 * - 多分辨率支持（1024x768, 1400x1050 等）
 */

#ifndef _DRIVERS_X86_FRAMEBUFFER_H_
#define _DRIVERS_X86_FRAMEBUFFER_H_

#include <types.h>
#include <kernel/multiboot.h>

/* ============================================================================
 * 像素格式
 * ============================================================================ */

typedef enum {
    FB_FORMAT_RGB565,     ///< 16bpp: RRRRRGGGGGGBBBBB
    FB_FORMAT_RGB888,     ///< 24bpp: RRRRRRRRGGGGGGGGBBBBBBBB
    FB_FORMAT_ARGB8888,   ///< 32bpp: AAAAAAAARRRRRRRRGGGGGGGGBBBBBBBB
    FB_FORMAT_BGRA8888,   ///< 32bpp: BBBBBBBBGGGGGGGGRRRRRRRRAAAAAAAAA
    FB_FORMAT_UNKNOWN     ///< 未知格式
} fb_format_t;

/* ============================================================================
 * 帧缓冲信息结构
 * ============================================================================ */

typedef struct framebuffer_info {
    uintptr_t address;      ///< 帧缓冲物理地址
    uint32_t *buffer;       ///< 映射后的虚拟地址
    uint32_t width;         ///< 水平分辨率（像素）
    uint32_t height;        ///< 垂直分辨率（像素）
    uint32_t pitch;         ///< 每行字节数（包括填充）
    uint8_t  bpp;           ///< 每像素位数（16/24/32）
    fb_format_t format;     ///< 像素格式
    uint8_t  red_mask_size;   ///< 红色掩码大小
    uint8_t  red_field_pos;   ///< 红色位位置
    uint8_t  green_mask_size; ///< 绿色掩码大小
    uint8_t  green_field_pos; ///< 绿色位位置
    uint8_t  blue_mask_size;  ///< 蓝色掩码大小
    uint8_t  blue_field_pos;  ///< 蓝色位位置
} framebuffer_info_t;

/* ============================================================================
 * 颜色定义
 * ============================================================================ */

/**
 * @brief 颜色结构（RGBA 格式）
 */
typedef struct {
    uint8_t r;  ///< 红色分量 (0-255)
    uint8_t g;  ///< 绿色分量 (0-255)
    uint8_t b;  ///< 蓝色分量 (0-255)
    uint8_t a;  ///< Alpha 分量 (0-255)
} color_t;

/** 预定义颜色 */
#define COLOR_BLACK     ((color_t){0, 0, 0, 255})
#define COLOR_WHITE     ((color_t){255, 255, 255, 255})
#define COLOR_RED       ((color_t){255, 0, 0, 255})
#define COLOR_GREEN     ((color_t){0, 255, 0, 255})
#define COLOR_BLUE      ((color_t){0, 0, 255, 255})
#define COLOR_YELLOW    ((color_t){255, 255, 0, 255})
#define COLOR_CYAN      ((color_t){0, 255, 255, 255})
#define COLOR_MAGENTA   ((color_t){255, 0, 255, 255})
#define COLOR_GRAY      ((color_t){128, 128, 128, 255})
#define COLOR_DARK_GRAY ((color_t){64, 64, 64, 255})
#define COLOR_LIGHT_GRAY ((color_t){192, 192, 192, 255})
#define COLOR_ORANGE    ((color_t){255, 165, 0, 255})
#define COLOR_PINK      ((color_t){255, 192, 203, 255})
#define COLOR_BROWN     ((color_t){139, 69, 19, 255})
#define COLOR_PURPLE    ((color_t){128, 0, 128, 255})

/* ============================================================================
 * 初始化函数
 * ============================================================================ */

/**
 * @brief 从 Multiboot 信息初始化帧缓冲
 * @param mbi Multiboot 信息结构指针
 * @return 成功返回 0，失败返回负数错误码
 *         -1: 没有帧缓冲信息
 *         -2: 不是图形模式
 *         -3: 内存映射失败
 */
int fb_init(multiboot_info_t *mbi);

/**
 * @brief 检查帧缓冲是否已初始化
 * @return 已初始化返回 true
 */
bool fb_is_initialized(void);

/**
 * @brief 获取帧缓冲信息
 * @return 帧缓冲信息指针，未初始化返回 NULL
 */
framebuffer_info_t *fb_get_info(void);

/* ============================================================================
 * 基础绘图函数
 * ============================================================================ */

/**
 * @brief 清屏
 * @param color 填充颜色
 */
void fb_clear(color_t color);

/**
 * @brief 绘制单个像素
 * @param x X 坐标
 * @param y Y 坐标
 * @param color 像素颜色
 */
void fb_put_pixel(int x, int y, color_t color);

/**
 * @brief 读取像素颜色
 * @param x X 坐标
 * @param y Y 坐标
 * @return 像素颜色
 */
color_t fb_get_pixel(int x, int y);

/**
 * @brief 绘制水平线
 * @param x 起始 X 坐标
 * @param y Y 坐标
 * @param length 线条长度
 * @param color 线条颜色
 */
void fb_draw_hline(int x, int y, int length, color_t color);

/**
 * @brief 绘制垂直线
 * @param x X 坐标
 * @param y 起始 Y 坐标
 * @param length 线条长度
 * @param color 线条颜色
 */
void fb_draw_vline(int x, int y, int length, color_t color);

/**
 * @brief 绘制任意线条（Bresenham 算法）
 * @param x1 起点 X
 * @param y1 起点 Y
 * @param x2 终点 X
 * @param y2 终点 Y
 * @param color 线条颜色
 */
void fb_draw_line(int x1, int y1, int x2, int y2, color_t color);

/**
 * @brief 绘制矩形边框
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param width 宽度
 * @param height 高度
 * @param color 边框颜色
 */
void fb_draw_rect(int x, int y, int width, int height, color_t color);

/**
 * @brief 填充矩形
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param width 宽度
 * @param height 高度
 * @param color 填充颜色
 */
void fb_fill_rect(int x, int y, int width, int height, color_t color);

/* ============================================================================
 * 位图操作
 * ============================================================================ */

/**
 * @brief 绘制位图（32bpp ARGB 格式）
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param width 位图宽度
 * @param height 位图高度
 * @param data 像素数据（ARGB 格式）
 */
void fb_blit(int x, int y, int width, int height, const uint32_t *data);

/**
 * @brief 复制屏幕区域
 * @param src_x 源区域左上角 X
 * @param src_y 源区域左上角 Y
 * @param dst_x 目标区域左上角 X
 * @param dst_y 目标区域左上角 Y
 * @param width 区域宽度
 * @param height 区域高度
 */
void fb_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height);

/* ============================================================================
 * 文本渲染
 * ============================================================================ */

/**
 * @brief 设置位图字体
 * @param font_data 字体数据指针
 * @param char_width 字符宽度（像素）
 * @param char_height 字符高度（像素）
 */
void fb_set_font(const uint8_t *font_data, int char_width, int char_height);

/**
 * @brief 绘制单个字符
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param c 字符
 * @param fg 前景色
 * @param bg 背景色
 */
void fb_draw_char(int x, int y, char c, color_t fg, color_t bg);

/**
 * @brief 绘制字符（透明背景）
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param c 字符
 * @param fg 前景色
 */
void fb_draw_char_transparent(int x, int y, char c, color_t fg);

/**
 * @brief 绘制字符串
 * @param x 起始 X
 * @param y 起始 Y
 * @param str 字符串
 * @param fg 前景色
 * @param bg 背景色
 */
void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg);

/**
 * @brief 绘制字符串（透明背景）
 * @param x 起始 X
 * @param y 起始 Y
 * @param str 字符串
 * @param fg 前景色
 */
void fb_draw_string_transparent(int x, int y, const char *str, color_t fg);

/**
 * @brief 获取当前字体宽度
 * @return 字符宽度（像素）
 */
int fb_get_font_width(void);

/**
 * @brief 获取当前字体高度
 * @return 字符高度（像素）
 */
int fb_get_font_height(void);

/**
 * @brief 获取屏幕可显示的字符列数
 * @return 字符列数
 */
int fb_get_cols(void);

/**
 * @brief 获取屏幕可显示的字符行数
 * @return 字符行数
 */
int fb_get_rows(void);

/* ============================================================================
 * 终端仿真
 * ============================================================================ */

/**
 * @brief 初始化图形终端（用于替代 VGA 文本模式）
 */
void fb_terminal_init(void);

/**
 * @brief 清空终端屏幕
 */
void fb_terminal_clear(void);

/**
 * @brief 向终端写入字符
 * @param c 字符
 */
void fb_terminal_putchar(char c);

/**
 * @brief 向终端写入字符串
 * @param str 字符串
 */
void fb_terminal_write(const char *str);

/**
 * @brief 设置终端颜色
 * @param fg 前景色
 * @param bg 背景色
 */
void fb_terminal_set_color(color_t fg, color_t bg);

/**
 * @brief 设置终端光标位置
 * @param col 列
 * @param row 行
 */
void fb_terminal_set_cursor(int col, int row);

/**
 * @brief 获取终端光标列位置
 * @return 当前列
 */
int fb_terminal_get_cursor_col(void);

/**
 * @brief 获取终端光标行位置
 * @return 当前行
 */
int fb_terminal_get_cursor_row(void);

/**
 * @brief 终端滚动
 * @param lines 滚动行数（正数向上滚动）
 */
void fb_terminal_scroll(int lines);

/* ============================================================================
 * VGA 兼容函数（用于内核 shell）
 * ============================================================================ */

/**
 * @brief 将 VGA 颜色索引转换为 RGB 颜色
 * @param vga_color VGA 颜色索引 (0-15)
 * @return RGB 颜色
 */
color_t fb_vga_to_color(uint8_t vga_color);

/**
 * @brief 使用 VGA 颜色设置终端颜色（兼容内核 shell）
 * @param fg VGA 前景色索引 (0-15)
 * @param bg VGA 背景色索引 (0-15)
 */
void fb_terminal_set_vga_color(uint8_t fg, uint8_t bg);

/* ============================================================================
 * 双缓冲支持
 * ============================================================================ */

/**
 * @brief 启用双缓冲
 * 
 * 双缓冲在普通内存中维护后备缓冲区，所有绘制操作在后备缓冲区进行。
 * 这样滚屏等需要读取像素的操作会更快。
 */
void fb_enable_double_buffer(void);

/**
 * @brief 刷新后备缓冲区到显存
 * 
 * 只刷新脏区域以提升性能
 */
void fb_flush(void);

/**
 * @brief 强制刷新整个屏幕
 */
void fb_flush_all(void);

/**
 * @brief 启用/禁用双缓冲（兼容旧接口）
 * @param enable true 启用，false 禁用
 * @return 成功返回 true
 */
bool fb_set_double_buffer(bool enable);

/**
 * @brief 交换前后缓冲区（旧接口，现在等同于 fb_flush_all）
 */
void fb_swap_buffers(void);

/* ============================================================================
 * 调试和工具函数
 * ============================================================================ */

/**
 * @brief 打印帧缓冲信息
 */
void fb_print_info(void);

/**
 * @brief 运行图形演示
 */
void fb_demo(void);

#endif /* _DRIVERS_X86_FRAMEBUFFER_H_ */

