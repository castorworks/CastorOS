#ifndef _KERNEL_SYSCALLS_PROCESS_H_
#define _KERNEL_SYSCALLS_PROCESS_H_

#include <types.h>

namespace syscall {

/**
 * @brief 进程管理相关系统调用
 */
class Process {
public:
    /**
     * 进程管理系统调用
     */

    /**
     * syscall::Process::exit - 退出当前进程
     * @param code 退出码
     * @note 此函数不会返回
     */
    static void exit(uint32_t code) __attribute__((noreturn));

    /**
     * syscall::Process::fork - 创建子进程
     * @param frame 系统调用栈帧指针（架构相关大小）
     * @return 父进程返回子进程 PID，子进程返回 0，失败返回 -1
     */
    static uint32_t fork(uintptr_t *frame);

    /**
     * syscall::Process::execve - 执行新程序（替换当前进程）
     * @param frame 系统调用栈帧指针（架构相关大小）
     * @param path  程序路径
     * @return 成功则返回 0（通过修改 frame 返回到新程序），失败返回 -1
     */
    static uint32_t execve(uintptr_t *frame, const char *path);

    /**
     * syscall::Process::getpid - 获取当前进程 PID
     * @return 当前进程的 PID
     */
    static uint32_t getpid();

    /**
     * syscall::Process::getppid - 获取父进程 PID
     * @return 父进程 PID，如果没有父进程返回 0
     */
    static uint32_t getppid();

    /**
     * syscall::Process::yield - 主动让出 CPU
     * @return 总是返回 0
     */
    static uint32_t yield();

    /**
     * syscall::Process::nanosleep - 睡眠指定时间
     * @param req  请求的睡眠时间
     * @param rem  剩余时间（若非 NULL）
     * @return 0 成功，(uint32_t)-1 失败
     */
    static uint32_t nanosleep(const struct timespec *req, struct timespec *rem);

    /**
     * syscall::Process::kill - 向进程发送信号
     * @param pid    目标进程 PID
     * @param signal 信号号
     * @return 0 成功，(uint32_t)-1 失败
     */
    static uint32_t kill(uint32_t pid, uint32_t signal);

    /**
     * syscall::Process::waitpid - 等待子进程退出
     * @param pid     要等待的进程 PID（-1 表示任意子进程）
     * @param wstatus 退出状态存储地址（可为 NULL）
     * @param options 等待选项（如 WNOHANG）
     * @return 成功返回子进程 PID，失败返回 (uint32_t)-1
     */
    static uint32_t waitpid(int32_t pid, uint32_t *wstatus, uint32_t options);
};

} // namespace syscall

#endif // _KERNEL_SYSCALLS_PROCESS_H_
