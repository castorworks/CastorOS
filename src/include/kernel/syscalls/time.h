#ifndef _KERNEL_SYSCALLS_TIME_H_
#define _KERNEL_SYSCALLS_TIME_H_

#include <types.h>

namespace syscall {

/**
 * @brief 时间相关系统调用
 */
class Time {
public:
    /**
     * 时间相关系统调用
     */

    /**
     * syscall::Time::time - 获取系统运行时间（秒）
     * @return 自系统启动以来的秒数
     */
    static uint32_t time();
};

} // namespace syscall

#endif // _KERNEL_SYSCALLS_TIME_H_
