#ifndef _KERNEL_SYSCALLS_SYSTEM_H_
#define _KERNEL_SYSCALLS_SYSTEM_H_

#include <types.h>

struct utsname;  // 前向声明

namespace syscall {

/**
 * @brief 系统控制相关系统调用
 */
class System {
public:
    /**
     * syscall::System::reboot - 重启系统
     * @return 不返回（成功时系统重启）
     */
    static uint32_t reboot();

    /**
     * syscall::System::poweroff - 关闭系统
     * @return 不返回（成功时系统关机）
     */
    static uint32_t poweroff();

    /**
     * syscall::System::uname - 获取系统信息
     * @param buf 用户空间 utsname 结构体指针
     * @return 0 成功，(uint32_t)-1 失败
     */
    static uint32_t uname(struct utsname *buf);
};

} // namespace syscall

#endif /* _KERNEL_SYSCALLS_SYSTEM_H_ */
