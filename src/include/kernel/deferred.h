#ifndef _KERNEL_DEFERRED_H_
#define _KERNEL_DEFERRED_H_

#include <types.h>

/**
 * @file deferred.h
 * @brief 延迟工作：把中断里触发的工作移到任务上下文执行
 *
 * 中断处理函数（包括定时器回调）不能睡眠、不能拿 Mutex、不能等待时间流逝。
 * 需要做这些事的工作在初始化时用 add() 登记，中断里只调用 raise() 置位，
 * 由 kworker 内核线程在任务上下文执行。
 */

namespace kernel {

class Deferred {
public:
    /**
     * 登记一项工作
     * @return 工作编号（>= 0），登记表已满返回 -1
     */
    static int add(void (*fn)(void), const char *name);

    /**
     * 请求执行一项工作。可在中断上下文调用。
     * 工作执行前重复 raise 只会执行一次。
     */
    static void raise(int id);

    /** 创建 kworker 线程（调度器初始化之后调用一次） */
    static void start();
};

} // namespace kernel

#endif // _KERNEL_DEFERRED_H_
