#ifndef _LIB_KLOG_H_
#define _LIB_KLOG_H_

/**
 * 内核日志系统
 * 
 * 提供分级日志输出功能，支持：
 * - 根据日志等级过滤
 * - ANSI 彩色输出（VGA 和串口终端）
 * - 可配置输出目标（VGA、串口或两者）
 */

/* 日志等级 */
typedef enum {
    LOG_DEBUG = 0,  // 调试信息（灰色）
    LOG_INFO  = 1,  // 普通信息（青色）
    LOG_WARN  = 2,  // 警告信息（黄色）
    LOG_ERROR = 3,  // 错误信息（红色）
} log_level_t;

/* 日志输出目标 */
typedef enum {
    LOG_TARGET_SERIAL = 0x01,  // 仅串口
    LOG_TARGET_VGA    = 0x02,  // 仅 VGA
    LOG_TARGET_BOTH   = 0x03,  // 两者都输出（默认）
} log_target_t;

/**
 * 设置当前日志等级阈值
 * 只有等级 >= 阈值的日志才会输出
 * @param level 日志等级阈值
 */
void klog_set_level(log_level_t level);

/**
 * 获取当前日志等级阈值
 * @return 当前日志等级阈值
 */
log_level_t klog_get_level(void);

/**
 * 设置日志输出目标
 * @param target 输出目标（LOG_TARGET_SERIAL, LOG_TARGET_VGA, LOG_TARGET_BOTH）
 */
void klog_set_target(log_target_t target);

/**
 * 获取当前日志输出目标
 * @return 当前输出目标
 */
log_target_t klog_get_target(void);

/**
 * 输出日志（带等级和颜色）
 * @param level 日志等级
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void klog(log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/**
 * 便捷宏：输出调试信息
 */
#define LOG_DEBUG_MSG(fmt, ...) klog(LOG_DEBUG, fmt, ##__VA_ARGS__)

/**
 * 便捷宏：输出普通信息
 */
#define LOG_INFO_MSG(fmt, ...) klog(LOG_INFO, fmt, ##__VA_ARGS__)

/**
 * 便捷宏：输出警告信息
 */
#define LOG_WARN_MSG(fmt, ...) klog(LOG_WARN, fmt, ##__VA_ARGS__)

/**
 * 便捷宏：输出错误信息
 */
#define LOG_ERROR_MSG(fmt, ...) klog(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif /* _LIB_KLOG_H_ */
