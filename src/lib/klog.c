#include <lib/klog.h>
#include <lib/kprintf.h>
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
    
    // 根据等级选择颜色和前缀
    const char *prefix;
    
    switch (level) {
        case LOG_DEBUG:
            prefix = "[DEBUG] ";
            break;
        case LOG_INFO:
            prefix = "[INFO]  ";
            break;
        case LOG_WARN:
            prefix = "[WARN]  ";
            break;
        case LOG_ERROR:
            prefix = "[ERROR] ";
            break;
        default:
            prefix = "[????]  ";
            break;
    }
    
    // 打印前缀
    // Serial: 不带颜色，直接输出
    kprint_serial(prefix);
    
    // 格式化输出消息
    // Serial: 不带颜色，直接输出
    va_list args;
    va_start(args, fmt);
    vkprintf_serial(fmt, args);
    va_end(args);    
}
