#ifndef _KERNEL_TASK_PRIVATE_H_
#define _KERNEL_TASK_PRIVATE_H_

/**
 * @file task_private.h
 * @brief task.cpp 和 sched.cpp 之间共用、不对外的东西
 *
 * kernel::Scheduler 分在两个文件里实现：task.cpp 管任务表和进程的一生，sched.cpp 管
 * 调度。它们共用一把锁。要在这把锁里改任务状态的还有 IPC（ipc.cpp）和设备中断的转发
 * （user_irq.cpp）；别的代码不应该包含这个文件。
 */

#include <kernel/task.h>
#include <kernel/sync/spinlock.h>

/**
 * 调度锁（定义在 task.cpp）。保护任务池、PID 分配、就绪队列，以及每个任务的 state、
 * on_cpu 和它在等什么（wait_object、sleep_until_ms、IPC 和设备中断的那几个字段）。
 * 拿它的时候要关中断（用 SpinlockIrqGuard / lock_irqsave）：中断处理函数也会拿。
 */
extern sync::Spinlock task_lock;

/**
 * 让 task 变成就绪（调用者拿着调度锁）。它的栈上还有 CPU 在执行的话只改状态，
 * 由那个 CPU 换完之后放进就绪队列（见 sched.cpp）。
 */
void sched_make_ready_locked(task_t *task);

/** 按 PID 找任务（调用者拿着调度锁）；Scheduler::get_by_pid 是它加上拿锁 */
task_t *sched_find_locked(uint32_t pid);

namespace kernel {
/** Ipc::notify 的本体（调用者拿着调度锁）：task 正等着内核消息的话交给它一条 */
void ipc_notify_locked(task_t *task);
}

/** 清空任务表、初始化锁、重置 PID 分配。由 kernel::Scheduler::init 调用一次 */
void task_table_init(void);

#endif // _KERNEL_TASK_PRIVATE_H_
