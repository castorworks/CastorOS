#include <lib/klog.h>
#include <lib/kprintf.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <stdarg.h>

/* 当前日志等级阈值 */
static log_level_t current_log_level = LOG_INFO;

void klog_set_level(log_level_t level) {
    current_log_level = level;
}

log_level_t klog_get_level(void) {
    return current_log_level;
}

void klog(log_level_t level, const char *fmt, ...) {
    // 过滤低等级日志
    if (level < current_log_level) {
        return;
    }
    
    // 保存当前 VGA 颜色
    uint8_t old_color = vga_get_color();
    
    // 根据等级选择颜色和前缀
    const char *prefix;
    vga_color_t fg_color;
    
    switch (level) {
        case LOG_DEBUG:
            prefix = "[DEBUG] ";
            fg_color = VGA_COLOR_DARK_GREY;
            break;
        case LOG_INFO:
            prefix = "[INFO]  ";
            fg_color = VGA_COLOR_WHITE;
            break;
        case LOG_WARN:
            prefix = "[WARN]  ";
            fg_color = VGA_COLOR_YELLOW;
            break;
        case LOG_ERROR:
            prefix = "[ERROR] ";
            fg_color = VGA_COLOR_LIGHT_RED;
            break;
        default:
            prefix = "[????]  ";
            fg_color = VGA_COLOR_WHITE;
            break;
    }
    
    // 打印前缀（带颜色）
    serial_print(prefix);
    vga_set_color(fg_color, VGA_COLOR_BLACK);
    vga_print(prefix);
    
    // 恢复颜色并打印消息
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    // 使用 vkprintf 进行格式化输出
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
    
    // 恢复原始颜色
    vga_set_color(old_color & 0x0F, (old_color >> 4) & 0x0F);
}
