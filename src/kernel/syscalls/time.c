/**
 * 时间相关系统调用实现
 * 
 * 实现 POSIX 标准的时间系统调用：
 * - time(2)
 */

#include <kernel/syscalls/time.h>
#include <drivers/timer.h>
#include <lib/klog.h>

/**
 * sys_time - 获取系统运行时间（秒）
 * 
 * 返回自系统启动以来的秒数
 * 
 * @return 系统运行时间（秒）
 */
uint32_t sys_time(void) {
    uint32_t uptime_sec = timer_get_uptime_sec();
    LOG_DEBUG_MSG("sys_time: returning %u seconds\n", uptime_sec);
    return uptime_sec;
}

