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

#include <kernel/smp.h>

#include "task_private.h"

/* ============================================================================
 * 调度器的状态
 * ========================================================================== */

/**
 * 每个 CPU 自己的那一份：它正在运行哪个任务、它的 idle 任务、它的时间片计数。
 * 下面的代码里 current_task、idle_task、need_resched、tick_count 指的都是"这个 CPU 的"。
 * 其余的状态所有 CPU 共用，由调度锁（task_lock）保护：就绪队列、待清理链表，以及每个
 * 任务的 state、on_cpu 和它在等什么（wait_object、sleep_until_ms、IPC 的那几个字段）。
 * 调度器不靠内核锁：不拿内核锁的系统调用（IPC）也会阻塞、唤醒别的任务、切换任务。
 *
 * 两条规矩让几个 CPU 能同时调度：
 *   1. 改任务的状态、动就绪队列，都在调度锁里。"看一眼条件，不满足就睡"和"让条件成立，
 *      叫醒等它的任务"因此不会交错：睡的一方在锁里把自己标成 BLOCKED，放了锁才去切换。
 *   2. 内核栈上还有 CPU 在执行的任务（on_cpu）不进就绪队列。任务被换下去是分两步的：
 *      先在锁里决定换谁，再放了锁去切换寄存器和栈；这中间它可能已经被叫醒了。叫醒它的
 *      一方只把状态改成 READY；等切换真的完成了（finish_switch），换它下去的那个 CPU
 *      再把它放进队列。所以从队列里取出来的任务，它的栈一定没人在用。
 */
static struct {
    task_t *current;                // 正在运行的任务
    task_t *idle;                   // 没有任务可运行时运行它
    volatile bool need_resched;     // 当前任务的时间片用完了，等在返回用户态的抢占点切换
    bool idle_waiting;              // 它的 idle 任务正停着等中断（或者马上就要停）
    task_t *leaving;                // 正在被这个 CPU 换下去的任务：换完之后由 finish_switch 收尾
    uint32_t tick_count;            // 当前任务已经用掉的时钟滴答数
} per_cpu[MAX_CPUS];

#define current_task    (per_cpu[hal::Cpu::id()].current)
#define idle_task       (per_cpu[hal::Cpu::id()].idle)
#define need_resched    (per_cpu[hal::Cpu::id()].need_resched)
#define tick_count      (per_cpu[hal::Cpu::id()].tick_count)

static void finish_switch(void);

/** @brief 就绪队列头指针 */
static task_t *ready_queue_head = NULL;

/** @brief 就绪队列尾指针 */
static task_t *ready_queue_tail = NULL;

/** @brief 调度器是否已初始化 */
static bool scheduler_initialized = false;


/* ============================================================================
 * 就绪队列（调用者拿着调度锁）
 * ========================================================================== */

/** 放到队尾，并叫醒闲着的 CPU */
static void enqueue_locked(task_t *task) {
    task->next = NULL;
    task->prev = ready_queue_tail;
    if (ready_queue_tail) {
        ready_queue_tail->next = task;
    } else {
        ready_queue_head = task;
    }
    ready_queue_tail = task;

    // 有 CPU 正闲着等中断的话叫醒它们：不叫的话，这个任务要等到某个闲着的 CPU 自己的
    // 下一次时钟中断才会被发现。idle 是在调度锁里看队列、标记"我要停了"，放了锁再停的，
    // 所以不会漏：它要么还没标记（那它拿到锁时会看到这个任务），要么已经标记了（那我们的
    // 中断会把它从等待里叫出来，哪怕它还没来得及停下）
    uint32_t self = hal::Cpu::id();
    for (uint32_t cpu = 0; cpu < MAX_CPUS; cpu++) {
        if (cpu != self && per_cpu[cpu].idle_waiting) {
            hal::Cpu::kick_others();
            break;
        }
    }
}

/** 取队首；队列空返回 NULL */
static task_t *dequeue_locked(void) {
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

void sched_make_ready_locked(task_t *task) {
    task->wait_object = NULL;
    task->sleep_until_ms = 0;
    task->state = TASK_READY;
    // 它的栈上还有 CPU 在执行（刚把自己标成 BLOCKED，还没切换走）：先不进队列，
    // 那个 CPU 切换完会看到它已经 READY，由它来放（finish_switch）
    if (!task->on_cpu) {
        enqueue_locked(task);
    }
}

/**
 * @brief 把一个新建的、状态是 READY 的任务放进就绪队列
 */
void kernel::Scheduler::ready_queue_add(task_t *task) {
    if (!task) {
        return;
    }
    sync::SpinlockIrqGuard guard(task_lock);
    if (task->state == TASK_READY && !task->on_cpu) {
        enqueue_locked(task);
    }
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
    finish_switch();        // 第一次被换上来时走到这里：给换下去的那个任务收尾
    LOG_DEBUG_MSG("Idle task started\n");

    while (1) {
        // 在调度锁里看有没有就绪任务，没有就标记"我要停了"，放了锁再停。别的 CPU 把任务
        // 放进队列时会看这个标记来叫我们（enqueue_locked）；停下和开中断是一个原子动作，
        // 所以标记之后来的中断一定叫得醒
        bool irq_state;
        task_lock.lock_irqsave(irq_state);
        bool nothing_to_do = ready_queue_head == NULL;
        per_cpu[hal::Cpu::id()].idle_waiting = nothing_to_do;
        task_lock.unlock();                     // 中断还关着
        if (nothing_to_do) {
            hal::Cpu::idle();                   // 原子地开中断并等待；返回时中断已打开
            per_cpu[hal::Cpu::id()].idle_waiting = false;
        } else {
            kernel::Interrupts::enable();
        }

        // 有任务就绪（或者刚处理完一个中断）：让出 CPU
        kernel::Scheduler::yield();
    }
}

/**
 * @brief 创建 cpu 号 CPU 的 idle 任务
 *
 * 任务表最前面的 MAX_CPUS 个 PCB 留给各个 CPU 的 idle 任务（PID 都是 0，它们不是进程）。
 * 这个 CPU 真的启动时才分配内核栈。
 */
static bool task_create_idle(uint32_t cpu) {
    task_t *idle = &task_pool[cpu];
    idle->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!idle->kernel_stack_base) {
        LOG_ERROR_MSG("task_create_idle: Failed to allocate kernel stack\n");
        return false;
    }
    idle->kernel_stack = idle->kernel_stack_base + KERNEL_STACK_SIZE;

    // 使用内核页目录
    idle->page_dir_phys = mm::Vmm::kernel_page_directory();

    // idle 必须开着中断执行停机指令：否则所有任务都阻塞时定时器中断得不到处理，
    // 睡眠的任务永远不会被唤醒
    hal::UserContext::init_kernel(&idle->context, idle_task_loop,
                                  idle->kernel_stack, idle->page_dir_phys);

    per_cpu[cpu].idle = idle;
    LOG_DEBUG_MSG("Idle task created for CPU %u\n", cpu);
    return true;
}

uintptr_t kernel::Scheduler::prepare_idle(uint32_t cpu) {
    if (cpu >= MAX_CPUS || (!per_cpu[cpu].idle && !task_create_idle(cpu))) {
        return 0;
    }
    return per_cpu[cpu].idle->kernel_stack;
}

void kernel::Scheduler::run_idle() {
    // 这个 CPU 正在自己的 idle 任务的栈上（启动代码用的就是 prepare_idle 给的那个栈）：
    // 从现在起它就是那个 idle 任务
    idle_task->state = TASK_RUNNING;
    idle_task->on_cpu = true;
    current_task = idle_task;
    // idle 不在内核锁里运行（它只碰调度器，调度器有自己的锁）：把启动时拿的那一层放掉
    kernel::KernelLock::release();
    idle_task_loop();
    while (1) {
        hal::Cpu::halt();
    }
}

/* ============================================================================
 * 切换完成之后的收尾
 * ========================================================================== */

/**
 * 待清理的任务（已终止、没有父进程来回收的）。任务不能在自己的内核栈上释放自己，所以
 * 退出时先留着；等它被换下 CPU（finish_switch 把它挂到这里），再由之后进 schedule()
 * 的 CPU 释放。通过 task->next 串联，由调度锁保护。
 */
static task_t *pending_cleanup_head = NULL;

/**
 * 一次任务切换的后半段：在换上来的任务的栈上执行。
 *
 * 换上来的任务可能是从 schedule() 里的 task_switch_context 返回的（它以前被换下去过），
 * 也可能是第一次运行（user_task_start、idle_task_loop 的开头）；三处都要调用它。
 */
static void finish_switch(void) {
    uint32_t cpu = hal::Cpu::id();
    task_t *prev = per_cpu[cpu].leaving;
    per_cpu[cpu].leaving = NULL;
    if (prev) {
        sync::SpinlockIrqGuard guard(task_lock);
        // 从这一刻起 prev 的栈没人在用了，可以让别的 CPU 运行它或者释放它
        prev->on_cpu = false;
        if (prev->state == TASK_READY && prev != per_cpu[cpu].idle) {
            // 时间片用完被换下的，或者换下去的途中被叫醒了的
            enqueue_locked(prev);
        } else if (prev->state == TASK_TERMINATED) {
            prev->next = pending_cleanup_head;
            prev->prev = NULL;
            pending_cleanup_head = prev;
        }
    }
    // 内核锁跟着任务走：换上来的任务被换下去时拿着几层，现在恢复成几层
    kernel::KernelLock::adopt(per_cpu[cpu].current->lock_depth);
}

/* ============================================================================
 * 新进程的第一步
 * ========================================================================== */

/**
 * 一个用户进程第一次被换上 CPU 时从这里开始：在内核态，在它自己的内核栈上。
 * 它给上一个任务收尾（finish_switch，新任务不拿内核锁），然后装上用户态的现场。
 */
static void user_task_start(void) {
    kernel::Interrupts::disable();
    finish_switch();
    task_switch_context(NULL, &current_task->user_context);     // 不返回
    while (1) {
        hal::Cpu::halt();
    }
}

void kernel::Scheduler::start_in_user_mode(task_t *task) {
    hal::UserContext::init_kernel(&task->context, user_task_start, task->kernel_stack, task->page_dir_phys);
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
    uint32_t cpu = hal::Cpu::id();

    task_t *prev_task = current_task;
    task_t *next_task;
    task_t *to_free;
    {
        task_lock.lock();

        // 已终止、已经换下 CPU 的任务：摘下来，放了锁再释放（释放要拿别的锁）
        to_free = pending_cleanup_head;
        pending_cleanup_head = NULL;

        // 还能运行的当前任务变回就绪。这时不放进队列：它的栈我们还在用，
        // 切换完了由 finish_switch 放
        if (prev_task && prev_task != idle_task && prev_task->state == TASK_RUNNING) {
            prev_task->state = TASK_READY;
        }

        // 取下一个。队列空了：当前任务还能运行就接着运行它，否则运行 idle
        next_task = dequeue_locked();
        if (!next_task) {
            bool prev_runnable = prev_task && prev_task != idle_task && prev_task->state == TASK_READY;
            next_task = prev_runnable ? prev_task : idle_task;
        }
        next_task->state = TASK_RUNNING;
        next_task->on_cpu = true;
        current_task = next_task;
        if (prev_task != next_task) {
            per_cpu[cpu].leaving = prev_task;
        }

        task_lock.unlock();
    }

    while (to_free) {
        task_t *task = to_free;
        to_free = task->next;
        task->next = NULL;
        LOG_DEBUG_MSG("Cleaning up terminated task %u (%s)\n", task->pid, task->name);
        kernel::Scheduler::free(task);
    }

    if (prev_task != next_task) {
        // 内核栈（架构相关）
        if (next_task->is_user_process) {
            hal::UserContext::set_kernel_stack(next_task->kernel_stack);
        }

        // 切换代码会直接装入新任务的页表（CR3 / TTBR0），但不会告诉 VMM：先在这里记下。
        // 内核线程也要记：它的 context 里是内核的页表。不记的话 VMM 仍以为刚退出的进程的
        // 页表是"当前页表"，清理时会拒绝释放它
        mm::Vmm::sync_current_dir(next_task->is_user_process ? next_task->page_dir_phys
                                                             : (uintptr_t)next_task->context.cr3);

        // 浮点/SIMD 寄存器不在 task_switch_context 换的那一组里。内核自己不用它们，
        // 所以只在用户任务之间换：换下去的存起来，换上来的装回去（中间隔着 idle 也一样）
        if (prev_task && prev_task->is_user_process) {
            hal::UserContext::fp_save(&prev_task->fp_state);
        }
        if (next_task->is_user_process) {
            hal::UserContext::fp_restore(&next_task->fp_state);
        }

        // 许可给用户任务的 I/O 端口它可以直接访问（x86 的 I/O 许可位图）：每个 CPU 一份，
        // 换下去的任务的端口关上，换上来的打开
        if (prev_task && prev_task->is_user_process) {
            hal::Platform::set_user_ports(prev_task->hw_allowed, prev_task->hw_allowed_count, false);
        }
        if (next_task->is_user_process) {
            hal::Platform::set_user_ports(next_task->hw_allowed, next_task->hw_allowed_count, true);
        }

        // 内核锁跟着任务走：记下换下去的任务拿着几层，它被换回来时恢复（finish_switch）
        if (prev_task) {
            prev_task->lock_depth = kernel::KernelLock::depth();
        }

        cpu_context_t *old_ctx_ptr = prev_task ? &prev_task->context : NULL;
        task_switch_context(&old_ctx_ptr, &next_task->context);

        // 回到这里时，我们是一个以前被换下去的任务，刚被某个 CPU 换上来（不一定是原来
        // 那个）：给那个 CPU 刚换下去的任务收尾
        finish_switch();
    }

    kernel::Interrupts::restore(prev_state);
}

/**
 * @brief 定时器中断处理
 */
void kernel::Scheduler::set_timer(uint64_t ms) {
    sync::SpinlockIrqGuard guard(task_lock);
    current_task->timer_pending = false;
    current_task->timer_deadline_ms = ms ? drivers::Timer::get_uptime_ms() + ms : 0;
}

void kernel::Scheduler::timer_tick() {
    if (!scheduler_initialized || !current_task) {
        return;
    }
    
    // 更新当前任务的运行时间
    uint32_t tick_ms = 1000 / drivers::Timer::get_frequency();
    current_task->runtime_ms += tick_ms;
    
    // 检查睡眠任务是否应该唤醒
    uint64_t current_time_ms = drivers::Timer::get_uptime_ms();
    
    {
        sync::SpinlockIrqGuard guard(task_lock);
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            task_t *task = &task_pool[i];

            // 睡够了的任务
            if (task->state == TASK_BLOCKED && task->sleep_until_ms > 0 &&
                current_time_ms >= task->sleep_until_ms) {
                sched_make_ready_locked(task);
            }

            // 用户态定时器（timer_set）到期：记成待处理的内核消息；它正在 recv 上等
            // 内核消息的话直接交给它
            if (task->state != TASK_UNUSED && task->timer_deadline_ms != 0 &&
                current_time_ms >= task->timer_deadline_ms) {
                task->timer_deadline_ms = 0;
                task->timer_pending = true;
                kernel::ipc_notify_locked(task);
            }
        }
    }
    
    // 时间片轮转调度
    // 注意：这个函数在 IRQ 中调用，调度将在 IRQ 返回时由 schedule_from_irq 处理
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
    {
        sync::SpinlockIrqGuard guard(task_lock);
        current_task->sleep_until_ms = drivers::Timer::get_uptime_ms() + ms;
        current_task->wait_object = NULL;
        current_task->state = TASK_BLOCKED;
    }

    // 切换到其他任务；时钟中断发现睡够了会把我们放回就绪队列
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
    {
        sync::SpinlockIrqGuard guard(task_lock);
        current_task->wait_object = wait_object;
        current_task->state = TASK_BLOCKED;
    }

    // 切换到其他任务。如果放了锁之后、切换之前就有人 wakeup 了我们，状态已经是 READY，
    // schedule() 会照"让出"来处理，不会睡过头
    kernel::Scheduler::schedule();

    kernel::Interrupts::restore(prev_state);
}

/**
 * @brief 唤醒等待在指定对象上的一个任务
 * 
 * @param wait_object 等待对象指针，只唤醒阻塞在同一对象上的任务
 */
void kernel::Scheduler::wakeup(void *wait_object) {
    sync::SpinlockIrqGuard guard(task_lock);

    // 唤醒一个阻塞在该对象上的任务（睡眠中的任务由定时器唤醒，不在此列）
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &task_pool[i];
        if (task->state == TASK_BLOCKED && task->sleep_until_ms == 0 &&
            task->wait_object == wait_object) {
            sched_make_ready_locked(task);
            break;  // 只唤醒一个任务
        }
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

    memset(per_cpu, 0, sizeof(per_cpu));
    ready_queue_head = NULL;
    ready_queue_tail = NULL;

    // 任务表最前面的几个 PCB 留给各个 CPU 的 idle 任务，免得被分给进程
    for (uint32_t cpu = 0; cpu < MAX_CPUS; cpu++) {
        task_t *idle = &task_pool[cpu];
        memset(idle, 0, sizeof(task_t));
        idle->pid = 0;
        strcpy(idle->name, "idle");
        idle->state = TASK_READY;
        idle->priority = UINT32_MAX;  // 最低优先级
        idle->time_slice = DEFAULT_TIME_SLICE;
        idle->is_user_process = false;
    }

    // 启动 CPU 的 idle 任务现在就建好；其余的等那个 CPU 启动时再建（prepare_idle）
    if (!task_create_idle(0)) {
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
