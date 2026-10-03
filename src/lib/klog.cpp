// ============================================================================
// klog.c - 内核日志系统
// ============================================================================

#include <lib/klog.h>
#include <lib/kprintf.h>
#include <stdarg.h>

/* 当前日志等级阈值（默认 INFO，过滤 DEBUG） */
static log_level_t current_log_level = LOG_INFO;

/* 当前日志输出目标（默认同时输出到 VGA 和 Serial） */
static log_target_t current_log_target = LOG_TARGET_BOTH;

/* ANSI 颜色码定义 */
#define ANSI_RESET   "\033[0m"
#define ANSI_GRAY    "\033[90m"    // 亮黑（灰色）- DEBUG
#define ANSI_CYAN    "\033[36m"    // 青色 - INFO
#define ANSI_YELLOW  "\033[33m"    // 黄色 - WARN
#define ANSI_RED     "\033[31m"    // 红色 - ERROR
#define ANSI_BOLD    "\033[1m"     // 粗体

void klog_set_level(log_level_t level) {
    current_log_level = level;
}

log_level_t klog_get_level(void) {
    return current_log_level;
}

void klog_set_target(log_target_t target) {
    current_log_target = target;
}

log_target_t klog_get_target(void) {
    return current_log_target;
}

/**
 * 根据当前目标设置输出字符串
 */
static void log_output(const char *str) {
    if (current_log_target & LOG_TARGET_SERIAL) {
        kprint_serial(str);
    }
    if (current_log_target & LOG_TARGET_VGA) {
        kprint_vga(str);
    }
}

/**
 * 根据当前目标进行格式化输出
 */
static void log_vprintf(const char *fmt, va_list args) {
    if (current_log_target & LOG_TARGET_SERIAL) {
        va_list args_copy;
        va_copy(args_copy, args);
        vkprintf_serial(fmt, args_copy);
        va_end(args_copy);
    }
    if (current_log_target & LOG_TARGET_VGA) {
        va_list args_copy;
        va_copy(args_copy, args);
        vkprintf_vga(fmt, args_copy);
        va_end(args_copy);
    }
}

void klog(log_level_t level, const char *fmt, ...) {
    // 过滤低等级日志
    if (level < current_log_level) {
        return;
    }
    
    // 根据等级选择颜色和前缀
    const char *color_code;
    const char *prefix;
    
    switch (level) {
        case LOG_DEBUG:
            color_code = ANSI_GRAY;
            prefix = "[DEBUG] ";
            break;
        case LOG_INFO:
            color_code = ANSI_CYAN;
            prefix = "[INFO]  ";
            break;
        case LOG_WARN:
            color_code = ANSI_BOLD ANSI_YELLOW;
            prefix = "[WARN]  ";
            break;
        case LOG_ERROR:
            color_code = ANSI_BOLD ANSI_RED;
            prefix = "[ERROR] ";
            break;
        default:
            color_code = ANSI_RESET;
            prefix = "[????]  ";
            break;
    }
    
    // 输出：颜色码 + 前缀
    log_output(color_code);
    log_output(prefix);
    
    // 格式化输出消息内容
    va_list args;
    va_start(args, fmt);
    log_vprintf(fmt, args);
    va_end(args);
    
    // 重置颜色
    log_output(ANSI_RESET);
}
