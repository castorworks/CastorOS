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

    /*
     * 浮点/SIMD 寄存器。内核自己不用它们（编译时就禁掉了），所以进出内核时不用保存；
     * 只有换一个用户任务上 CPU 时才要把它们换掉。
     */

    /** 一个新程序开始运行时的浮点状态：寄存器清空，所有浮点异常屏蔽 */
    static void fp_reset(hal_fp_state_t *state);

    /** 把 CPU 上现在的浮点/SIMD 寄存器存进 *state（不改变寄存器） */
    static void fp_save(hal_fp_state_t *state);

    /** 把 *state 装回 CPU */
    static void fp_restore(const hal_fp_state_t *state);
};

} // namespace hal

#endif // _HAL_USER_CONTEXT_H_
