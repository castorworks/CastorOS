// ============================================================================
// task.c - 任务管理实现
// ============================================================================

#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <kernel/user_irq.h>
#include <kernel/sync/spinlock.h>
#include <hal/hal.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/timer.h>

#include <hal/user_context.h>

// 辅助函数：从页目录项中提取物理地址
static inline uint32_t get_frame(uint32_t pde) { return pde & 0xFFFFF000; }

/* ============================================================================
 * 全局变量
 * ========================================================================== */

/** @brief 任务控制块池 */
task_t task_pool[MAX_TASKS];

/** @brief 当前正在运行的任务 */
static task_t *current_task = NULL;

/** @brief 就绪队列头指针 */
static task_t *ready_queue_head = NULL;

/** @brief 就绪队列尾指针 */
static task_t *ready_queue_tail = NULL;

/** @brief 下一个可用的 PID（INIT_PID 留给 init） */
static uint32_t next_pid = INIT_PID + 1;

/** @brief 活动任务计数 */
static uint32_t active_task_count = 0;

/** @brief idle 任务指针 */
static task_t *idle_task = NULL;

/** @brief 调度器是否已初始化 */
static bool scheduler_initialized = false;

/** @brief 任务管理全局锁 - 保护任务池、就绪队列和 PID 分配 */
static sync::Spinlock task_lock;

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
                active_task_count++;
            
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
    active_task_count--;
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

/**
 * @brief 获取当前任务
 */
task_t* kernel::Scheduler::get_current() {
    return current_task;
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
    
    active_task_count++;
    
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

/* ============================================================================
 * 任务控制
 * ========================================================================== */

/**
 * @brief 任务退出
 */
void task_exit(uint32_t exit_code) {
    // 异常处理路径（arm64）可能在调用前已经写好 exit_signaled/exit_signal，
    // 这里原样保留；PCB 分配时清零，普通退出时它们就是 false/0
    task_t *task = current_task;
    kernel::Scheduler::exit_current(exit_code,
                                    task ? task->exit_signaled : false,
                                    task ? task->exit_signal : 0);
}

void kernel::Scheduler::exit_current(uint32_t exit_code, bool signaled, uint32_t signal) {
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

    // 切换到其他任务。current_task 必须保持指向本任务：schedule() 靠它看到
    // TERMINATED 状态并把任务挂到延迟清理链表；清成 NULL 的话无父进程的任务
    // （孤儿、返回的内核线程）永远不会被回收。
    kernel::Scheduler::schedule();
    
    // 永远不会执行到这里
    while (1) {
        hal::Cpu::halt();
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

/**
 * @brief 若当前任务有待处理的 kill，就地退出
 */
bool kernel::Scheduler::current_is_privileged() {
    task_t *task = current_task;
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

void kernel::Scheduler::deliver_pending_kill() {
    task_t *task = current_task;
    if (!task || !task->kill_pending) {
        return;
    }

    uint32_t signal = task->kill_signal;
    task->kill_pending = false;
    LOG_DEBUG_MSG("Task %u (%s) terminated by signal %u\n", task->pid, task->name, signal);
    kernel::Scheduler::exit_current(128 + signal, true, signal);
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
    
    // 初始化任务管理锁
    task_lock.init();
    
    // 清空任务池
    memset(task_pool, 0, sizeof(task_pool));
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_UNUSED;
    }
    
    // 初始化全局变量
    current_task = NULL;
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    next_pid = INIT_PID + 1;
    active_task_count = 0;
    
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

