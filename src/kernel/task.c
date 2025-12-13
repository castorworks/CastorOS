// ============================================================================
// task.c - 任务管理实现
// ============================================================================

#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <kernel/fd_table.h>
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

/* GDT is x86-specific */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <kernel/gdt.h>
#endif

// 辅助函数：检查页目录项是否存在
static inline bool is_present(uint32_t pde) { return pde & 0x1; }
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

/** @brief 下一个可用的 PID */
static uint32_t next_pid = 1;

/** @brief 活动任务计数 */
static uint32_t active_task_count = 0;

/** @brief idle 任务指针 */
static task_t *idle_task = NULL;

/** @brief 调度器是否已初始化 */
static bool scheduler_initialized = false;

/** @brief 任务管理全局锁 - 保护任务池、就绪队列和 PID 分配 */
static spinlock_t task_lock;

/** @brief 待清理的 terminated 任务（用于延迟清理） */
static task_t *pending_cleanup_task = NULL;

/* ============================================================================
 * 辅助函数：就绪队列操作
 * ========================================================================== */

/**
 * @brief 将任务添加到就绪队列尾部
 */
void ready_queue_add(task_t *task) {
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
    
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    task->next = NULL;
    task->prev = ready_queue_tail;
    
    if (ready_queue_tail) {
        ready_queue_tail->next = task;
    } else {
        ready_queue_head = task;
    }
    
    ready_queue_tail = task;
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
}

/**
 * @brief 从就绪队列移除任务
 */
void ready_queue_remove(task_t *task) {
    if (!task) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    if (task->prev) {
        task->prev->next = task->next;
    } else {
        ready_queue_head = task->next;
    }
    
    if (task->next) {
        task->next->prev = task->prev;
    } else {
        ready_queue_tail = task->prev;
    }
    
    task->next = NULL;
    task->prev = NULL;
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
}

/**
 * @brief 从就绪队列获取下一个任务
 * 
 * @return 下一个就绪任务，如果队列为空返回 NULL
 */
static task_t* ready_queue_pop(void) {
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
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
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
    return task;
}

/* ============================================================================
 * 辅助函数：任务控制块管理
 * ========================================================================== */

/**
 * @brief 分配一个空闲的任务控制块
 */
task_t* task_alloc(void) {
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_UNUSED) {
            memset(&task_pool[i], 0, sizeof(task_t));
            task_pool[i].pid = next_pid++;
            task_pool[i].state = TASK_READY;
            task_pool[i].priority = DEFAULT_PRIORITY;
            task_pool[i].time_slice = DEFAULT_TIME_SLICE;
            active_task_count++;
            
            spinlock_unlock_irqrestore(&task_lock, irq_state);
            return &task_pool[i];
        }
    }
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
    LOG_ERROR_MSG("task_alloc: No free PCB available (max: %d)\n", MAX_TASKS);
    return NULL;
}

/**
 * @brief 释放任务控制块
 */
void task_free(task_t *task) {
    if (!task) {
        return;
    }
    
    // 注意：不能在持有 spinlock 时调用可能阻塞的函数（kfree、vmm_free_page_directory）
    // 所以先在锁外释放资源，最后在锁内清理 PCB
    
    uintptr_t kernel_stack_base = task->kernel_stack_base;
    bool is_user = task->is_user_process;
    uintptr_t page_dir_phys = task->page_dir_phys;
    
    // 释放内核栈（在锁外执行）
    if (kernel_stack_base) {
        kfree((void*)kernel_stack_base);
    }
    
    // 释放文件描述符表
    if (task->fd_table) {
        // 先关闭所有打开的文件描述符
        for (int i = 0; i < MAX_FDS; i++) {
            if (task->fd_table->entries[i].in_use) {
                fd_table_free(task->fd_table, i);
            }
        }
        // 再释放文件描述符表本身
        kfree(task->fd_table);
    }
    
    // 释放页目录（在锁外执行，仅用户进程）
    if (is_user && page_dir_phys) {
        vmm_free_page_directory(page_dir_phys);
    }
    
    // 在锁内清空 PCB
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    memset(task, 0, sizeof(task_t));
    task->state = TASK_UNUSED;
    active_task_count--;
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
}

/**
 * @brief 根据 PID 查找任务
 */
task_t* task_get_by_pid(uint32_t pid) {
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state != TASK_UNUSED && task_pool[i].pid == pid) {
            spinlock_unlock_irqrestore(&task_lock, irq_state);
            return &task_pool[i];
        }
    }
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
    return NULL;
}

/**
 * @brief 获取当前任务
 */
task_t* task_get_current(void) {
    return current_task;
}

/**
 * @brief 获取活动任务数量
 */
uint32_t task_get_count(void) {
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    uint32_t count = active_task_count;
    spinlock_unlock_irqrestore(&task_lock, irq_state);
    return count;
}

/* ============================================================================
 * 用户栈设置
 * ========================================================================== */

/**
 * @brief 为用户进程设置用户栈
 */
bool task_setup_user_stack(task_t *task) {
    if (!task || !task->is_user_process || !task->page_dir) {
        LOG_ERROR_MSG("task_setup_user_stack: Invalid task\n");
        return false;
    }
    
    // 用户栈位于用户空间顶部（0x80000000 - USER_STACK_SIZE）
    uint32_t stack_top = USER_SPACE_END;
    uint32_t stack_bottom = stack_top - USER_STACK_SIZE;
    
    // 分配并映射用户栈页面
    uint32_t num_pages = USER_STACK_SIZE / PAGE_SIZE;
    
    LOG_DEBUG_MSG("task_setup_user_stack: Allocating %u pages for user stack\n", num_pages);
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t virt_addr = stack_bottom + (i * PAGE_SIZE);
        
        // 测试模式：检查是否应该模拟分配失败
        if (task_should_fail_stack_page(i)) {
            LOG_DEBUG_MSG("task_setup_user_stack: Simulating allocation failure at page %u\n", i);
            
            // 清理已分配的页面
            for (uint32_t j = 0; j < i; j++) {
                uint32_t cleanup_virt = stack_bottom + (j * PAGE_SIZE);
                uint32_t phys = vmm_unmap_page_in_directory(task->page_dir_phys, cleanup_virt);
                if (phys) {
                    pmm_free_frame(phys);
                }
            }
            
            // 清理空的页表
            vmm_cleanup_empty_page_tables(task->page_dir_phys, stack_bottom, stack_top);
            
            task->user_stack_base = 0;
            task->user_stack = 0;
            return false;
        }
        
        // 分配物理页
        paddr_t phys_addr = pmm_alloc_frame();
        if (phys_addr == PADDR_INVALID) {
            LOG_ERROR_MSG("task_setup_user_stack: Failed to allocate physical page %u/%u\n", 
                         i + 1, num_pages);
            
            // 清理已分配的页面
            for (uint32_t j = 0; j < i; j++) {
                uint32_t cleanup_virt = stack_bottom + (j * PAGE_SIZE);
                uint32_t cleanup_phys = vmm_unmap_page_in_directory(task->page_dir_phys, cleanup_virt);
                if (cleanup_phys) {
                    pmm_free_frame(cleanup_phys);
                }
            }
            
            // 清理空的页表
            vmm_cleanup_empty_page_tables(task->page_dir_phys, stack_bottom, stack_top);
            
            return false;
        }
        
        // 映射到用户空间（用户可读写）
        if (!vmm_map_page_in_directory(task->page_dir_phys, virt_addr, (uintptr_t)phys_addr,
                                       PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
            LOG_ERROR_MSG("task_setup_user_stack: Failed to map page %u/%u\n", i + 1, num_pages);
            
            // 释放刚分配的物理页
            pmm_free_frame(phys_addr);
            
            // 清理之前映射的页面
            for (uint32_t j = 0; j < i; j++) {
                uint32_t cleanup_virt = stack_bottom + (j * PAGE_SIZE);
                uint32_t cleanup_phys = vmm_unmap_page_in_directory(task->page_dir_phys, cleanup_virt);
                if (cleanup_phys) {
                    pmm_free_frame(cleanup_phys);
                }
            }
            
            // 清理空的页表
            vmm_cleanup_empty_page_tables(task->page_dir_phys, stack_bottom, stack_top);
            
            return false;
        }
    }
    
    // 设置栈指针（栈顶，向下增长，减去 4 字节对齐）
    task->user_stack_base = stack_bottom;
    task->user_stack = stack_top - 4;
    
    LOG_DEBUG_MSG("task_setup_user_stack: User stack set up at 0x%x-0x%x\n", 
                 stack_bottom, stack_top);
    
    return true;
}

/**
 * @brief 测试辅助：检查是否应该使栈页分配失败
 * 
 * 这是一个弱符号实现，测试代码可以覆盖它
 */
__attribute__((weak))
bool task_should_fail_stack_page(uint32_t page_index) {
    // 默认实现：永不失败
    // 测试代码会覆盖这个函数
    (void)page_index;
    return false;
}

/* ============================================================================
 * 任务创建
 * ========================================================================== */

/**
 * @brief 创建内核线程
 */
uint32_t task_create_kernel_thread(void (*entry)(void), const char *name) {
    if (!entry) {
        LOG_ERROR_MSG("task_create_kernel_thread: Invalid entry point\n");
        return 0;
    }
    
    // 分配 PCB
    task_t *task = task_alloc();
    if (!task) {
        LOG_ERROR_MSG("task_create_kernel_thread: Failed to allocate PCB\n");
        return 0;
    }
    
    // 设置任务名称
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    
    // 内核线程标志
    task->is_user_process = false;
    
    // 分配内核栈
    task->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack_base) {
        LOG_ERROR_MSG("task_create_kernel_thread: Failed to allocate kernel stack\n");
        task_free(task);
        return 0;
    }
    
    // 内核栈顶（栈向下增长）
    task->kernel_stack = task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    // 使用内核页目录
    task->page_dir_phys = vmm_get_page_directory();
    task->page_dir = (page_directory_t*)PHYS_TO_VIRT(task->page_dir_phys);
    
    // 初始化上下文
    memset(&task->context, 0, sizeof(cpu_context_t));
    
    // 设置段寄存器（内核段）
    task->context.cs = GDT_KERNEL_CODE_SEGMENT;  // 0x08
    task->context.ss = GDT_KERNEL_DATA_SEGMENT;  // 0x10
#if !defined(ARCH_X86_64)
    // i686: 需要设置所有段寄存器
    task->context.ds = GDT_KERNEL_DATA_SEGMENT;
    task->context.es = GDT_KERNEL_DATA_SEGMENT;
    task->context.fs = GDT_KERNEL_DATA_SEGMENT;
    task->context.gs = GDT_KERNEL_DATA_SEGMENT;
#endif
    
    // 设置栈指针
    task->context.esp = task->kernel_stack;
    
    // 设置入口点（通过 task_enter_kernel_thread 包装）
    task->context.eip = (uintptr_t)task_enter_kernel_thread;
    
    // 设置 EFLAGS（启用中断）
    task->context.eflags = 0x202;  // IF=1
    
    // 设置 CR3
    task->context.cr3 = task->page_dir_phys;
    
    // 在栈上压入入口函数地址（task_enter_kernel_thread 会从栈顶获取）
    // task_enter_kernel_thread 执行 pop eax/rax，所以栈顶应该是入口函数地址
    uintptr_t *stack_ptr = (uintptr_t*)task->kernel_stack;
    stack_ptr[-1] = (uintptr_t)entry;       // 入口函数
    task->context.esp = (uintptr_t)&stack_ptr[-1];  // ESP/RSP 指向入口函数
    
    // 文件描述符表（内核线程不需要）
    task->fd_table = NULL;
    
    // 工作目录
    strcpy(task->cwd, "/");
    
    // 添加到就绪队列
    task->state = TASK_READY;
    ready_queue_add(task);
    
    LOG_INFO_MSG("Created kernel thread: PID=%u, name=%s\n", task->pid, task->name);
    
    return task->pid;
}

/**
 * @brief 创建用户进程
 */
uint32_t task_create_user_process(const char *name, uintptr_t entry_point,
                                   page_directory_t *page_dir, uintptr_t program_end) {
    if (!name || !page_dir || entry_point == 0) {
        LOG_ERROR_MSG("task_create_user_process: Invalid parameters\n");
        return 0;
    }
    
    // 分配 PCB
    task_t *task = task_alloc();
    if (!task) {
        LOG_ERROR_MSG("task_create_user_process: Failed to allocate PCB\n");
        return 0;
    }
    
    // 设置任务名称
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    
    // 用户进程标志
    task->is_user_process = true;
    task->user_entry = entry_point;
    
    // 分配内核栈
    task->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack_base) {
        LOG_ERROR_MSG("task_create_user_process: Failed to allocate kernel stack\n");
        task_free(task);
        return 0;
    }
    
    task->kernel_stack = task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    // 设置页目录
    task->page_dir_phys = VIRT_TO_PHYS((uintptr_t)page_dir);
    task->page_dir = page_dir;
    
    // 设置用户栈
    if (!task_setup_user_stack(task)) {
        LOG_ERROR_MSG("task_create_user_process: Failed to setup user stack\n");
        kfree((void*)task->kernel_stack_base);
        task_free(task);
        return 0;
    }
    
    // 初始化上下文
    memset(&task->context, 0, sizeof(cpu_context_t));
    
    // 设置段寄存器（用户段，Ring 3）
    task->context.cs = GDT_USER_CODE_SEGMENT | 3;  // 0x1B
    task->context.ss = GDT_USER_DATA_SEGMENT | 3;  // 0x23
#if !defined(ARCH_X86_64)
    // i686: 需要设置所有段寄存器
    task->context.ds = GDT_USER_DATA_SEGMENT | 3;
    task->context.es = GDT_USER_DATA_SEGMENT | 3;
    task->context.fs = GDT_USER_DATA_SEGMENT | 3;
    task->context.gs = GDT_USER_DATA_SEGMENT | 3;
#endif
    
    // 设置用户栈指针
    task->context.esp = task->user_stack;
    
    // 设置用户入口点
    task->context.eip = entry_point;
    
    // 设置 EFLAGS（启用中断）
    task->context.eflags = 0x202;  // IF=1
    
    // 设置 CR3
    task->context.cr3 = task->page_dir_phys;
    
    // 分配文件描述符表
    task->fd_table = (fd_table_t*)kmalloc(sizeof(fd_table_t));
    if (!task->fd_table) {
        LOG_ERROR_MSG("task_create_user_process: Failed to allocate fd_table\n");
        kfree((void*)task->kernel_stack_base);
        task_free(task);
        return 0;
    }
    
    fd_table_init(task->fd_table);
    
    // 打开标准输入/输出/错误（指向 /dev/console）
    fs_node_t *console = vfs_path_to_node("/dev/console");
    if (console) {
        fd_table_alloc(task->fd_table, console, 0); // stdin (fd 0)
        fd_table_alloc(task->fd_table, console, 0); // stdout (fd 1)
        fd_table_alloc(task->fd_table, console, 0); // stderr (fd 2)
        // 关键修复：释放初始引用（fd_table_alloc 已经增加了 3 次引用计数）
        vfs_release_node(console);
        LOG_DEBUG_MSG("  Opened stdin/stdout/stderr for process\n");
    } else {
        LOG_WARN_MSG("  Failed to open /dev/console for stdio\n");
    }
    
    // 设置堆管理
    // 堆从程序结束后的下一页开始
    task->heap_start = PAGE_ALIGN_UP(program_end);
    task->heap_end = task->heap_start;
    // 堆最大值：留出 8MB 给栈（栈在用户空间顶部）
    task->heap_max = task->user_stack_base - (8 * 1024 * 1024);
    
    LOG_DEBUG_MSG("  Heap: start=0x%x, end=0x%x, max=0x%x\n", 
                 task->heap_start, task->heap_end, task->heap_max);
    
    // 工作目录
    strcpy(task->cwd, "/");
    
    // 添加到就绪队列
    task->state = TASK_READY;
    ready_queue_add(task);
    
    LOG_INFO_MSG("Created user process: PID=%u, name=%s, entry=0x%x\n", 
                 task->pid, task->name, entry_point);
    
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
        // 暂停 CPU 直到下一次中断
        hal_cpu_halt();
        
        // 在中断返回后，主动让出 CPU
        // 这样如果有任务被唤醒，它们就能得到执行
        task_yield();
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
    idle_task->page_dir_phys = vmm_get_page_directory();
    idle_task->page_dir = (page_directory_t*)PHYS_TO_VIRT(idle_task->page_dir_phys);
    
    // 初始化上下文
    memset(&idle_task->context, 0, sizeof(cpu_context_t));
    
    idle_task->context.cs = GDT_KERNEL_CODE_SEGMENT;
    idle_task->context.ss = GDT_KERNEL_DATA_SEGMENT;
#if !defined(ARCH_X86_64)
    // i686: 需要设置所有段寄存器
    idle_task->context.ds = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.es = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.fs = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.gs = GDT_KERNEL_DATA_SEGMENT;
#endif
    
    idle_task->context.esp = idle_task->kernel_stack;
    idle_task->context.eip = (uintptr_t)task_enter_kernel_thread;
    idle_task->context.eflags = 0x202;
    idle_task->context.cr3 = idle_task->page_dir_phys;
    
    // 在栈上压入入口函数
    // task_enter_kernel_thread 会执行 pop eax/rax 获取入口函数
    // 所以栈顶应该是入口函数地址
    uintptr_t *stack_ptr = (uintptr_t*)idle_task->kernel_stack;
    stack_ptr[-1] = (uintptr_t)idle_task_loop;  // 入口函数地址
    idle_task->context.esp = (uintptr_t)&stack_ptr[-1];  // ESP/RSP 指向入口函数
    
    idle_task->fd_table = NULL;
    strcpy(idle_task->cwd, "/");
    
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
void task_schedule(void) {
    if (!scheduler_initialized) {
        return;
    }
    
    bool prev_state = interrupts_disable();
    
    // 【关键修复】先清理上一次延迟的 terminated 任务
    // 现在我们已经在新任务的栈上了，可以安全地释放旧任务的资源
    if (pending_cleanup_task) {
        task_t *task_to_cleanup = pending_cleanup_task;
        pending_cleanup_task = NULL;  // 清空待清理指针
        
        LOG_INFO_MSG("Cleaning up terminated task %u (%s)\n", 
                     task_to_cleanup->pid, task_to_cleanup->name);
        
        // 保存需要释放的资源信息（在 task_free 会清空 PCB）
        uintptr_t kernel_stack_base = task_to_cleanup->kernel_stack_base;
        bool is_user = task_to_cleanup->is_user_process;
        uintptr_t page_dir_phys = task_to_cleanup->page_dir_phys;
        fd_table_t *fd_table = task_to_cleanup->fd_table;
        
        // 先在锁内清空 PCB
        bool irq_state_cleanup;
        spinlock_lock_irqsave(&task_lock, &irq_state_cleanup);
        memset(task_to_cleanup, 0, sizeof(task_t));
        task_to_cleanup->state = TASK_UNUSED;
        active_task_count--;
        spinlock_unlock_irqrestore(&task_lock, irq_state_cleanup);
        
        // 然后在锁外释放资源（避免死锁）
        if (kernel_stack_base) {
            kfree((void*)kernel_stack_base);
        }
        
        if (fd_table) {
            // 关闭所有打开的文件描述符
            for (int i = 0; i < MAX_FDS; i++) {
                if (fd_table->entries[i].in_use) {
                    fd_table_free(fd_table, i);
                }
            }
            kfree(fd_table);
        }
        
        if (is_user && page_dir_phys) {
            vmm_free_page_directory(page_dir_phys);  // ✅ 释放页目录
        }
        
        LOG_DEBUG_MSG("Terminated task cleanup complete\n");
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
            ready_queue_add(prev_task);
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
    
    // 更新 TSS 内核栈
    if (next_task->is_user_process) {
        tss_set_kernel_stack(next_task->kernel_stack);
#if defined(ARCH_X86_64)
        // x86_64: Also set kernel stack for SYSCALL mechanism
        extern void hal_syscall_set_kernel_stack(uint64_t stack_ptr);
        hal_syscall_set_kernel_stack((uint64_t)next_task->kernel_stack);
#endif
    }
    
    // 关键修复：在上下文切换前，先同步 VMM 的 current_dir_phys
    // task_switch_context 会直接修改 CR3，但不会更新 current_dir_phys
    // 我们必须在切换前就更新，因为切换后不能再调用任何函数
    if (prev_task != next_task && next_task->is_user_process) {
        vmm_sync_current_dir(next_task->page_dir_phys);
        
        // 【调试】如果是切换到 Shell，验证其页目录完整性
        if (next_task->pid == 1) {
            page_directory_t *shell_dir = next_task->page_dir;
            if (is_present(shell_dir->entries[1])) {
                uintptr_t phys = get_frame(shell_dir->entries[1]);
                if (phys == 0 || phys >= KERNEL_VIRTUAL_BASE) {
                    LOG_ERROR_MSG("Shell PDE[1] corrupted BEFORE switching to it!\n");
                    LOG_ERROR_MSG("  PDE[0]=0x%llx, PDE[1]=0x%llx, PDE[2]=0x%llx, PDE[3]=0x%llx\n",
                                 (unsigned long long)shell_dir->entries[0], 
                                 (unsigned long long)shell_dir->entries[1],
                                 (unsigned long long)shell_dir->entries[2], 
                                 (unsigned long long)shell_dir->entries[3]);
                }
            }
        }
    }
    
    // 执行上下文切换
    // 注意：切换后不能调用任何函数，因为栈已经切换了
    if (prev_task != next_task) {
        cpu_context_t *old_ctx_ptr = prev_task ? &prev_task->context : NULL;
        
        // 如果需要释放 prev_task，必须在切换前处理
        // 但我们不能在切换前释放，因为还在使用 prev_task 的栈和上下文
        // 解决方案：标记为待清理，下次调度时清理（那时已在新栈上）
        if (should_free_prev_task) {
            // ✅ 将任务标记为待清理，下次调度时会在新栈上安全清理
            pending_cleanup_task = prev_task;
            LOG_DEBUG_MSG("Task %u (%s) marked for deferred cleanup\n",
                        prev_task->pid, prev_task->name);
        }
        
#if defined(ARCH_X86_64)
        // Debug: Print context before switching to user process
        if (next_task->is_user_process) {
            LOG_INFO_MSG("Switching to user process %u (%s):\n", next_task->pid, next_task->name);
            LOG_INFO_MSG("  RIP=0x%llx, RSP=0x%llx\n", 
                         (unsigned long long)next_task->context.eip,
                         (unsigned long long)next_task->context.esp);
            LOG_INFO_MSG("  CS=0x%llx, SS=0x%llx, RFLAGS=0x%llx\n",
                         (unsigned long long)next_task->context.cs,
                         (unsigned long long)next_task->context.ss,
                         (unsigned long long)next_task->context.eflags);
            LOG_INFO_MSG("  CR3=0x%llx\n", (unsigned long long)next_task->context.cr3);
        }
#endif
        
        task_switch_context(&old_ctx_ptr, &next_task->context);
        
        // 注意：永远不会执行到这里（task_switch_context 不会返回到这里）
        // 下一次进入这个函数时，已经是在新任务的上下文中了
    }
    
    interrupts_restore(prev_state);
}

/**
 * @brief 定时器中断处理
 */
void task_timer_tick(void) {
    if (!scheduler_initialized || !current_task) {
        return;
    }
    
    // 更新当前任务的运行时间
    uint32_t tick_ms = 1000 / timer_get_frequency();
    current_task->runtime_ms += tick_ms;
    
    // 检查睡眠任务是否应该唤醒
    uint64_t current_time_ms = timer_get_uptime_ms();
    
    // 收集需要唤醒的任务（避免在持有锁时调用 ready_queue_add）
    task_t *tasks_to_wake[MAX_TASKS];
    uint32_t wake_count = 0;
    
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &task_pool[i];
        
        if (task->state == TASK_BLOCKED && task->sleep_until_ms > 0) {
            if (current_time_ms >= task->sleep_until_ms) {
                task->sleep_until_ms = 0;
                task->state = TASK_READY;
                tasks_to_wake[wake_count++] = task;
            }
        }
    }
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
    
    // 在锁外将任务添加到就绪队列
    for (uint32_t i = 0; i < wake_count; i++) {
        ready_queue_add(tasks_to_wake[i]);
    }
    
    // 时间片轮转调度
    // 注意：这个函数在 IRQ 中调用，调度将在 IRQ 返回时由 schedule_from_irq 处理
    static uint32_t tick_count = 0;
    tick_count++;
    
    if (tick_count >= current_task->time_slice) {
        tick_count = 0;
        // 在 IRQ 上下文中不调用 task_schedule()
        // 调度会在 IRQ handler 返回时自动处理
    }
}

/* ============================================================================
 * 任务控制
 * ========================================================================== */

/**
 * @brief 任务退出
 */
void task_exit(uint32_t exit_code) {
    bool prev_state = interrupts_disable();
    
    if (!current_task) {
        LOG_ERROR_MSG("task_exit: No current task\n");
        interrupts_restore(prev_state);
        // 无限循环，因为函数标记为 noreturn
        while (1) {
            hal_cpu_halt();
        }
    }
    
    LOG_INFO_MSG("Task %u (%s) exiting with code %u\n", 
                 current_task->pid, current_task->name, exit_code);
    
    // 设置退出信息
    current_task->exit_code = exit_code;
    current_task->exit_signaled = false;
    
    // 处理所有子进程
    // 遍历任务池，查找当前进程的子进程
    task_t *zombie_children[MAX_TASKS];
    uint32_t zombie_count = 0;
    
    bool irq_state_child;
    spinlock_lock_irqsave(&task_lock, &irq_state_child);
    
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
    
    spinlock_unlock_irqrestore(&task_lock, irq_state_child);
    
    // 在锁外清理僵尸子进程
    for (uint32_t i = 0; i < zombie_count; i++) {
        task_free(zombie_children[i]);
    }
    
    // 如果有父进程，变成僵尸进程等待父进程回收
    // 否则直接终止（孤儿进程）
    if (current_task->parent && current_task->parent->state != TASK_UNUSED) {
        current_task->state = TASK_ZOMBIE;
        LOG_DEBUG_MSG("Task %u becomes zombie, waiting for parent %u\n", 
                     current_task->pid, current_task->parent->pid);
    } else {
        current_task->state = TASK_TERMINATED;
        LOG_DEBUG_MSG("Task %u has no parent, terminating directly\n", current_task->pid);
    }
    
    // 释放资源
    // 注意：不能在这里调用 task_free，因为我们还在使用当前任务的栈
    // 清理工作由调度器或父进程的 wait/waitpid 完成
    
    // 切换到其他任务
    current_task = NULL;
    task_schedule();
    
    // 永远不会执行到这里
    while (1) {
        hal_cpu_halt();
    }
}

/**
 * @brief 主动让出 CPU
 */
void task_yield(void) {
    task_schedule();
}

/**
 * @brief 任务睡眠
 */
void task_sleep(uint32_t ms) {
    if (!current_task || ms == 0) {
        return;
    }
    
    bool prev_state = interrupts_disable();
    
    // 计算唤醒时间
    uint64_t wake_time = timer_get_uptime_ms() + ms;
    current_task->sleep_until_ms = wake_time;
    current_task->state = TASK_BLOCKED;
    
    // 切换到其他任务
    task_schedule();
    
    interrupts_restore(prev_state);
}

/**
 * @brief 阻塞当前任务（用于同步原语）
 * 
 * @param wait_object 等待对象指针（用于调试）
 */
void task_block(void *wait_object) {
    (void)wait_object;  // 暂不使用
    
    if (!current_task) {
        return;
    }
    
    bool prev_state = interrupts_disable();
    
    current_task->state = TASK_BLOCKED;
    
    LOG_DEBUG_MSG("Task %u (%s) blocked on %p\n", 
                 current_task->pid, current_task->name, wait_object);
    
    // 触发调度，切换到其他任务
    task_schedule();
    
    interrupts_restore(prev_state);
}

/**
 * @brief 唤醒等待在指定对象上的一个任务
 * 
 * @param wait_object 等待对象指针（用于调试）
 */
void task_wakeup(void *wait_object) {
    (void)wait_object;  // 暂不使用
    
    task_t *task_to_wake = NULL;
    
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    // 简化实现：唤醒第一个阻塞的任务
    // 完整实现应该维护每个等待对象的等待队列
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &task_pool[i];
        
        if (task->state == TASK_BLOCKED && task->sleep_until_ms == 0) {
            task->state = TASK_READY;
            task_to_wake = task;
            LOG_DEBUG_MSG("Task %u (%s) woken up\n", task->pid, task->name);
            break;  // 只唤醒一个任务
        }
    }
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
    
    // 在锁外添加到就绪队列
    if (task_to_wake) {
        ready_queue_add(task_to_wake);
    }
}

/**
 * @brief 从中断上下文调度
 * 
 * 这个函数由 IRQ 处理程序调用，用于在中断返回时可能触发调度
 * 
 * @param regs 中断寄存器状态（未使用）
 */
void schedule_from_irq(void *regs) {
    (void)regs;
    
    // 注意：不能在 IRQ 上下文中直接调用 task_schedule()！
    // 因为我们还在 IRQ 处理程序的栈帧中，切换任务会导致栈混乱
    // 
    // 正确的做法是：
    // 1. 在 IRQ 返回到用户态之前进行调度（需要修改 IRQ 汇编代码）
    // 2. 或者使用软中断/延迟调度机制
    //
    // 当前暂时禁用从 IRQ 的调度
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

/**
 * @brief 初始化任务管理系统
 */
void task_init(void) {
    LOG_INFO_MSG("Initializing task management...\n");
    
    // 初始化任务管理锁
    spinlock_init(&task_lock);
    
    // 清空任务池
    memset(task_pool, 0, sizeof(task_pool));
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_UNUSED;
    }
    
    // 初始化全局变量
    current_task = NULL;
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    next_pid = 1;
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

/* ============================================================================
 * 调试和监控
 * ========================================================================== */

/**
 * @brief 打印所有任务信息
 */
void task_print_all(void) {
    kprintf("\n=== Task List ===\n");
    kprintf("PID  State     Priority  Runtime(ms)  Name\n");
    kprintf("---  --------  --------  -----------  ----\n");
    
    bool irq_state;
    spinlock_lock_irqsave(&task_lock, &irq_state);
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = &task_pool[i];
        
        if (task->state != TASK_UNUSED) {
            const char *state_str = "UNKNOWN";
            switch (task->state) {
                case TASK_READY:      state_str = "READY"; break;
                case TASK_RUNNING:    state_str = "RUNNING"; break;
                case TASK_BLOCKED:    state_str = "BLOCKED"; break;
                case TASK_ZOMBIE:     state_str = "ZOMBIE"; break;
                case TASK_TERMINATED: state_str = "TERMINATED"; break;
                default: break;
            }
            
            kprintf("%-4u %-8s %-9u %-12llu %s%s\n",
                   task->pid, state_str, task->priority, task->runtime_ms,
                   task->name,
                   (task == current_task) ? " (current)" : "");
        }
    }
    
    kprintf("\nActive tasks: %u / %u\n", active_task_count, MAX_TASKS);
    
    spinlock_unlock_irqrestore(&task_lock, irq_state);
}

