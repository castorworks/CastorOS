/**
 * @file user_context.h
 * @brief 任务的寄存器现场：通用的调度和进程代码向各架构要的东西
 *
 * 寄存器叫什么、系统调用入口把它们按什么顺序压在内核栈上、返回用户态靠哪条指令，
 * 每个架构都不一样。创建进程、fork、exec 需要读写这些东西的地方收在这里，
 * 由 src/arch/<arch>/task/user_context.cpp 实现；通用代码里不出现寄存器名。
 */

#ifndef _HAL_USER_CONTEXT_H_
#define _HAL_USER_CONTEXT_H_

#include <types.h>
#include <kernel/task.h>

namespace hal {

class UserContext {
public:
    /**
     * 新程序刚开始运行时的栈指针。stack_top 是栈区的上界（不含）；
     * 入口函数按各自的调用约定对栈的对齐有要求，所以由架构决定留多少。
     */
    static uintptr_t initial_sp(uintptr_t stack_top);

    /** 一个从 entry 开始、在用户态运行的任务的初始现场 */
    static void init(cpu_context_t *ctx, uintptr_t entry, uintptr_t user_sp,
                     uintptr_t space, uintptr_t kernel_sp);

    /** 一个在内核态运行 entry 的任务（idle）的初始现场：开着中断，经 task_enter_kernel_thread 进入 */
    static void init_kernel(cpu_context_t *ctx, void (*entry)(void), uintptr_t kernel_sp, uintptr_t space);

    /**
     * fork 出来的子进程的现场：和父进程进入这次系统调用时一模一样，
     * 只是系统调用的返回值是 0。frame 是系统调用入口保存的寄存器。
     */
    static void fork(cpu_context_t *child, const uintptr_t *frame, uintptr_t space, uintptr_t kernel_sp);

    /** exec：改写系统调用入口保存的寄存器，让这次系统调用"返回"到新程序的入口 */
    static void exec_return(uintptr_t *frame, uintptr_t entry, uintptr_t user_sp);

    /** 切换到一个用户任务之前：告诉 CPU 它从用户态陷入内核时用哪个内核栈 */
    static void set_kernel_stack(uintptr_t kernel_sp);
};

} // namespace hal

#endif // _HAL_USER_CONTEXT_H_
