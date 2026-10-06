// ============================================================================
// sched.cpp - 调度器：谁在 CPU 上、下一个是谁、怎么换
// ============================================================================
//
// 就绪队列、idle 任务、schedule()（包括换浮点寄存器和延迟回收退出的任务）、时钟滴答和
// 抢占点、让出 / 睡眠 / 阻塞 / 唤醒。任务表本身（PCB 的分配和释放、进程的创建和退出）
// 在 task.cpp 里；两个文件一起实现 kernel::Scheduler。

#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <kernel/sync/spinlock.h>
#include <hal/hal.h>
#include <hal/user_context.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/timer.h>

#include "task_private.h"

/* ============================================================================
 * 调度器的状态
 * ========================================================================== */

/** @brief 当前正在运行的任务 */
static task_t *current_task = NULL;

/** @brief 就绪队列头指针 */
static task_t *ready_queue_head = NULL;

/** @brief 就绪队列尾指针 */
static task_t *ready_queue_tail = NULL;

/** @brief idle 任务指针 */
static task_t *idle_task = NULL;

/** @brief 调度器是否已初始化 */
static bool scheduler_initialized = false;

/**
 * @brief 待清理的 terminated 任务链表（延迟清理，通过 task->next 串联）
 *
 * 任务不能在自己的内核栈上释放自己，所以退出时只挂到这里，
 * 由之后调用 schedule() 的任务在它自己的栈上回收。
 * 只在关中断的 schedule() 中访问。
 */
static task_t *pending_cleanup_head = NULL;

/* ============================================================================
 * 辅助函数：就绪队列操作
 * ========================================================================== */

/**
 * @brief 将任务添加到就绪队列尾部
 */
void kernel::Scheduler::ready_queue_add(task_t *task) {
    if (!task) {
        return;
    }
    
    // 不添加 UNUSED、ZOMBIE 或 TERMINATED 状态的任务
    if (task->state == TASK_UNUSED || task->state == TASK_ZOMBIE || task->state == TASK_TERMINATED) {
        return;
    }
    
    // 确保任务处于 READY 状态
    if (task->state != TASK_READY) {
        return;
    }
    
    sync::SpinlockIrqGuard guard(task_lock);
    
    task->next = NULL;
    task->prev = ready_queue_tail;
    
    if (ready_queue_tail) {
        ready_queue_tail->next = task;
    } else {
        ready_queue_head = task;
    }
    
    ready_queue_tail = task;
}

/**
 * @brief 从就绪队列获取下一个任务
 * 
 * @return 下一个就绪任务，如果队列为空返回 NULL
 */
static task_t* ready_queue_pop(void) {
    sync::SpinlockIrqGuard guard(task_lock);
    
    task_t *task = ready_queue_head;
    if (task) {
        ready_queue_head = task->next;
        if (ready_queue_head) {
            ready_queue_head->prev = NULL;
        } else {
            ready_queue_tail = NULL;
        }
        
        task->next = NULL;
        task->prev = NULL;
    }
    
    return task;
}

/**
 * @brief 获取当前任务
 */
task_t* kernel::Scheduler::get_current() {
    return current_task;
}

/* ============================================================================
 * idle 任务
 * ========================================================================== */

/**
 * @brief idle 任务循环
 * 
 * 当没有其他任务可运行时，运行此任务
 */
static void idle_task_loop(void) {
    LOG_DEBUG_MSG("Idle task started\n");
    
    while (1) {
        // 先关中断再看有没有就绪任务：中断处理函数（设备中断、时钟）随时可能唤醒任务，
        // 如果“检查”和“停机”之间有空档，刚被唤醒的任务就要白等到下一次时钟中断
        kernel::Interrupts::disable();
        if (ready_queue_head == NULL) {
            hal::Cpu::idle();       // 原子地开中断并等待；返回时中断已打开
        } else {
            kernel::Interrupts::enable();
        }

        // 有任务就绪（或者刚处理完一个中断）：让出 CPU
        kernel::Scheduler::yield();
    }
}

/**
 * @brief 创建 idle 任务
 */
static bool task_create_idle(void) {
    // 分配 idle PCB（使用 PID 0）
    idle_task = &task_pool[0];
    memset(idle_task, 0, sizeof(task_t));
    
    idle_task->pid = 0;
    strcpy(idle_task->name, "idle");
    idle_task->state = TASK_READY;
    idle_task->priority = UINT32_MAX;  // 最低优先级
    idle_task->time_slice = DEFAULT_TIME_SLICE;
    idle_task->is_user_process = false;
    
    // 分配内核栈
    idle_task->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!idle_task->kernel_stack_base) {
        LOG_ERROR_MSG("task_create_idle: Failed to allocate kernel stack\n");
        return false;
    }
    
    idle_task->kernel_stack = idle_task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    // 使用内核页目录
    idle_task->page_dir_phys = mm::Vmm::get_page_directory();

    // idle 必须开着中断执行停机指令：否则所有任务都阻塞时定时器中断得不到处理，
    // 睡眠的任务永远不会被唤醒
    hal::UserContext::init_kernel(&idle_task->context, idle_task_loop,
                                  idle_task->kernel_stack, idle_task->page_dir_phys);

    LOG_DEBUG_MSG("Idle task created (PID 0)\n");
    return true;
}

/* ============================================================================
 * 任务调度
 * ========================================================================== */

/**
 * @brief 任务调度器
 */
void kernel::Scheduler::schedule() {
    if (!scheduler_initialized) {
        return;
    }
    
    bool prev_state = kernel::Interrupts::disable();
    
    // 【关键修复】先清理上一次延迟的 terminated 任务
    // 现在我们已经在新任务的栈上了，可以安全地释放旧任务的资源
    // 调用者不是这些任务中的任何一个（它们退出后不会再运行），
    // 这里只剩内核栈、地址空间和 PCB，释放过程不会睡眠
    while (pending_cleanup_head) {
        task_t *task_to_cleanup = pending_cleanup_head;
        pending_cleanup_head = task_to_cleanup->next;
        task_to_cleanup->next = NULL;

        LOG_DEBUG_MSG("Cleaning up terminated task %u (%s)\n",
                     task_to_cleanup->pid, task_to_cleanup->name);

        kernel::Scheduler::free(task_to_cleanup);
    }

    // 保存当前任务
    task_t *prev_task = current_task;
    
    // 检查是否需要清理 prev_task（但不要立即清理，避免时序问题）
    bool should_free_prev_task = false;
    if (prev_task && prev_task != idle_task && prev_task->state == TASK_TERMINATED) {
        should_free_prev_task = true;
        LOG_DEBUG_MSG("Marking terminated task %u (%s) for cleanup\n", 
                     prev_task->pid, prev_task->name);
    }
    
    // 处理当前任务（除了 TERMINATED，已经标记延迟清理）
    if (prev_task && prev_task != idle_task) {
        if (prev_task->state == TASK_ZOMBIE) {
            // 僵尸进程：不调度，也不清理，等待父进程回收
            LOG_DEBUG_MSG("Task %u (%s) is zombie, waiting for parent\n", 
                         prev_task->pid, prev_task->name);
        } else if (prev_task->state == TASK_RUNNING) {
            // 将还在运行的任务加回就绪队列
            prev_task->state = TASK_READY;
            kernel::Scheduler::ready_queue_add(prev_task);
        }
    }
    
    // 从就绪队列选择下一个任务
    task_t *next_task = ready_queue_pop();
    
    // 如果没有就绪任务，运行 idle
    if (!next_task) {
        next_task = idle_task;
    }
    
    
    // 更新任务状态
    next_task->state = TASK_RUNNING;
    current_task = next_task;
    
    // 更新内核栈（架构相关）
    if (next_task->is_user_process) {
        hal::UserContext::set_kernel_stack(next_task->kernel_stack);
    }
    
    // 关键修复：在上下文切换前，先同步 VMM 的 current_dir_phys
    // task_switch_context 会直接修改 CR3，但不会更新 current_dir_phys
    // 我们必须在切换前就更新，因为切换后不能再调用任何函数
    // 内核线程也要同步：切换代码会装入它 context 里保存的 CR3/TTBR0（内核
    // 页目录）。不同步的话 VMM 仍以为刚退出的用户进程的页目录是“当前页目录”，
    // 延迟清理时 free_page_directory 会拒绝释放它。
    if (prev_task != next_task) {
        mm::Vmm::sync_current_dir(next_task->is_user_process ? next_task->page_dir_phys
                                                             : (uintptr_t)next_task->context.cr3);
    }
    
    // 执行上下文切换
    // 注意：切换后不能调用任何函数，因为栈已经切换了
    if (prev_task != next_task) {
        cpu_context_t *old_ctx_ptr = prev_task ? &prev_task->context : NULL;
        
        // 如果需要释放 prev_task，必须在切换前处理
        // 但我们不能在切换前释放，因为还在使用 prev_task 的栈和上下文
        // 解决方案：标记为待清理，下次调度时清理（那时已在新栈上）
        if (should_free_prev_task) {
            // ✅ 将任务挂到待清理链表，下次调度时会在新栈上安全清理。
            // 用链表而不是单个指针：连续退出的任务不能互相覆盖。
            prev_task->next = pending_cleanup_head;
            prev_task->prev = NULL;
            pending_cleanup_head = prev_task;
            LOG_DEBUG_MSG("Task %u (%s) marked for deferred cleanup\n",
                        prev_task->pid, prev_task->name);
        }
        
        // 浮点/SIMD 寄存器不在 task_switch_context 换的那一组里。内核自己不用它们，
        // 所以只在用户任务之间换：换下去的存起来，换上来的装回去（中间隔着 idle 也一样）
        if (prev_task && prev_task->is_user_process) {
            hal::UserContext::fp_save(&prev_task->fp_state);
        }
        if (next_task->is_user_process) {
            hal::UserContext::fp_restore(&next_task->fp_state);
        }

        task_switch_context(&old_ctx_ptr, &next_task->context);
        
        // 注意：永远不会执行到这里（task_switch_context 不会返回到这里）
        // 下一次进入这个函数时，已经是在新任务的上下文中了
    }
    
    kernel::Interrupts::restore(prev_state);
}

/* 当前任务的时间片已用完，等待在返回用户态的抢占点切换 */
static volatile bool need_resched = false;

/**
 * @brief 定时器中断处理
 */
void kernel::Scheduler::timer_tick() {
    if (!scheduler_initialized || !current_task) {
        return;
    }
    
    // 更新当前任务的运行时间
    uint32_t tick_ms = 1000 / drivers::Timer::get_frequency();
    current_task->runtime_ms += tick_ms;
    
    // 检查睡眠任务是否应该唤醒
    uint64_t current_time_ms = drivers::Timer::get_uptime_ms();
    
    // 收集需要唤醒的任务（避免在持有锁时调用 kernel::Scheduler::ready_queue_add）
    task_t *tasks_to_wake[MAX_TASKS];
    uint32_t wake_count = 0;
    task_t *tasks_to_notify[MAX_TASKS];
    uint32_t notify_count = 0;
    
    {
        sync::SpinlockIrqGuard guard(task_lock);
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            task_t *task = &task_pool[i];
        
            if (task->state == TASK_BLOCKED && task->sleep_until_ms > 0) {
                if (current_time_ms >= task->sleep_until_ms) {
                    task->sleep_until_ms = 0;
                    task->state = TASK_READY;
                    tasks_to_wake[wake_count++] = task;
                }
            }

            // 用户态定时器（timer_set）到期：记成待处理的内核消息
            if (task->state != TASK_UNUSED && task->timer_deadline_ms != 0 &&
                current_time_ms >= task->timer_deadline_ms) {
                task->timer_deadline_ms = 0;
                task->timer_pending = true;
                tasks_to_notify[notify_count++] = task;
            }
        }
    }
    
    // 在锁外将任务添加到就绪队列
    for (uint32_t i = 0; i < wake_count; i++) {
        kernel::Scheduler::ready_queue_add(tasks_to_wake[i]);
    }

    // 正在 recv 上等内核消息的任务：把定时器消息交给它
    for (uint32_t i = 0; i < notify_count; i++) {
        kernel::Ipc::notify(tasks_to_notify[i]);
    }
    
    // 时间片轮转调度
    // 注意：这个函数在 IRQ 中调用，调度将在 IRQ 返回时由 schedule_from_irq 处理
    static uint32_t tick_count = 0;
    tick_count++;
    
    if (tick_count >= current_task->time_slice) {
        tick_count = 0;
        // 这里还在中断处理函数里，不能切换任务：只记下“时间片用完”，
        // 由 schedule_from_irq 在中断即将返回用户态时处理
        need_resched = true;
    }
}

/**
 * @brief 主动让出 CPU
 */
void kernel::Scheduler::yield() {
    kernel::Scheduler::schedule();
}

/**
 * @brief 任务睡眠
 */
void kernel::Scheduler::sleep(uint32_t ms) {
    if (!current_task || ms == 0) {
        return;
    }
    assert_may_sleep("Scheduler::sleep");
    
    bool prev_state = kernel::Interrupts::disable();
    
    // 计算唤醒时间
    uint64_t wake_time = drivers::Timer::get_uptime_ms() + ms;
    current_task->sleep_until_ms = wake_time;
    current_task->wait_object = NULL;
    current_task->state = TASK_BLOCKED;
    
    // 切换到其他任务
    kernel::Scheduler::schedule();
    
    kernel::Interrupts::restore(prev_state);
}

/**
 * @brief 阻塞当前任务（用于同步原语）
 * 
 * @param wait_object 等待对象指针（用于调试）
 */
void kernel::Scheduler::block(void *wait_object) {
    assert_may_sleep("Scheduler::block");
    
    if (!current_task) {
        return;
    }
    
    bool prev_state = kernel::Interrupts::disable();
    
    current_task->wait_object = wait_object;
    current_task->state = TASK_BLOCKED;
    
    LOG_DEBUG_MSG("Task %u (%s) blocked on %p\n", 
                 current_task->pid, current_task->name, wait_object);
    
    // 触发调度，切换到其他任务
    kernel::Scheduler::schedule();
    
    kernel::Interrupts::restore(prev_state);
}

/**
 * @brief 唤醒等待在指定对象上的一个任务
 * 
 * @param wait_object 等待对象指针，只唤醒阻塞在同一对象上的任务
 */
void kernel::Scheduler::wakeup(void *wait_object) {    
    task_t *task_to_wake = NULL;
    
    bool irq_state;
    task_lock.lock_irqsave(irq_state);
    
    // 唤醒一个阻塞在该对象上的任务（睡眠中的任务由定时器唤醒，不在此列）
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &task_pool[i];
        
        if (task->state == TASK_BLOCKED && task->sleep_until_ms == 0 &&
            task->wait_object == wait_object) {
            task->wait_object = NULL;
            task->state = TASK_READY;
            task_to_wake = task;
            LOG_DEBUG_MSG("Task %u (%s) woken up\n", task->pid, task->name);
            break;  // 只唤醒一个任务
        }
    }
    
    task_lock.unlock_irqrestore(irq_state);
    
    // 在锁外添加到就绪队列
    if (task_to_wake) {
        kernel::Scheduler::ready_queue_add(task_to_wake);
    }
}

/**
 * @brief 中断返回前的抢占点
 *
 * 由各架构的 IRQ 分发函数在应答中断（EOI）并离开中断上下文之后调用。
 *
 * 只抢占用户态：被打断的是用户态代码时，当前任务的内核栈上只有这一个中断帧，
 * 在这里切走、以后再切回来继续返回用户态是安全的。被打断的是内核态代码时
 * 不切换（内核不可抢占，见并发规则 R1），等它自己让出或返回用户态。
 *
 * @param from_user 被打断的上下文是否为用户态
 */
void schedule_from_irq(bool from_user) {
    if (!from_user) {
        return;
    }

    // 即将返回用户态：有待处理的 kill 就在这里退出。只在系统调用返回时投递的话，
    // 从不进内核的进程（用户态死循环）永远杀不掉。exit 路径会清掉中断计数。
    kernel::Scheduler::deliver_pending_kill();

    if (!need_resched) {
        return;
    }
    need_resched = false;

    // 打断的是用户态，所以这是最外层的中断帧：此刻的嵌套计数全部属于它。
    // 切换期间让出计数，换回来之后恢复，再由中断存根配对地减掉。
    uint32_t depth = interrupt_depth_suspend();
    kernel::Scheduler::schedule();
    interrupt_depth_resume(depth);
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

/**
 * @brief 初始化任务管理系统
 */
void kernel::Scheduler::init() {
    LOG_INFO_MSG("Initializing task management...\n");
    
    // 任务表（锁、PCB 池、PID 分配）在 task.cpp 里
    task_table_init();

    current_task = NULL;
    ready_queue_head = NULL;
    ready_queue_tail = NULL;

    // 创建 idle 任务
    if (!task_create_idle()) {
        LOG_ERROR_MSG("Failed to create idle task\n");
        return;
    }
    
    // 标记调度器为已初始化
    scheduler_initialized = true;
    
    LOG_INFO_MSG("Task management initialized\n");
    LOG_DEBUG_MSG("  Max tasks: %d\n", MAX_TASKS);
    LOG_DEBUG_MSG("  Kernel stack size: %d KB\n", KERNEL_STACK_SIZE / 1024);
    LOG_DEBUG_MSG("  User stack size: %d MB\n", USER_STACK_SIZE / (1024 * 1024));
}
