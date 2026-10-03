/**
 * @file framebuffer.h
 * @brief ARM64 Framebuffer driver header
 * 
 * Placeholder for ARM64 framebuffer driver.
 * ARM64 typically uses SimpleFB or UEFI GOP for framebuffer.
 */

#ifndef _DRIVERS_ARM_FRAMEBUFFER_H_
#define _DRIVERS_ARM_FRAMEBUFFER_H_

#include <types.h>

/**
 * Color structure
 */
typedef struct {
    uint8_t r, g, b, a;
} color_t;

/**
 * Framebuffer pixel format
 */
typedef enum {
    FB_FORMAT_UNKNOWN = 0,
    FB_FORMAT_RGB888,
    FB_FORMAT_ARGB8888,
    FB_FORMAT_BGRA8888,
    FB_FORMAT_RGB565,
} fb_format_t;

/**
 * Framebuffer information
 */
typedef struct {
    uint32_t *buffer;
    uint32_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    fb_format_t format;
    uint8_t red_mask_size, red_field_pos;
    uint8_t green_mask_size, green_field_pos;
    uint8_t blue_mask_size, blue_field_pos;
} framebuffer_info_t;

namespace drivers {

/**
 * @brief 帧缓冲与图形终端
 */
class Framebuffer {
public:
    /**
     * Check if framebuffer is initialized
     * @return true if initialized
     */
    static bool is_initialized();

    /**
     * Get framebuffer info
     * @return Pointer to framebuffer info, or NULL if not initialized
     */
    static framebuffer_info_t *get_info();

    /**
     * Clear screen with color
     * @param color Fill color
     */
    static void clear(color_t color);

    /**
     * Terminal functions
     */
    static void terminal_init();
    static void terminal_clear();
    static void terminal_putchar(char c);
    static void terminal_write(const char *str);
    static void terminal_set_vga_color(uint8_t fg, uint8_t bg);
    static void flush();

    /* 绘图与终端尺寸（与 x86 驱动保持同名接口） */
    static int get_cols();
    static int get_rows();
    static void draw_char(int x, int y, char c, color_t fg, color_t bg);
    static void fill_rect(int x, int y, int width, int height, color_t color);
};

} // namespace drivers

#endif /* _DRIVERS_ARM_FRAMEBUFFER_H_ */
