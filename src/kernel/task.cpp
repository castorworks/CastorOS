// ============================================================================
// task.cpp - 任务表和进程的一生
// ============================================================================
//
// PCB 的分配和释放、按 PID 查找、第一个用户进程的创建和它的用户栈、退出、kill，
// 以及"当前进程有没有特权"这类查询。谁在 CPU 上、怎么切换在 sched.cpp 里；
// 两个文件一起实现 kernel::Scheduler。

#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <kernel/user_irq.h>
#include <kernel/sync/spinlock.h>
#include <hal/hal.h>
#include <hal/user_context.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

#include "task_private.h"

/* ============================================================================
 * 任务表
 * ========================================================================== */

/** @brief 任务控制块池 */
task_t task_pool[MAX_TASKS];

/** @brief 下一个可用的 PID（INIT_PID 留给 init） */
static uint32_t next_pid = INIT_PID + 1;

/** @brief 任务管理全局锁 - 保护任务池、就绪队列和 PID 分配 */
sync::Spinlock task_lock;

void task_table_init(void) {
    task_lock.init();

    memset(task_pool, 0, sizeof(task_pool));
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_UNUSED;
    }
    next_pid = INIT_PID + 1;
}

/* ============================================================================
 * 辅助函数：任务控制块管理
 * ========================================================================== */

/**
 * @brief 分配一个空闲的任务控制块
 */
task_t* kernel::Scheduler::alloc() {
    {
        sync::SpinlockIrqGuard guard(task_lock);
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (task_pool[i].state == TASK_UNUSED) {
                memset(&task_pool[i], 0, sizeof(task_t));
                task_pool[i].pid = next_pid++;
                task_pool[i].state = TASK_READY;
                task_pool[i].priority = DEFAULT_PRIORITY;
                task_pool[i].time_slice = DEFAULT_TIME_SLICE;

                return &task_pool[i];
            }
        }
    }
    LOG_ERROR_MSG("kernel::Scheduler::alloc: No free PCB available (max: %d)\n", MAX_TASKS);
    return NULL;
}

/**
 * @brief 释放任务控制块
 */
void kernel::Scheduler::free(task_t *task) {
    if (!task) {
        return;
    }
    
    // 注意：不能在持有 spinlock 时调用可能阻塞的函数（kfree、mm::Vmm::free_page_directory）
    // 所以先在锁外释放资源，最后在锁内清理 PCB
    
    uintptr_t kernel_stack_base = task->kernel_stack_base;
    bool is_user = task->is_user_process;
    uintptr_t page_dir_phys = task->page_dir_phys;

    // 被释放的任务不再算作其地址空间的使用者。fork 失败路径上的子进程
    // 还处于 READY 状态，不改的话 i686 的 free_page_directory 会认为页目录
    // “仍被任务使用”而拒绝释放（泄漏整个克隆出来的地址空间）。
    task->state = TASK_TERMINATED;

    // 释放内核栈（在锁外执行）
    if (kernel_stack_base) {
        kfree((void*)kernel_stack_base);
    }
    
    // 释放页目录（在锁外执行，仅用户进程）
    if (is_user && page_dir_phys) {
        mm::Vmm::free_page_directory(page_dir_phys);
    }
    
    // 在锁内清空 PCB
    sync::SpinlockIrqGuard guard(task_lock);
    
    memset(task, 0, sizeof(task_t));
    task->state = TASK_UNUSED;
}

/**
 * @brief 根据 PID 查找任务
 */
task_t* kernel::Scheduler::get_by_pid(uint32_t pid) {
    sync::SpinlockIrqGuard guard(task_lock);
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state != TASK_UNUSED && task_pool[i].pid == pid) {
            return &task_pool[i];
        }
    }
    
    return NULL;
}

bool kernel::Scheduler::make_init(uint32_t pid) {
    sync::SpinlockIrqGuard guard(task_lock);

    task_t *found = NULL;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_UNUSED) {
            continue;
        }
        if (task_pool[i].pid == INIT_PID) {
            return false;
        }
        if (task_pool[i].pid == pid) {
            found = &task_pool[i];
        }
    }
    if (!found) {
        return false;
    }
    found->pid = INIT_PID;
    return true;
}

/* ============================================================================
 * 用户栈设置
 * ========================================================================== */

/**
 * @brief 为用户进程设置用户栈
 * 
 * ARM64: Uses HAL MMU interface for page mapping
 * i686/x86_64: Uses VMM page directory interface
 */
bool kernel::Scheduler::setup_user_stack(task_t *task) {
    if (!task || !task->is_user_process || task->page_dir_phys == 0) {
        LOG_ERROR_MSG("kernel::Scheduler::setup_user_stack: Invalid task\n");
        return false;
    }

    // 用户栈在用户空间顶部。最顶上一页是参数页（USER_ARGS_ADDR），栈从它下面开始
    hal_addr_space_t space = (hal_addr_space_t)task->page_dir_phys;
    uintptr_t stack_top = USER_STACK_TOP;
    uintptr_t stack_bottom = stack_top - USER_STACK_SIZE;
    uint32_t num_pages = USER_STACK_SIZE / PAGE_SIZE;

    for (uint32_t i = 0; i < num_pages; i++) {
        uintptr_t virt = stack_bottom + (uintptr_t)i * PAGE_SIZE;

        // should_fail_stack_page：测试用它模拟内存不够
        paddr_t phys = kernel::Scheduler::should_fail_stack_page(i) ? PADDR_INVALID : mm::Pmm::alloc_frame();
        if (phys != PADDR_INVALID &&
            !hal::Mmu::map(space, virt, phys, HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_USER)) {
            mm::Pmm::free_frame(phys);
            phys = PADDR_INVALID;
        }
        if (phys == PADDR_INVALID) {
            // 失败：把已经映射的栈页撤掉。空出来的页表留给地址空间销毁时回收
            for (uint32_t j = 0; j < i; j++) {
                paddr_t mapped = hal::Mmu::unmap(space, stack_bottom + (uintptr_t)j * PAGE_SIZE);
                if (mapped != PADDR_INVALID) {
                    mm::Pmm::free_frame(mapped);
                }
            }
            task->user_stack_base = 0;
            task->user_stack = 0;
            return false;
        }
        // 清零：不把别的进程留下的内容带进新进程，也保证参数页默认是"没有参数"
        memset((void *)PADDR_TO_KVADDR(phys), 0, PAGE_SIZE);
    }

    task->user_stack_base = stack_bottom;
    task->user_stack = hal::UserContext::initial_sp(USER_ARGS_ADDR);
    return true;
}

/**
 * @brief 测试辅助：检查是否应该使栈页分配失败
 * 
 * 这是一个弱符号实现，测试代码可以覆盖它
 */
__attribute__((weak))
bool kernel::Scheduler::should_fail_stack_page(uint32_t page_index) {
    // 默认实现：永不失败
    // 测试代码会覆盖这个函数
    (void)page_index;
    return false;
}

/* ============================================================================
 * 任务创建
 * ========================================================================== */

/**
 * @brief 创建用户进程
 */
uint32_t kernel::Scheduler::create_user_process(const char *name, uintptr_t entry_point,
                                   uintptr_t space, uintptr_t program_end) {
    if (!name || !space || entry_point == 0) {
        LOG_ERROR_MSG("kernel::Scheduler::create_user_process: Invalid parameters\n");
        return 0;
    }

    // 分配 PCB
    task_t *task = kernel::Scheduler::alloc();
    if (!task) {
        LOG_ERROR_MSG("kernel::Scheduler::create_user_process: Failed to allocate PCB\n");
        return 0;
    }
    
    // 设置任务名称
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    
    // 用户进程标志
    task->is_user_process = true;
    task->privileged = true;         // 由内核直接创建的用户进程（init）
    hal::UserContext::fp_reset(&task->fp_state);
    task->user_entry = entry_point;
    
    // 分配内核栈
    task->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack_base) {
        LOG_ERROR_MSG("kernel::Scheduler::create_user_process: Failed to allocate kernel stack\n");
        kernel::Scheduler::free(task);
        return 0;
    }
    
    task->kernel_stack = task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    task->page_dir_phys = space;
    
    // 设置用户栈
    if (!kernel::Scheduler::setup_user_stack(task)) {
        LOG_ERROR_MSG("kernel::Scheduler::create_user_process: Failed to setup user stack\n");
        // 失败时地址空间仍归调用者所有（由调用者销毁），这里只交还 PCB 自己
        // 分配的资源；内核栈由 kernel::Scheduler::free 释放，不能再手动 kfree
        task->page_dir_phys = 0;
        kernel::Scheduler::free(task);
        return 0;
    }
    
    hal::UserContext::init(&task->context, entry_point, task->user_stack,
                           task->page_dir_phys, task->kernel_stack);

    // 设置堆管理
    // 堆从程序结束后的下一页开始
    task->heap_start = PAGE_ALIGN_UP(program_end);
    task->heap_end = task->heap_start;
    // 堆最大值：留出 8MB 给栈（栈在用户空间顶部）
    task->heap_max = task->user_stack_base - (8 * 1024 * 1024);
    
    LOG_DEBUG_MSG("  Heap: start=0x%llx, end=0x%llx, max=0x%llx\n", 
                 (unsigned long long)task->heap_start, 
                 (unsigned long long)task->heap_end, 
                 (unsigned long long)task->heap_max);
    
    // 添加到就绪队列
    task->state = TASK_READY;
    kernel::Scheduler::ready_queue_add(task);
    
    LOG_DEBUG_MSG("Created user process: PID=%u, name=%s, entry=0x%llx\n", 
                 task->pid, task->name, (unsigned long long)entry_point);
    
    return task->pid;
}

/* ============================================================================
 * 任务控制
 * ========================================================================== */

/**
 * @brief 任务退出
 */
void task_exit(uint32_t exit_code) {
    // 普通退出。被信号终止（kill、用户态异常）的路径直接调用 exit_current
    kernel::Scheduler::exit_current(exit_code, false, 0);
}

void kernel::Scheduler::exit_current(uint32_t exit_code, bool signaled, uint32_t signal) {
    task_t *current_task = kernel::Scheduler::get_current();
    if (!current_task) {
        LOG_ERROR_MSG("task_exit: No current task\n");
        // 无限循环，因为函数标记为 noreturn
        while (1) {
            hal::Cpu::halt();
        }
    }

    LOG_DEBUG_MSG("Task %u (%s) exiting with code %u\n",
                 current_task->pid, current_task->name, exit_code);

    // 用户态异常（x86 的 ISR 存根）是带着 interrupt_enter 的计数进来的，而退出
    // 的任务不会再回到存根执行 interrupt_exit。这里是任务自己的上下文（同步异常
    // 或系统调用，下面没有被打断的内核代码），把计数清掉；否则此后所有任务的
    // Mutex::lock 都会被当成“在中断里睡眠”。
    while (in_interrupt()) {
        interrupt_exit();
    }

    // 等着和本任务通信的任务不会再等到结果
    kernel::Ipc::on_exit(current_task);
    kernel::UserIrq::on_exit(current_task);

    kernel::Interrupts::disable();

    // 设置退出信息
    current_task->exit_code = exit_code;
    current_task->exit_signaled = signaled;
    current_task->exit_signal = signal;
    current_task->kill_pending = false;

    // 处理所有子进程
    // 遍历任务池，查找当前进程的子进程
    task_t *zombie_children[MAX_TASKS];
    uint32_t zombie_count = 0;
    
    bool irq_state_child;
    task_lock.lock_irqsave(irq_state_child);
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &task_pool[i];
        
        // 跳过未使用的任务
        if (task->state == TASK_UNUSED) {
            continue;
        }
        
        // 检查是否为当前进程的子进程
        if (task->parent == current_task) {
            if (task->state == TASK_ZOMBIE) {
                // 僵尸子进程：收集起来稍后清理
                LOG_DEBUG_MSG("Task %u: cleaning up zombie child %u\n", 
                             current_task->pid, task->pid);
                zombie_children[zombie_count++] = task;
            } else {
                // 运行中的子进程：变成孤儿进程
                // 当它们退出时会自动清理（因为没有父进程）
                LOG_DEBUG_MSG("Task %u: orphaning child %u\n", 
                             current_task->pid, task->pid);
                task->parent = NULL;
            }
        }
    }
    
    task_lock.unlock_irqrestore(irq_state_child);
    
    // 在锁外清理僵尸子进程
    for (uint32_t i = 0; i < zombie_count; i++) {
        kernel::Scheduler::free(zombie_children[i]);
    }
    
    // 如果有父进程，变成僵尸进程等待父进程回收
    // 否则直接终止（孤儿进程）
    if (current_task->parent && current_task->parent->state != TASK_UNUSED) {
        current_task->state = TASK_ZOMBIE;
        // 父进程可能正阻塞在 waitpid 里等我们
        kernel::Scheduler::wakeup(current_task->parent);
        LOG_DEBUG_MSG("Task %u becomes zombie, waiting for parent %u\n", 
                     current_task->pid, current_task->parent->pid);
    } else {
        current_task->state = TASK_TERMINATED;
        LOG_DEBUG_MSG("Task %u has no parent, terminating directly\n", current_task->pid);
    }
    
    // 释放资源
    // 注意：不能在这里调用 kernel::Scheduler::free，因为我们还在使用当前任务的栈
    // 清理工作由调度器或父进程的 wait/waitpid 完成

    // 切换到其他任务。调度器的"当前任务"必须保持指向本任务：schedule() 靠它看到
    // TERMINATED 状态并把任务挂到延迟清理链表；清成 NULL 的话无父进程的任务
    // （孤儿、返回的内核线程）永远不会被回收。
    kernel::Scheduler::schedule();
    
    // 永远不会执行到这里
    while (1) {
        hal::Cpu::halt();
    }
}

/**
 * @brief 请求终止另一个任务（只记录，不动目标的状态和资源）
 */
bool kernel::Scheduler::request_kill(task_t *target, uint32_t signal) {
    if (!target) {
        return false;
    }

    bool wake = false;
    {
        sync::SpinlockIrqGuard guard(task_lock);

        if (target->state == TASK_UNUSED || target->state == TASK_ZOMBIE ||
            target->state == TASK_TERMINATED) {
            return false;
        }

        // 已有待处理的请求时保留第一个信号
        if (!target->kill_pending) {
            target->kill_pending = true;
            target->kill_signal = signal;
        }

        // 正在 sleep、等待 IPC 或等待子进程的任务提前唤醒，让它尽快走到系统调用出口
        // （这些等待循环看到 kill_pending 都会放弃）。
        // 阻塞在 Mutex/Semaphore 上的任务不能唤醒：它们醒来后会重新检查条件
        // 并再次阻塞，要等到被正常唤醒后才会走到出口。
        if (target->state == TASK_BLOCKED &&
            (target->sleep_until_ms > 0 || target->ipc_state != IPC_IDLE ||
             target->wait_object == target /* 在 waitpid 里等子进程 */)) {
            target->sleep_until_ms = 0;
            target->wait_object = NULL;
            target->state = TASK_READY;
            wake = true;
        }
    }

    // 在锁外添加到就绪队列
    if (wake) {
        kernel::Scheduler::ready_queue_add(target);
    }
    return true;
}

bool kernel::Scheduler::current_is_privileged() {
    task_t *task = kernel::Scheduler::get_current();
    return !task || !task->is_user_process || task->privileged;
}

bool kernel::Scheduler::is_descendant(task_t *target, task_t *ancestor) {
    if (!target || !ancestor) {
        return false;
    }
    // 父链长度不会超过任务表大小；计数只是防止损坏的链表造成死循环
    task_t *p = target->parent;
    for (uint32_t depth = 0; p && depth < MAX_TASKS; depth++) {
        if (p == ancestor) {
            return true;
        }
        p = p->parent;
    }
    return false;
}

/**
 * @brief 若当前任务有待处理的 kill，就地退出
 */
void kernel::Scheduler::deliver_pending_kill() {
    task_t *task = kernel::Scheduler::get_current();
    if (!task || !task->kill_pending) {
        return;
    }

    uint32_t signal = task->kill_signal;
    task->kill_pending = false;
    LOG_DEBUG_MSG("Task %u (%s) terminated by signal %u\n", task->pid, task->name, signal);
    kernel::Scheduler::exit_current(128 + signal, true, signal);
}
