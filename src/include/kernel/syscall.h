#ifndef _KERNEL_SYSCALL_H_
#define _KERNEL_SYSCALL_H_

#include <types.h>   // uint32_t, size_t, pid_t 等定义

// 系统调用参数使用 uintptr_t 以支持 32 位和 64 位架构
typedef uintptr_t syscall_arg_t;

// ============================================================================
// 系统调用号（与 user/lib/include/syscall.h 保持一致）
//
// 内核只提供进程、内存和调试控制台三类调用；文件系统、网络、设备驱动
// 不在内核里。
// ============================================================================

enum {
    // 进程
    SYS_EXIT            = 0,
    SYS_FORK            = 1,
    SYS_EXEC            = 2,   // exec(image, size)：用用户内存里的 ELF 映像替换当前进程
    SYS_WAITPID         = 3,
    SYS_GETPID          = 4,
    SYS_GETPPID         = 5,
    SYS_SCHED_YIELD     = 6,
    SYS_KILL            = 7,
    SYS_NANOSLEEP       = 8,

    // 内存
    SYS_BRK             = 9,
    SYS_MMAP            = 10,  // 只支持匿名映射
    SYS_MUNMAP          = 11,

    // 调试控制台（串口）
    SYS_CONSOLE_WRITE   = 12,  // console_write(buf, len)
    SYS_CONSOLE_READ    = 13,  // console_read(buf, len)：非阻塞，返回读到的字节数

    SYS_MAX
};

// 初始化 syscall 表
void syscall_init(void);

/**
 * 系统调用处理函数（汇编调用）
 */
extern "C" void syscall_handler(void);

/**
 * 系统调用分发器
 * @param syscall_num 系统调用号
 * @param p1-p5 系统调用参数
 * @param frame 栈帧指针
 * @return 系统调用返回值
 */
extern "C" syscall_arg_t syscall_dispatcher(syscall_arg_t syscall_num, syscall_arg_t p1, syscall_arg_t p2,
                                 syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5,
                                 syscall_arg_t *frame);

#endif // _KERNEL_SYSCALL_H_
