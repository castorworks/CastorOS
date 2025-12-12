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

/**
 * Check if framebuffer is initialized
 * @return true if initialized
 */
bool fb_is_initialized(void);

/**
 * Get framebuffer info
 * @return Pointer to framebuffer info, or NULL if not initialized
 */
framebuffer_info_t *fb_get_info(void);

/**
 * Clear screen with color
 * @param color Fill color
 */
void fb_clear(color_t color);

/**
 * Terminal functions
 */
void fb_terminal_init(void);
void fb_terminal_clear(void);
void fb_terminal_putchar(char c);
void fb_terminal_write(const char *str);
void fb_terminal_set_vga_color(uint8_t fg, uint8_t bg);
void fb_flush(void);

#endif /* _DRIVERS_ARM_FRAMEBUFFER_H_ */
