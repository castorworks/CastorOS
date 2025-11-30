/**
 * @file framebuffer.c
 * @brief 帧缓冲驱动实现
 * 
 * 提供基于 Multiboot 的图形模式支持，包括基础绘图和文本渲染
 */

#include <drivers/framebuffer.h>
#include <drivers/font8x16.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* ============================================================================
 * 全局变量
 * ============================================================================ */

/** 帧缓冲信息 */
static framebuffer_info_t fb_info;

/** 初始化标志 */
static bool fb_initialized = false;

/** 当前字体 */
static const uint8_t *current_font = NULL;
static int font_width = 8;
static int font_height = 16;

/** 终端状态 */
static int term_cursor_col = 0;
static int term_cursor_row = 0;
static color_t term_fg = {170, 170, 170, 255};  // 浅灰（VGA 默认）
static color_t term_bg = {0, 0, 0, 255};        // 黑色

/** 双缓冲支持 */
static uint8_t *back_buffer_mem = NULL;   // 后备缓冲区（在普通内存中）
static bool double_buffering = false;      // 是否启用双缓冲
static int dirty_line_start = -1;          // 脏区域起始行
static int dirty_line_end = -1;            // 脏区域结束行

/* VGA 16 色调色板 → RGB 颜色 */
static const color_t vga_palette[16] = {
    {0, 0, 0, 255},         // 0: BLACK
    {0, 0, 170, 255},       // 1: BLUE
    {0, 170, 0, 255},       // 2: GREEN
    {0, 170, 170, 255},     // 3: CYAN
    {170, 0, 0, 255},       // 4: RED
    {170, 0, 170, 255},     // 5: MAGENTA
    {170, 85, 0, 255},      // 6: BROWN
    {170, 170, 170, 255},   // 7: LIGHT_GREY
    {85, 85, 85, 255},      // 8: DARK_GREY
    {85, 85, 255, 255},     // 9: LIGHT_BLUE
    {85, 255, 85, 255},     // 10: LIGHT_GREEN
    {85, 255, 255, 255},    // 11: LIGHT_CYAN
    {255, 85, 85, 255},     // 12: LIGHT_RED
    {255, 85, 255, 255},    // 13: LIGHT_MAGENTA
    {255, 255, 85, 255},    // 14: YELLOW
    {255, 255, 255, 255},   // 15: WHITE
};

/* ANSI 颜色到 VGA 索引的映射 */
static const uint8_t ansi_to_vga_fg[8] = {0, 4, 2, 6, 1, 5, 3, 7};  // 30-37
static const uint8_t ansi_to_vga_bright[8] = {8, 12, 10, 14, 9, 13, 11, 15};  // 90-97

/* ANSI 转义序列解析状态 */
typedef enum {
    ANSI_NORMAL,      // 正常模式
    ANSI_ESCAPE,      // 收到 ESC (0x1B)
    ANSI_BRACKET,     // 收到 '['
    ANSI_PARAM        // 解析参数
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_NORMAL;
#define ANSI_MAX_PARAMS 8
static int ansi_params[ANSI_MAX_PARAMS];
static int ansi_param_count = 0;
static bool ansi_bold = false;

/* 前置声明 */
static void fb_handle_sgr(void);
static inline uint8_t *fb_get_draw_buffer(void);
static inline void fb_mark_dirty(int y_start, int y_end);

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 将 color_t 转换为像素值
 */
static inline uint32_t color_to_pixel(color_t c) {
    switch (fb_info.format) {
        case FB_FORMAT_ARGB8888:
            return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | 
                   ((uint32_t)c.g << 8) | c.b;
        case FB_FORMAT_BGRA8888:
            return ((uint32_t)c.b << 24) | ((uint32_t)c.g << 16) | 
                   ((uint32_t)c.r << 8) | c.a;
        case FB_FORMAT_RGB888:
            return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
        case FB_FORMAT_RGB565:
            return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
        default:
            return 0;
    }
}

/**
 * @brief 将像素值转换为 color_t
 */
static inline color_t pixel_to_color(uint32_t pixel) {
    color_t c = {0, 0, 0, 255};
    
    switch (fb_info.format) {
        case FB_FORMAT_ARGB8888:
            c.a = (pixel >> 24) & 0xFF;
            c.r = (pixel >> 16) & 0xFF;
            c.g = (pixel >> 8) & 0xFF;
            c.b = pixel & 0xFF;
            break;
        case FB_FORMAT_BGRA8888:
            c.b = (pixel >> 24) & 0xFF;
            c.g = (pixel >> 16) & 0xFF;
            c.r = (pixel >> 8) & 0xFF;
            c.a = pixel & 0xFF;
            break;
        case FB_FORMAT_RGB888:
            c.r = (pixel >> 16) & 0xFF;
            c.g = (pixel >> 8) & 0xFF;
            c.b = pixel & 0xFF;
            break;
        case FB_FORMAT_RGB565:
            c.r = ((pixel >> 11) & 0x1F) << 3;
            c.g = ((pixel >> 5) & 0x3F) << 2;
            c.b = (pixel & 0x1F) << 3;
            break;
        default:
            break;
    }
    return c;
}

/**
 * @brief 快速设置像素（内联，无边界检查）
 * 
 * 如果启用了双缓冲，写入后备缓冲区；否则直接写入帧缓冲
 */
static inline void fb_put_pixel_fast(int x, int y, uint32_t pixel) {
    uint8_t *draw_buf = fb_get_draw_buffer();
    uint32_t offset = y * fb_info.pitch + x * (fb_info.bpp / 8);
    
    if (fb_info.bpp == 32) {
        *((uint32_t *)(draw_buf + offset)) = pixel;
    } else if (fb_info.bpp == 24) {
        draw_buf[offset] = pixel & 0xFF;
        draw_buf[offset + 1] = (pixel >> 8) & 0xFF;
        draw_buf[offset + 2] = (pixel >> 16) & 0xFF;
    } else if (fb_info.bpp == 16) {
        *((uint16_t *)(draw_buf + offset)) = (uint16_t)pixel;
    }
    
    // 标记脏行
    if (double_buffering) {
        fb_mark_dirty(y, y + 1);
    }
}

/* ============================================================================
 * 初始化函数
 * ============================================================================ */

int fb_init(multiboot_info_t *mbi) {
    if (!mbi) {
        return -1;
    }
    
    // 检查是否有帧缓冲信息
    if (!(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        LOG_DEBUG_MSG("fb: No framebuffer info in multiboot\n");
        return -1;
    }
    
    // 检查是否为图形模式（类型 1 = RGB 图形模式）
    if (mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        LOG_DEBUG_MSG("fb: Not in graphics mode (type=%d)\n", mbi->framebuffer_type);
        return -2;
    }
    
    // 填充帧缓冲信息
    fb_info.address = (uint32_t)mbi->framebuffer_addr;
    fb_info.width = mbi->framebuffer_width;
    fb_info.height = mbi->framebuffer_height;
    fb_info.pitch = mbi->framebuffer_pitch;
    fb_info.bpp = mbi->framebuffer_bpp;
    
    fb_info.red_mask_size = mbi->framebuffer_red_mask_size;
    fb_info.red_field_pos = mbi->framebuffer_red_field_position;
    fb_info.green_mask_size = mbi->framebuffer_green_mask_size;
    fb_info.green_field_pos = mbi->framebuffer_green_field_position;
    fb_info.blue_mask_size = mbi->framebuffer_blue_mask_size;
    fb_info.blue_field_pos = mbi->framebuffer_blue_field_position;
    
    // 确定像素格式
    if (fb_info.bpp == 32) {
        if (fb_info.red_field_pos == 16) {
            fb_info.format = FB_FORMAT_ARGB8888;
        } else if (fb_info.blue_field_pos == 24) {
            fb_info.format = FB_FORMAT_BGRA8888;
        } else {
            fb_info.format = FB_FORMAT_ARGB8888;  // 默认
        }
    } else if (fb_info.bpp == 24) {
        fb_info.format = FB_FORMAT_RGB888;
    } else if (fb_info.bpp == 16) {
        fb_info.format = FB_FORMAT_RGB565;
    } else {
        fb_info.format = FB_FORMAT_UNKNOWN;
    }
    
    // 计算帧缓冲大小
    uint32_t fb_size = fb_info.pitch * fb_info.height;
    
    // 映射帧缓冲到虚拟地址空间
    // 使用 vmm_map_framebuffer 进行映射，启用 Write-Combining 模式以提升性能
    uint32_t fb_virt = vmm_map_framebuffer(fb_info.address, fb_size);
    if (!fb_virt) {
        LOG_ERROR_MSG("fb: Failed to map framebuffer\n");
        return -3;
    }
    
    fb_info.buffer = (uint32_t *)fb_virt;
    
    // 设置默认字体
    fb_set_font(font8x16_data, 8, 16);
    
    fb_initialized = true;
    
    // 双缓冲选项：
    // - 启用：滚屏时在 WB 内存操作更快，但每次输出需要复制到显存
    // - 禁用：直接写入 WC 显存，写入快但滚屏时读取显存慢
    // 根据实际测试选择最佳方案
    fb_enable_double_buffer();
    
    LOG_INFO_MSG("fb: Initialized %ux%u @ %ubpp (format=%d)\n",
                 fb_info.width, fb_info.height, fb_info.bpp, fb_info.format);
    LOG_INFO_MSG("fb: Physical=0x%x, Virtual=0x%x, Pitch=%u\n",
                 fb_info.address, (uint32_t)fb_info.buffer, fb_info.pitch);
    
    return 0;
}

bool fb_is_initialized(void) {
    return fb_initialized;
}

framebuffer_info_t *fb_get_info(void) {
    return fb_initialized ? &fb_info : NULL;
}

/* ============================================================================
 * 双缓冲支持
 * ============================================================================ */

/**
 * @brief 启用双缓冲
 * 
 * 双缓冲在普通内存（WB 模式）中维护后备缓冲区，所有绘制操作在后备缓冲区进行。
 * 这样滚屏等需要读取像素的操作会更快，因为普通内存的读取性能远好于 WC 显存。
 */
void fb_enable_double_buffer(void) {
    if (double_buffering || !fb_initialized) {
        return;
    }
    
    uint32_t fb_size = fb_info.pitch * fb_info.height;
    
    // 分配后备缓冲区（使用 kmalloc 在普通内存中分配）
    extern void *kmalloc(size_t size);
    back_buffer_mem = (uint8_t *)kmalloc(fb_size);
    
    if (!back_buffer_mem) {
        LOG_WARN_MSG("fb: Failed to allocate back buffer, double buffering disabled\n");
        return;
    }
    
    // 将当前帧缓冲内容复制到后备缓冲区
    memcpy(back_buffer_mem, fb_info.buffer, fb_size);
    
    double_buffering = true;
    dirty_line_start = -1;
    dirty_line_end = -1;
    
    LOG_INFO_MSG("fb: Double buffering enabled (%u KB back buffer)\n", fb_size / 1024);
}

/**
 * @brief 标记脏区域
 */
static inline void fb_mark_dirty(int y_start, int y_end) {
    if (!double_buffering) return;
    
    if (dirty_line_start < 0 || y_start < dirty_line_start) {
        dirty_line_start = y_start;
    }
    if (dirty_line_end < 0 || y_end > dirty_line_end) {
        dirty_line_end = y_end;
    }
}

/**
 * @brief 刷新后备缓冲区到显存
 * 
 * 只刷新脏区域以提升性能
 */
void fb_flush(void) {
    if (!double_buffering || !back_buffer_mem) {
        return;
    }
    
    if (dirty_line_start < 0 || dirty_line_end < 0) {
        return;  // 没有脏区域
    }
    
    // 限制脏区域范围
    if (dirty_line_start < 0) dirty_line_start = 0;
    if (dirty_line_end > (int)fb_info.height) dirty_line_end = fb_info.height;
    
    // 计算脏区域的起始偏移和大小
    uint32_t offset = dirty_line_start * fb_info.pitch;
    uint32_t size = (dirty_line_end - dirty_line_start) * fb_info.pitch;
    
    // 复制脏区域到显存
    memcpy((uint8_t *)fb_info.buffer + offset, back_buffer_mem + offset, size);
    
    // 清除脏标记
    dirty_line_start = -1;
    dirty_line_end = -1;
}

/**
 * @brief 强制刷新整个屏幕
 */
void fb_flush_all(void) {
    if (!double_buffering || !back_buffer_mem) {
        return;
    }
    
    uint32_t fb_size = fb_info.pitch * fb_info.height;
    memcpy(fb_info.buffer, back_buffer_mem, fb_size);
    
    dirty_line_start = -1;
    dirty_line_end = -1;
}

/**
 * @brief 获取当前绘图缓冲区
 * 
 * 如果启用了双缓冲，返回后备缓冲区；否则返回帧缓冲
 */
static inline uint8_t *fb_get_draw_buffer(void) {
    return double_buffering && back_buffer_mem ? back_buffer_mem : (uint8_t *)fb_info.buffer;
}

/* ============================================================================
 * 基础绘图函数
 * ============================================================================ */

void fb_clear(color_t color) {
    if (!fb_initialized) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint8_t *draw_buf = fb_get_draw_buffer();
    
    if (fb_info.bpp == 32) {
        // 32bpp：使用 32 位填充，效率最高
        uint32_t *fb = (uint32_t *)draw_buf;
        uint32_t count = fb_info.pitch * fb_info.height / 4;
        
        for (uint32_t i = 0; i < count; i++) {
            fb[i] = pixel;
        }
    } else {
        // 其他格式：逐像素填充
        for (uint32_t y = 0; y < fb_info.height; y++) {
            for (uint32_t x = 0; x < fb_info.width; x++) {
                fb_put_pixel_fast(x, y, pixel);
            }
        }
    }
    
    // 标记整个屏幕为脏
    if (double_buffering) {
        fb_mark_dirty(0, fb_info.height);
        fb_flush();  // 清屏后立即刷新
    }
}

void fb_put_pixel(int x, int y, color_t color) {
    if (!fb_initialized) return;
    if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) return;
    
    fb_put_pixel_fast(x, y, color_to_pixel(color));
}

color_t fb_get_pixel(int x, int y) {
    color_t c = {0, 0, 0, 255};
    
    if (!fb_initialized) return c;
    if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) return c;
    
    uint8_t *draw_buf = fb_get_draw_buffer();
    uint32_t offset = y * fb_info.pitch + x * (fb_info.bpp / 8);
    uint32_t pixel = 0;
    
    if (fb_info.bpp == 32) {
        pixel = *((uint32_t *)(draw_buf + offset));
    } else if (fb_info.bpp == 24) {
        pixel = draw_buf[offset] | (draw_buf[offset + 1] << 8) | (draw_buf[offset + 2] << 16);
    } else if (fb_info.bpp == 16) {
        pixel = *((uint16_t *)(draw_buf + offset));
    }
    
    return pixel_to_color(pixel);
}

void fb_draw_hline(int x, int y, int length, color_t color) {
    if (!fb_initialized) return;
    if (y < 0 || y >= (int)fb_info.height) return;
    
    // 裁剪到屏幕范围
    if (x < 0) { length += x; x = 0; }
    if (x + length > (int)fb_info.width) { length = fb_info.width - x; }
    if (length <= 0) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint8_t *draw_buf = fb_get_draw_buffer();
    
    if (fb_info.bpp == 32) {
        uint32_t *line = (uint32_t *)(draw_buf + y * fb_info.pitch);
        for (int i = 0; i < length; i++) {
            line[x + i] = pixel;
        }
    } else {
        for (int i = 0; i < length; i++) {
            fb_put_pixel_fast(x + i, y, pixel);
        }
    }
    
    if (double_buffering) {
        fb_mark_dirty(y, y + 1);
    }
}

void fb_draw_vline(int x, int y, int length, color_t color) {
    if (!fb_initialized) return;
    if (x < 0 || x >= (int)fb_info.width) return;
    
    // 裁剪到屏幕范围
    if (y < 0) { length += y; y = 0; }
    if (y + length > (int)fb_info.height) { length = fb_info.height - y; }
    if (length <= 0) return;
    
    uint32_t pixel = color_to_pixel(color);
    
    for (int i = 0; i < length; i++) {
        fb_put_pixel_fast(x, y + i, pixel);
    }
}

void fb_draw_line(int x1, int y1, int x2, int y2, color_t color) {
    if (!fb_initialized) return;
    
    // Bresenham 算法
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        fb_put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void fb_draw_rect(int x, int y, int width, int height, color_t color) {
    if (!fb_initialized) return;
    
    fb_draw_hline(x, y, width, color);
    fb_draw_hline(x, y + height - 1, width, color);
    fb_draw_vline(x, y, height, color);
    fb_draw_vline(x + width - 1, y, height, color);
}

void fb_fill_rect(int x, int y, int width, int height, color_t color) {
    if (!fb_initialized) return;
    
    // 裁剪到屏幕范围
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > (int)fb_info.width) { width = fb_info.width - x; }
    if (y + height > (int)fb_info.height) { height = fb_info.height - y; }
    if (width <= 0 || height <= 0) return;
    
    uint32_t pixel = color_to_pixel(color);
    uint32_t bytes_per_pixel = fb_info.bpp / 8;
    uint8_t *draw_buf = fb_get_draw_buffer();
    
    for (int row = 0; row < height; row++) {
        uint8_t *line = draw_buf + (y + row) * fb_info.pitch + x * bytes_per_pixel;
        
        if (fb_info.bpp == 32) {
            uint32_t *p = (uint32_t *)line;
            for (int col = 0; col < width; col++) {
                p[col] = pixel;
            }
        } else {
            for (int col = 0; col < width; col++) {
                fb_put_pixel_fast(x + col, y + row, pixel);
            }
        }
    }
    
    if (double_buffering) {
        fb_mark_dirty(y, y + height);
    }
}

/* ============================================================================
 * 位图操作
 * ============================================================================ */

void fb_blit(int x, int y, int width, int height, const uint32_t *data) {
    if (!fb_initialized || !data) return;
    
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int px = x + col;
            int py = y + row;
            
            if (px >= 0 && px < (int)fb_info.width &&
                py >= 0 && py < (int)fb_info.height) {
                uint32_t pixel = data[row * width + col];
                color_t c;
                c.a = (pixel >> 24) & 0xFF;
                c.r = (pixel >> 16) & 0xFF;
                c.g = (pixel >> 8) & 0xFF;
                c.b = pixel & 0xFF;
                fb_put_pixel(px, py, c);
            }
        }
    }
}

void fb_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int width, int height) {
    if (!fb_initialized) return;
    
    // 简单实现：逐行复制
    // 注意处理重叠区域
    if (dst_y < src_y || (dst_y == src_y && dst_x < src_x)) {
        // 从上到下，从左到右
        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width; col++) {
                color_t c = fb_get_pixel(src_x + col, src_y + row);
                fb_put_pixel(dst_x + col, dst_y + row, c);
            }
        }
    } else {
        // 从下到上，从右到左
        for (int row = height - 1; row >= 0; row--) {
            for (int col = width - 1; col >= 0; col--) {
                color_t c = fb_get_pixel(src_x + col, src_y + row);
                fb_put_pixel(dst_x + col, dst_y + row, c);
            }
        }
    }
}

/* ============================================================================
 * 文本渲染
 * ============================================================================ */

void fb_set_font(const uint8_t *font_data, int char_width, int char_height) {
    if (font_data) {
        current_font = font_data;
        font_width = char_width;
        font_height = char_height;
    }
}

void fb_draw_char(int x, int y, char c, color_t fg, color_t bg) {
    if (!fb_initialized || !current_font) return;
    
    const uint8_t *glyph = current_font + (unsigned char)c * font_height;
    
    for (int row = 0; row < font_height; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < font_width; col++) {
            color_t color = (bits & (0x80 >> col)) ? fg : bg;
            int px = x + col;
            int py = y + row;
            
            if (px >= 0 && px < (int)fb_info.width &&
                py >= 0 && py < (int)fb_info.height) {
                fb_put_pixel_fast(px, py, color_to_pixel(color));
            }
        }
    }
}

void fb_draw_char_transparent(int x, int y, char c, color_t fg) {
    if (!fb_initialized || !current_font) return;
    
    const uint8_t *glyph = current_font + (unsigned char)c * font_height;
    uint32_t pixel = color_to_pixel(fg);
    
    for (int row = 0; row < font_height; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < font_width; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                int py = y + row;
                
                if (px >= 0 && px < (int)fb_info.width &&
                    py >= 0 && py < (int)fb_info.height) {
                    fb_put_pixel_fast(px, py, pixel);
                }
            }
        }
    }
}

void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg) {
    if (!str) return;
    
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += font_height;
        } else if (*str == '\t') {
            cx += font_width * 4;  // Tab = 4 spaces
        } else {
            fb_draw_char(cx, y, *str, fg, bg);
            cx += font_width;
        }
        str++;
    }
}

void fb_draw_string_transparent(int x, int y, const char *str, color_t fg) {
    if (!str) return;
    
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += font_height;
        } else if (*str == '\t') {
            cx += font_width * 4;
        } else {
            fb_draw_char_transparent(cx, y, *str, fg);
            cx += font_width;
        }
        str++;
    }
}

int fb_get_font_width(void) {
    return font_width;
}

int fb_get_font_height(void) {
    return font_height;
}

int fb_get_cols(void) {
    if (!fb_initialized) return 0;
    return fb_info.width / font_width;
}

int fb_get_rows(void) {
    if (!fb_initialized) return 0;
    return fb_info.height / font_height;
}

/* ============================================================================
 * 终端仿真
 * ============================================================================ */

void fb_terminal_init(void) {
    if (!fb_initialized) return;
    
    term_cursor_col = 0;
    term_cursor_row = 0;
    term_fg = vga_palette[7];   // LIGHT_GREY (VGA 默认)
    term_bg = vga_palette[0];   // BLACK
    
    // 重置 ANSI 解析状态
    ansi_state = ANSI_NORMAL;
    ansi_param_count = 0;
    ansi_bold = false;
    
    fb_clear(term_bg);
}

void fb_terminal_clear(void) {
    if (!fb_initialized) return;
    
    fb_clear(term_bg);
    term_cursor_col = 0;
    term_cursor_row = 0;
    
    // 重置 ANSI 解析状态
    ansi_state = ANSI_NORMAL;
    ansi_param_count = 0;
}

void fb_terminal_scroll(int lines) {
    if (!fb_initialized || lines <= 0) return;
    
    int scroll_height = lines * font_height;
    int remaining_height = fb_info.height - scroll_height;
    
    if (remaining_height > 0) {
        // 使用 memmove 进行高效滚动（处理重叠区域）
        // 在双缓冲模式下，这在普通内存中进行，速度很快
        uint8_t *draw_buf = fb_get_draw_buffer();
        memmove(draw_buf, draw_buf + scroll_height * fb_info.pitch, 
                remaining_height * fb_info.pitch);
        
        // 清空底部区域（需要手动填充，因为 fb_fill_rect 会标记脏区域）
        uint32_t pixel = color_to_pixel(term_bg);
        if (fb_info.bpp == 32) {
            uint32_t *p = (uint32_t *)(draw_buf + remaining_height * fb_info.pitch);
            uint32_t count = (scroll_height * fb_info.pitch) / 4;
            for (uint32_t i = 0; i < count; i++) {
                p[i] = pixel;
            }
        } else {
            for (int y = remaining_height; y < (int)fb_info.height; y++) {
                for (int x = 0; x < (int)fb_info.width; x++) {
                    fb_put_pixel_fast(x, y, pixel);
                }
            }
        }
        
        // 标记整个屏幕为脏（因为所有内容都移动了）
        if (double_buffering) {
            fb_mark_dirty(0, fb_info.height);
        }
    } else {
        fb_clear(term_bg);
    }
}

void fb_terminal_putchar(char c) {
    if (!fb_initialized) return;
    
    int max_cols = fb_get_cols();
    int max_rows = fb_get_rows();
    
    // ANSI 转义序列解析
    if (ansi_state == ANSI_NORMAL) {
        if (c == '\033' || c == 0x1B) {
            ansi_state = ANSI_ESCAPE;
            return;
        }
    } else if (ansi_state == ANSI_ESCAPE) {
        if (c == '[') {
            ansi_state = ANSI_BRACKET;
            ansi_param_count = 0;
            return;
        } else {
            ansi_state = ANSI_NORMAL;
        }
    } else if (ansi_state == ANSI_BRACKET || ansi_state == ANSI_PARAM) {
        if (c >= '0' && c <= '9') {
            if (ansi_param_count == 0) {
                ansi_param_count = 1;
                ansi_params[0] = 0;
            }
            ansi_params[ansi_param_count - 1] = 
                ansi_params[ansi_param_count - 1] * 10 + (c - '0');
            ansi_state = ANSI_PARAM;
            return;
        } else if (c == ';') {
            if (ansi_param_count < ANSI_MAX_PARAMS) {
                if (ansi_param_count == 0) {
                    ansi_param_count = 1;
                    ansi_params[0] = 0;
                }
                ansi_param_count++;
                ansi_params[ansi_param_count - 1] = 0;
            }
            return;
        } else if (c == 'm') {
            // SGR - 设置颜色和属性
            fb_handle_sgr();
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'J') {
            // 清屏命令
            int param = (ansi_param_count > 0) ? ansi_params[0] : 0;
            if (param == 2 || param == 0) {
                fb_terminal_clear();
            }
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'H') {
            // 光标定位
            int row = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
            int col = (ansi_param_count > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
            term_cursor_row = (row < max_rows) ? row : max_rows - 1;
            term_cursor_col = (col < max_cols) ? col : max_cols - 1;
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'A') {
            // 光标上移
            int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
            term_cursor_row = (term_cursor_row >= n) ? (term_cursor_row - n) : 0;
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'B') {
            // 光标下移
            int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
            term_cursor_row = (term_cursor_row + n < max_rows) ? (term_cursor_row + n) : (max_rows - 1);
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'C') {
            // 光标右移
            int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
            term_cursor_col = (term_cursor_col + n < max_cols) ? (term_cursor_col + n) : (max_cols - 1);
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else if (c == 'D') {
            // 光标左移
            int n = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] : 1;
            term_cursor_col = (term_cursor_col >= n) ? (term_cursor_col - n) : 0;
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
            return;
        } else {
            // 未知命令，重置状态
            ansi_state = ANSI_NORMAL;
            ansi_param_count = 0;
        }
    }
    
    // 正常字符处理
    switch (c) {
        case '\n':
            term_cursor_col = 0;
            term_cursor_row++;
            break;
            
        case '\r':
            term_cursor_col = 0;
            break;
            
        case '\t':
            term_cursor_col = (term_cursor_col + 4) & ~3;
            break;
            
        case '\b':
            if (term_cursor_col > 0) {
                term_cursor_col--;
                // 清除字符
                fb_fill_rect(term_cursor_col * font_width, 
                            term_cursor_row * font_height,
                            font_width, font_height, term_bg);
            }
            break;
            
        default:
            // 绘制字符
            fb_draw_char(term_cursor_col * font_width, 
                        term_cursor_row * font_height,
                        c, term_fg, term_bg);
            term_cursor_col++;
            break;
    }
    
    // 处理换行
    if (term_cursor_col >= max_cols) {
        term_cursor_col = 0;
        term_cursor_row++;
    }
    
    // 处理滚动
    if (term_cursor_row >= max_rows) {
        fb_terminal_scroll(1);
        term_cursor_row = max_rows - 1;
        // 注意：不在这里刷新，由调用者决定何时刷新
        // 这样连续输出多行时只需刷新一次
    }
    
    // 注意：不在每次换行时刷新，由调用者决定何时刷新
    // fb_terminal_write, kprintf, devconsole_write 会在结束时刷新
}

void fb_terminal_write(const char *str) {
    if (!str) return;
    
    while (*str) {
        fb_terminal_putchar(*str);
        str++;
    }
    
    // 写完整个字符串后刷新
    fb_flush();
}

void fb_terminal_set_color(color_t fg, color_t bg) {
    term_fg = fg;
    term_bg = bg;
}

void fb_terminal_set_cursor(int col, int row) {
    term_cursor_col = col;
    term_cursor_row = row;
    
    int max_cols = fb_get_cols();
    int max_rows = fb_get_rows();
    
    if (term_cursor_col < 0) term_cursor_col = 0;
    if (term_cursor_col >= max_cols) term_cursor_col = max_cols - 1;
    if (term_cursor_row < 0) term_cursor_row = 0;
    if (term_cursor_row >= max_rows) term_cursor_row = max_rows - 1;
}

int fb_terminal_get_cursor_col(void) {
    return term_cursor_col;
}

int fb_terminal_get_cursor_row(void) {
    return term_cursor_row;
}

/* ============================================================================
 * VGA 兼容函数
 * ============================================================================ */

/**
 * 将 VGA 颜色索引转换为 RGB 颜色
 */
color_t fb_vga_to_color(uint8_t vga_color) {
    if (vga_color > 15) vga_color = 15;
    return vga_palette[vga_color];
}

/**
 * 使用 VGA 颜色设置终端颜色
 */
void fb_terminal_set_vga_color(uint8_t fg, uint8_t bg) {
    term_fg = fb_vga_to_color(fg);
    term_bg = fb_vga_to_color(bg);
}

/**
 * 处理 ANSI SGR (Select Graphic Rendition) 命令
 */
static void fb_handle_sgr(void) {
    if (ansi_param_count == 0) {
        ansi_params[0] = 0;
        ansi_param_count = 1;
    }
    
    for (int i = 0; i < ansi_param_count; i++) {
        int code = ansi_params[i];
        
        if (code == 0) {
            // 重置所有属性
            term_fg = vga_palette[7];  // LIGHT_GREY
            term_bg = vga_palette[0];  // BLACK
            ansi_bold = false;
        } else if (code == 1) {
            // 粗体/高亮
            ansi_bold = true;
        } else if (code == 22) {
            // 取消粗体
            ansi_bold = false;
        } else if (code >= 30 && code <= 37) {
            // 普通前景色
            uint8_t idx = ansi_to_vga_fg[code - 30];
            if (ansi_bold && idx < 8) idx += 8;
            term_fg = vga_palette[idx];
        } else if (code == 39) {
            // 默认前景色
            term_fg = vga_palette[7];
        } else if (code >= 40 && code <= 47) {
            // 普通背景色
            term_bg = vga_palette[ansi_to_vga_fg[code - 40]];
        } else if (code == 49) {
            // 默认背景色
            term_bg = vga_palette[0];
        } else if (code >= 90 && code <= 97) {
            // 亮前景色
            term_fg = vga_palette[ansi_to_vga_bright[code - 90]];
        } else if (code >= 100 && code <= 107) {
            // 亮背景色
            term_bg = vga_palette[ansi_to_vga_bright[code - 100]];
        }
    }
}

/* ============================================================================
 * 双缓冲兼容接口
 * ============================================================================ */

bool fb_set_double_buffer(bool enable) {
    if (enable) {
        fb_enable_double_buffer();
        return double_buffering;
    }
    // 禁用双缓冲暂不支持
    return false;
}

void fb_swap_buffers(void) {
    fb_flush_all();
}

/* ============================================================================
 * 调试和工具函数
 * ============================================================================ */

void fb_print_info(void) {
    if (!fb_initialized) {
        kprintf("Framebuffer: Not initialized\n");
        return;
    }
    
    const char *format_names[] = {
        "RGB565", "RGB888", "ARGB8888", "BGRA8888", "Unknown"
    };
    
    kprintf("\n===== Framebuffer Info =====\n");
    kprintf("Resolution: %ux%u\n", fb_info.width, fb_info.height);
    kprintf("Bits per pixel: %u\n", fb_info.bpp);
    kprintf("Pitch: %u bytes per line\n", fb_info.pitch);
    kprintf("Format: %s\n", format_names[fb_info.format]);
    kprintf("Physical address: 0x%08x\n", fb_info.address);
    kprintf("Virtual address: 0x%08x\n", (uint32_t)fb_info.buffer);
    kprintf("Total size: %u KB\n", (fb_info.pitch * fb_info.height) / 1024);
    kprintf("Text mode: %u cols x %u rows\n", fb_get_cols(), fb_get_rows());
    kprintf("Color masks: R(%u@%u) G(%u@%u) B(%u@%u)\n",
            fb_info.red_mask_size, fb_info.red_field_pos,
            fb_info.green_mask_size, fb_info.green_field_pos,
            fb_info.blue_mask_size, fb_info.blue_field_pos);
    kprintf("============================\n");
}

void fb_demo(void) {
    if (!fb_initialized) return;
    
    // 清屏为深蓝色
    fb_clear((color_t){16, 24, 48, 255});
    
    // 绘制彩色矩形
    int rect_width = 80;
    int rect_height = 60;
    int start_x = 50;
    int start_y = 50;
    
    fb_fill_rect(start_x, start_y, rect_width, rect_height, COLOR_RED);
    fb_fill_rect(start_x + rect_width + 10, start_y, rect_width, rect_height, COLOR_GREEN);
    fb_fill_rect(start_x + (rect_width + 10) * 2, start_y, rect_width, rect_height, COLOR_BLUE);
    fb_fill_rect(start_x + (rect_width + 10) * 3, start_y, rect_width, rect_height, COLOR_YELLOW);
    
    // 绘制线条
    int line_y = start_y + rect_height + 30;
    fb_draw_line(50, line_y, 350, line_y + 50, COLOR_WHITE);
    fb_draw_line(50, line_y + 50, 350, line_y, COLOR_CYAN);
    
    // 绘制边框矩形
    fb_draw_rect(50, line_y + 70, 300, 100, COLOR_MAGENTA);
    
    // 绘制渐变
    int gradient_y = line_y + 200;
    for (int i = 0; i < 256; i++) {
        color_t c = {(uint8_t)i, 0, (uint8_t)(255 - i), 255};
        fb_draw_vline(50 + i, gradient_y, 30, c);
    }
    
    // 显示文本
    int text_y = gradient_y + 50;
    fb_draw_string(50, text_y, "CastorOS Graphics Mode Demo", COLOR_WHITE, COLOR_BLACK);
    
    char res_info[64];
    snprintf(res_info, sizeof(res_info), "Resolution: %ux%u @ %ubpp", 
             fb_info.width, fb_info.height, fb_info.bpp);
    fb_draw_string(50, text_y + 20, res_info, COLOR_LIGHT_GRAY, COLOR_BLACK);
    
    fb_draw_string(50, text_y + 40, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", COLOR_YELLOW, COLOR_BLACK);
    fb_draw_string(50, text_y + 60, "abcdefghijklmnopqrstuvwxyz", COLOR_CYAN, COLOR_BLACK);
    fb_draw_string(50, text_y + 80, "0123456789 !@#$%^&*()+-=[]{}|;':\",./<>?", COLOR_GREEN, COLOR_BLACK);
}

