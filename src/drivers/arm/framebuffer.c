/**
 * @file framebuffer.c
 * @brief ARM64 Framebuffer driver stub
 * 
 * Placeholder implementation for ARM64 framebuffer.
 * ARM64 typically uses SimpleFB or UEFI GOP for framebuffer.
 */

#include <drivers/arm/framebuffer.h>

/* Framebuffer not yet implemented for ARM64 */
static bool fb_initialized = false;

bool fb_is_initialized(void) {
    return fb_initialized;
}

framebuffer_info_t *fb_get_info(void) {
    return NULL;
}

void fb_clear(color_t color) {
    (void)color;
}

void fb_terminal_init(void) {
}

void fb_terminal_clear(void) {
}

void fb_terminal_putchar(char c) {
    (void)c;
}

void fb_terminal_write(const char *str) {
    (void)str;
}

void fb_terminal_set_vga_color(uint8_t fg, uint8_t bg) {
    (void)fg;
    (void)bg;
}

void fb_flush(void) {
}
