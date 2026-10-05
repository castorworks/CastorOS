// ============================================================================
// klog.c - 内核日志系统
// ============================================================================

#include <lib/klog.h>
#include <lib/kprintf.h>
#include <kernel/interrupt.h>
#include <stdarg.h>

/* 当前日志等级阈值（默认 INFO，过滤 DEBUG） */
static log_level_t current_log_level = LOG_INFO;

/* ANSI 颜色码定义 */
#define ANSI_RESET   "\033[0m"
#define ANSI_GRAY    "\033[90m"    // 亮黑（灰色）- DEBUG
#define ANSI_CYAN    "\033[36m"    // 青色 - INFO
#define ANSI_YELLOW  "\033[33m"    // 黄色 - WARN
#define ANSI_RED     "\033[31m"    // 红色 - ERROR
#define ANSI_BOLD    "\033[1m"     // 粗体

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
    
    // 整条日志（颜色码、前缀、正文、复位）作为一个整体输出
    kernel::InterruptGuard guard;
    
    // 输出：颜色码 + 前缀
    kprint(color_code);
    kprint(prefix);
    
    // 格式化输出消息内容
    va_list args;
    va_start(args, fmt);
    vkprintf(fmt, args);
    va_end(args);
    
    // 重置颜色
    kprint(ANSI_RESET);
}
