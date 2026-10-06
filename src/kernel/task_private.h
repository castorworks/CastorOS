#ifndef _KERNEL_TASK_PRIVATE_H_
#define _KERNEL_TASK_PRIVATE_H_

/**
 * @file task_private.h
 * @brief task.cpp 和 sched.cpp 之间共用、不对外的东西
 *
 * kernel::Scheduler 分在两个文件里实现：task.cpp 管任务表和进程的一生，sched.cpp 管
 * 调度。它们共用一把锁；别的代码不应该包含这个文件。
 */

#include <kernel/task.h>
#include <kernel/sync/spinlock.h>

/** 保护任务池、就绪队列和 PID 分配（定义在 task.cpp） */
extern sync::Spinlock task_lock;

/** 清空任务表、初始化锁、重置 PID 分配。由 kernel::Scheduler::init 调用一次 */
void task_table_init(void);

#endif // _KERNEL_TASK_PRIVATE_H_
