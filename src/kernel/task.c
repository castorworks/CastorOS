// ============================================================================
// task.c - 任务管理实现
// ============================================================================

#include <kernel/task.h>
#include <kernel/gdt.h>
#include <kernel/tss.h>
#include <kernel/panic.h>
#include <kernel/interrupt.h>
#include <kernel/user.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <drivers/timer.h>
#include <fs/vfs.h>
#include <kernel/syscalls/fs.h>

/* 任务表（静态分配，支持最多 MAX_TASKS 个任务） */
static task_t task_table[MAX_TASKS];

/* 当前运行的任务 */
static task_t *current_task = NULL;

/* 就绪队列头尾指针 */
static task_t *ready_queue_head = NULL;
static task_t *ready_queue_tail = NULL;

/* 待回收（僵尸）任务链表 */
static task_t *zombie_list = NULL;

/* 下一个可用的 PID */
static uint32_t next_pid = 1;

/* 默认时间片（单位：timer ticks） */
#define DEFAULT_TIME_SLICE 10  // 假设 100Hz 定时器，10 ticks = 100ms

/**
 * 初始化标准文件描述符（stdin, stdout, stderr）
 * @param task 目标任务
 * @return 0 成功，-1 失败
 */
static int task_init_standard_fds(task_t *task) {
    if (!task || !task->fd_table) {
        LOG_ERROR_MSG("task_init_standard_fds: invalid task or fd_table\n");
        return -1;
    }
    
    /* 查找 /dev/console 设备 */
    fs_node_t *console = vfs_path_to_node("/dev/console");
    if (!console) {
        LOG_WARN_MSG("task: /dev/console not available, stdio not initialized for PID %u\n", task->pid);
        return -1;
    }
    
    /* 分配 fd 0 (stdin) - 只读 */
    int32_t fd = fd_table_alloc(task->fd_table, console, O_RDONLY);
    if (fd != 0) {
        LOG_WARN_MSG("task: failed to assign STDIN (fd=%d) for PID %u\n", fd, task->pid);
        return -1;
    }
    
    /* 分配 fd 1 (stdout) - 只写 */
    fd = fd_table_alloc(task->fd_table, console, O_WRONLY);
    if (fd != 1) {
        LOG_WARN_MSG("task: failed to assign STDOUT (fd=%d) for PID %u\n", fd, task->pid);
        return -1;
    }
    
    /* 分配 fd 2 (stderr) - 只写 */
    fd = fd_table_alloc(task->fd_table, console, O_WRONLY);
    if (fd != 2) {
        LOG_WARN_MSG("task: failed to assign STDERR (fd=%d) for PID %u\n", fd, task->pid);
        return -1;
    }
    
    LOG_DEBUG_MSG("task: initialized stdio for PID %u\n", task->pid);
    return 0;
}

/**
 * 将任务加入僵尸队列，待安全时释放
 */
static void task_enqueue_zombie(task_t *task) {
    if (!task) {
        return;
    }
    task->next = zombie_list;
    zombie_list = task;
}

/**
 * 释放所有已终止但尚未回收的任务
 * 仅在当前任务仍处于 RUNNING/READY 等状态时调用
 */
static void task_reap_zombies(void) {
    while (zombie_list) {
        task_t *task = zombie_list;
        zombie_list = zombie_list->next;
        task->next = NULL;
        LOG_DEBUG_MSG("Reaping terminated task %s (PID %u)\n",
                      task->name, task->pid);
        task_free(task);
    }
}

/**
 * 分配一个空闲的 PCB
 */
task_t *task_alloc(void) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_UNUSED) {
            memset(&task_table[i], 0, sizeof(task_t));
            task_table[i].pid = next_pid++;
            task_table[i].state = TASK_READY;
            
            /* 分配并初始化文件描述符表 */
            task_table[i].fd_table = kmalloc(sizeof(fd_table_t));
            if (task_table[i].fd_table == NULL) {
                LOG_ERROR_MSG("Failed to allocate fd_table for PID %u\n", task_table[i].pid);
                return NULL;
            }
            fd_table_init(task_table[i].fd_table);
            
            /* 初始化当前工作目录为根目录 */
            strcpy(task_table[i].cwd, "/");
            
            return &task_table[i];
        }
    }
    return NULL;  // 没有空闲 PCB
}

/**
 * 释放 PCB
 */
void task_free(task_t *task) {
    /* 释放文件描述符表 */
    if (task->fd_table != NULL) {
        /* 关闭所有打开的文件描述符 */
        for (int i = 0; i < MAX_FDS; i++) {
            if (task->fd_table->entries[i].in_use) {
                fd_table_free(task->fd_table, i);
            }
        }
        /* 释放文件描述符表内存 */
        kfree(task->fd_table);
        task->fd_table = NULL;
    }
    
    /* 释放内核栈 */
    if (task->kernel_stack_base != 0) {
        kfree((void *)task->kernel_stack_base);
    }
    
    /* 释放用户进程的地址空间 */
    if (task->is_user_process && task->page_dir_phys != 0) {
        /* 释放页目录、用户空间页表和所有物理页 */
        /* 注意：vmm_free_page_directory 现在会释放所有物理页，
         * 包括用户栈，所以这里不需要手动释放 */
        vmm_free_page_directory(task->page_dir_phys);
    }
    
    /* 标记为未使用 */
    task->state = TASK_UNUSED;
}

/**
 * 将任务添加到就绪队列
 */
void ready_queue_add(task_t *task) {
    task->next = NULL;
    task->state = TASK_READY;
    
    if (ready_queue_tail == NULL) {
        ready_queue_head = ready_queue_tail = task;
    } else {
        ready_queue_tail->next = task;
        ready_queue_tail = task;
    }
}

/**
 * 从就绪队列移除并返回第一个任务
 */
static task_t *ready_queue_remove(void) {
    if (ready_queue_head == NULL) {
        return NULL;
    }
    
    task_t *task = ready_queue_head;
    ready_queue_head = task->next;
    
    if (ready_queue_head == NULL) {
        ready_queue_tail = NULL;
    }
    
    task->next = NULL;
    return task;
}

/**
 * 初始化空闲任务（idle task）
 * 当没有其他任务运行时，运行此任务
 */
static void idle_task_entry(void) {
    LOG_INFO_MSG("Idle task started (PID 0)\n");
    
    while (1) {
        __asm__ volatile("hlt");  // 等待中断
    }
}

/**
 * 初始化任务管理系统
 */
void task_init(void) {
    LOG_INFO_MSG("Initializing task management...\n");
    
    /* 清空任务表 */
    memset(task_table, 0, sizeof(task_table));
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_table[i].state = TASK_UNUSED;
    }
    
    /* 创建空闲任务（PID 0） */
    next_pid = 0;  // 从 0 开始，第一个任务是 idle
    task_t *idle_task = task_alloc();
    if (idle_task == NULL) {
        PANIC("Failed to create idle task");
    }
    
    strcpy(idle_task->name, "idle");
    idle_task->priority = 0;  // 最低优先级
    
    /* idle 任务使用内核页目录 */
    idle_task->page_dir = NULL;
    idle_task->page_dir_phys = vmm_get_page_directory();
    
    /* 分配内核栈 */
    idle_task->kernel_stack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (idle_task->kernel_stack_base == 0) {
        PANIC("Failed to allocate kernel stack for idle task");
    }
    idle_task->kernel_stack = idle_task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    /* 初始化上下文（准备首次运行） */
    idle_task->context.eip = (uint32_t)idle_task_entry;
    idle_task->context.esp = idle_task->kernel_stack;
    idle_task->context.ebp = idle_task->kernel_stack;
    idle_task->context.eflags = 0x202;  // IF (Interrupt Enable) = 1
    idle_task->context.cs = GDT_KERNEL_CODE_SEGMENT;
    idle_task->context.ds = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.es = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.fs = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.gs = GDT_KERNEL_DATA_SEGMENT;
    idle_task->context.ss = GDT_KERNEL_DATA_SEGMENT;
    
    /* 使用内核页目录的物理地址 */
    idle_task->context.cr3 = idle_task->page_dir_phys;
    
    /* 设置为当前任务 */
    idle_task->state = TASK_RUNNING;
    idle_task->last_scheduled_tick = timer_get_ticks();  // 记录初始调度时间
    current_task = idle_task;
    
    LOG_INFO_MSG("Task management initialized\n");
    LOG_DEBUG_MSG("  Idle task created (PID %u)\n", idle_task->pid);
    LOG_DEBUG_MSG("  Max tasks: %u\n", MAX_TASKS);
    LOG_DEBUG_MSG("  Default time slice: %u ticks\n", DEFAULT_TIME_SLICE);
}

/**
 * 内核线程退出包装器
 * 当内核线程函数返回时，调用此函数清理资源
 */
 static void kernel_thread_exit_wrapper(void) {
    /* 线程函数已返回，正常退出 */
    task_exit(0);
    
    /* 永远不会到达这里 */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/**
 * 创建内核线程
 */
uint32_t task_create_kernel_thread(void (*entry)(void), const char *name) {
    /* 分配 PCB */
    task_t *task = task_alloc();
    if (task == NULL) {
        LOG_ERROR_MSG("Failed to allocate PCB for %s\n", name);
        return 0;
    }
    
    /* 设置任务名称 */
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    
    /* 内核线程共享内核页目录 */
    task->page_dir = NULL;
    task->page_dir_phys = vmm_get_page_directory();
    
    /* 分配内核栈 */
    task->kernel_stack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (task->kernel_stack_base == 0) {
        LOG_ERROR_MSG("Failed to allocate kernel stack for %s\n", name);
        task_free(task);
        return 0;
    }
    task->kernel_stack = task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    /* 设置栈帧：当 entry 返回时，会返回到 kernel_thread_exit_wrapper 
     * 注意：task_asm.asm 的恢复流程会将 EIP 写入栈顶，然后 ret
     * 所以栈顶需要是占位符（会被替换为 EIP），下一个位置是返回地址
     */
     uint32_t *stack_ptr = (uint32_t *)(task->kernel_stack);
     *(--stack_ptr) = (uint32_t)kernel_thread_exit_wrapper;  // 返回地址（entry 返回时会跳转到这里）
     *(--stack_ptr) = 0;  // 占位符，会被 task_switch 替换为 EIP
     
     /* 初始化上下文 */
     task->context.eip = (uint32_t)entry;  // 首次运行时的入口点
     task->context.esp = (uint32_t)stack_ptr;  // 栈顶是占位符 0
     task->context.ebp = task->kernel_stack;
     task->context.eflags = 0x202;  // IF = 1
     task->context.cs = GDT_KERNEL_CODE_SEGMENT;
     task->context.ds = GDT_KERNEL_DATA_SEGMENT;
     task->context.es = GDT_KERNEL_DATA_SEGMENT;
     task->context.fs = GDT_KERNEL_DATA_SEGMENT;
     task->context.gs = GDT_KERNEL_DATA_SEGMENT;
     task->context.ss = GDT_KERNEL_DATA_SEGMENT;
    
    /* 使用内核页目录的物理地址（内核线程共享） */
    task->context.cr3 = task->page_dir_phys;
    
    /* 设置优先级和时间片 */
    task->priority = 128;  // 默认优先级
    task->time_slice = DEFAULT_TIME_SLICE;
    
    /* 添加到就绪队列 */
    ready_queue_add(task);
    
    LOG_DEBUG_MSG("Created kernel thread: %s (PID %u)\n", name, task->pid);
    
    return task->pid;
}

/**
 * 创建用户进程
 * 
 * 注意：当前 vmm 模块仅支持单页目录，因此用户进程也使用内核页目录
 * 这是一个简化的实现，未来需要扩展 vmm 模块以支持多页目录
 * 
 * @param page_dir 保留参数，当前被忽略（为了接口兼容性）
 */
uint32_t task_create_user_process(const char *name, uint32_t entry_point, 
                                   page_directory_t *page_dir __attribute__((unused))) {
    /* 分配 PCB */
    task_t *task = task_alloc();
    if (task == NULL) {
        LOG_ERROR_MSG("Failed to allocate PCB for user process %s\n", name);
        return 0;
    }
    
    /* 设置任务名称 */
    strncpy(task->name, name, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    
    /* 标记为用户进程 */
    task->is_user_process = true;
    
    /* 创建独立的页目录 */
    if (page_dir != NULL) {
        /* 如果提供了页目录，使用它（主要用于 exec） */
        task->page_dir = page_dir;
        task->page_dir_phys = VIRT_TO_PHYS((uint32_t)page_dir);
    } else {
        /* 创建新的页目录 */
        task->page_dir_phys = vmm_create_page_directory();
        if (task->page_dir_phys == 0) {
            LOG_ERROR_MSG("Failed to create page directory for %s\n", name);
            task_free(task);
            return 0;
        }
        task->page_dir = (page_directory_t*)PHYS_TO_VIRT(task->page_dir_phys);
    }
    
    /* 分配内核栈（用于处理系统调用和中断） */
    task->kernel_stack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (task->kernel_stack_base == 0) {
        LOG_ERROR_MSG("Failed to allocate kernel stack for %s\n", name);
        task_free(task);
        return 0;
    }
    task->kernel_stack = task->kernel_stack_base + KERNEL_STACK_SIZE;
    
    /* 分配用户栈（从 PMM 分配物理页，映射到进程的独立地址空间） */
    /* 用户栈位于用户空间顶部 */
    uint32_t user_stack_virt = USER_SPACE_END - USER_STACK_SIZE;
    uint32_t num_pages = USER_STACK_SIZE / PAGE_SIZE;
    
    /* 分配第一个物理页帧并保存基址 */
    task->user_stack_base = pmm_alloc_frame();
    if (task->user_stack_base == 0) {
        LOG_ERROR_MSG("Failed to allocate user stack for %s\n", name);
        kfree((void *)task->kernel_stack_base);
        task_free(task);
        return 0;
    }
    
    /* 映射第一页到进程的页目录 */
    if (!vmm_map_page_in_directory(task->page_dir_phys, user_stack_virt, 
                                    task->user_stack_base,
                                    PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
        LOG_ERROR_MSG("Failed to map user stack page at %x\n", user_stack_virt);
        pmm_free_frame(task->user_stack_base);
        kfree((void *)task->kernel_stack_base);
        task_free(task);
        return 0;
    }
    
    /* 分配并映射剩余页 */
    for (uint32_t i = 1; i < num_pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == 0) {
            LOG_ERROR_MSG("Failed to allocate user stack page %u for %s\n", i, name);
            /* 清理已分配的页 */
            for (uint32_t j = 0; j < i; j++) {
                if (j == 0) {
                    pmm_free_frame(task->user_stack_base);
                } else {
                    pmm_free_frame(task->user_stack_base + j * PAGE_SIZE);
                }
            }
            kfree((void *)task->kernel_stack_base);
            task_free(task);
            return 0;
        }
        
        if (!vmm_map_page_in_directory(task->page_dir_phys, user_stack_virt + i * PAGE_SIZE, 
                                        frame, PAGE_PRESENT | PAGE_WRITE | PAGE_USER)) {
            LOG_ERROR_MSG("Failed to map user stack page at %x\n", user_stack_virt + i * PAGE_SIZE);
            pmm_free_frame(frame);
            /* 清理已分配的页 */
            for (uint32_t j = 0; j < i; j++) {
                if (j == 0) {
                    pmm_free_frame(task->user_stack_base);
                } else {
                    pmm_free_frame(task->user_stack_base + j * PAGE_SIZE);
                }
            }
            kfree((void *)task->kernel_stack_base);
            task_free(task);
            return 0;
        }
    }
    
    /* 保存用户程序的真实入口点和栈 */
    task->user_entry = entry_point;
    task->user_stack = USER_SPACE_END - 4;  // 用户栈顶（虚拟地址）
    
    /* 初始化上下文为内核模式（首次运行时会调用 usermode_wrapper） */
    task->context.eip = get_usermode_wrapper();
    task->context.esp = task->kernel_stack;
    task->context.ebp = task->kernel_stack;
    task->context.eflags = 0x202;  // IF = 1
    
    /* 设置段寄存器为内核段 */
    task->context.cs = GDT_KERNEL_CODE_SEGMENT;
    task->context.ds = GDT_KERNEL_DATA_SEGMENT;
    task->context.es = GDT_KERNEL_DATA_SEGMENT;
    task->context.fs = GDT_KERNEL_DATA_SEGMENT;
    task->context.gs = GDT_KERNEL_DATA_SEGMENT;
    task->context.ss = GDT_KERNEL_DATA_SEGMENT;
    
    /* 使用进程自己的页目录物理地址 */
    task->context.cr3 = task->page_dir_phys;
    
    /* 设置优先级和时间片 */
    task->priority = 128;
    task->time_slice = DEFAULT_TIME_SLICE;
    
    /* 初始化标准文件描述符（用户进程需要 stdio） */
    task_init_standard_fds(task);
    
    /* 添加到就绪队列 */
    ready_queue_add(task);
    
    LOG_DEBUG_MSG("Created user process: %s (PID %u, entry=%x)\n", 
                 name, task->pid, entry_point);
    LOG_DEBUG_MSG("  User stack: %x -> %x\n", 
                 task->user_stack_base, user_stack_virt);
    LOG_DEBUG_MSG("  Kernel stack: %x\n", task->kernel_stack_base);
    
    return task->pid;
}

/**
 * 获取当前任务
 */
task_t *task_get_current(void) {
    return current_task;
}

/**
 * 根据 PID 获取任务
 */
task_t *task_get_by_pid(uint32_t pid) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state != TASK_UNUSED && task_table[i].pid == pid) {
            return &task_table[i];
        }
    }
    return NULL;
}

/**
 * 调度器（选择下一个任务）
 * 
 * 注意：此函数会禁用中断以保护关键区域
 * 如果调用者已经禁用了中断，此函数仍会正常工作
 */
void task_schedule(void) {
    /* 禁用中断以保护调度器的关键区域 */
    bool prev_state = interrupts_disable();
    
    if (current_task == NULL) {
        interrupts_restore(prev_state);
        return;  // 还未初始化
    }

    /* 仅在当前任务不是终止状态时，安全地回收僵尸任务 */
    if (current_task->state != TASK_TERMINATED) {
        task_reap_zombies();
    }

    /* 更新当前任务的运行时间统计（切换时统计增量） */
    uint64_t current_tick = timer_get_ticks();
    if (current_task->last_scheduled_tick > 0) {
        uint64_t elapsed_ticks = current_tick - current_task->last_scheduled_tick;
        /* 累加从上次更新到现在的增量时间
         * 如果elapsed为0，说明任务运行时间小于1个tick，但确实运行过
         * 为了统计这类频繁yield的任务，至少计数1个tick */
        if (elapsed_ticks == 0) {
            /* 检查任务是否真的运行过（不是刚经历过timer_tick的情况）
             * 由于timer_tick会重置时间戳，这里elapsed=0说明任务刚被调度
             * 就被切换，确实应该计数 */
            elapsed_ticks = 1;
        }
        current_task->total_runtime += elapsed_ticks;
    }
    /* 注意：不重置 last_scheduled_tick，由新任务设置自己的起始时间 */
    
    /* 如果当前任务仍然可运行，放回就绪队列 */
    if (current_task->state == TASK_RUNNING) {
        current_task->time_slice = DEFAULT_TIME_SLICE;  // 重置时间片
        ready_queue_add(current_task);
    } else if (current_task->state == TASK_TERMINATED) {
        /* 不立即释放，加入僵尸链表，等待下一次安全回收 */
        LOG_DEBUG_MSG("Marking terminated task %s (PID %u) for delayed cleanup\n", 
                     current_task->name, current_task->pid);
        task_enqueue_zombie(current_task);
    }
    
    /* 从就绪队列获取下一个任务（跳过已终止的任务） */
    task_t *next_task = NULL;
    while (1) {
        next_task = ready_queue_remove();
        if (next_task == NULL) {
            break;  // 队列为空
        }
        if (next_task->state != TASK_TERMINATED) {
            break;  // 找到有效任务
        }
        // 跳过已终止的任务，将其加入僵尸队列
        task_enqueue_zombie(next_task);
    }
    
    /* 如果没有就绪任务，运行 idle */
    if (next_task == NULL) {
        next_task = &task_table[0];  // idle task (PID 0)
    }
    
    /* 如果下一个任务就是当前任务，直接返回 */
    if (next_task == current_task) {
        next_task->state = TASK_RUNNING;
        next_task->last_scheduled_tick = current_tick;  // 更新时间戳
        interrupts_restore(prev_state);
        return;
    }
    
    /* 保存当前任务指针 */
    task_t *prev_task = current_task;
    
    /* 切换当前任务 */
    current_task = next_task;
    next_task->state = TASK_RUNNING;
    
    /* 更新 TSS 内核栈（用于特权级切换） */
    tss_set_kernel_stack(next_task->kernel_stack);
    next_task->last_scheduled_tick = current_tick;  // 记录新任务开始运行的时间
    
    /* 切换页目录（如果不同）*/
    if (prev_task->page_dir_phys != next_task->page_dir_phys) {
        vmm_switch_page_directory(next_task->page_dir_phys);
    }
    
    /* 注意：在调用 task_switch 之前不恢复中断
     * task_switch 会切换到新任务，新任务会从自己的上下文恢复 EFLAGS
     * 如果新任务需要中断，它的 EFLAGS 中 IF 位会被设置
     * 如果当前在中断上下文中，prev_state 为 false，新任务也不会启用中断
     */
    
    /* 执行上下文切换（汇编实现）
     * 注意：task_switch 不会返回（直接跳转到新任务）
     * 如果由于某种原因返回了，恢复中断状态
     */
    task_switch(prev_task, next_task);
    
    /* 如果 task_switch 返回（不应该发生），恢复中断状态 */
    interrupts_restore(prev_state);
}

/**
 * 主动让出 CPU
 * 
 * 注意：task_schedule() 内部已经禁用中断保护
 * 此函数不需要额外禁用中断，但如果调用者需要确保原子性，可以自己禁用
 */
void task_yield(void) {
    /* task_schedule() 内部会禁用中断保护关键区域
     * 如果 task_schedule() 执行了任务切换，不会返回
     * 如果没有切换（同一个任务），会恢复中断状态
     */
    task_schedule();
}

/**
 * 退出当前任务
 */
void task_exit(uint32_t exit_code) {
    bool prev_state = interrupts_disable();
    
    current_task->state = TASK_TERMINATED;
    current_task->exit_code = exit_code;
    
    LOG_DEBUG_MSG("Task %s (PID %u) exited with code %u\n", 
                 current_task->name, current_task->pid, exit_code);
    
    /* 调度到下一个任务 */
    task_schedule();
    
    /* 永远不会执行到这里 */
    interrupts_restore(prev_state);
    while (1) {
        __asm__ volatile("hlt");
    }
}

/**
 * 睡眠（简单实现：忙等待）
 */
void task_sleep(uint32_t ms) {
    uint64_t target = timer_get_uptime_ms() + ms;
    while (timer_get_uptime_ms() < target) {
        task_yield();
    }
}

/**
 * 阻塞当前任务
 */
void task_block(void *channel) {
    bool prev_state = interrupts_disable();
    
    current_task->state = TASK_BLOCKED;
    current_task->wait_channel = channel;
    
    task_schedule();
    
    interrupts_restore(prev_state);
}

/**
 * 唤醒等待特定通道的所有任务
 */
void task_wakeup(void *channel) {
    bool prev_state = interrupts_disable();
    
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_BLOCKED && 
            task_table[i].wait_channel == channel) {
            task_table[i].wait_channel = NULL;
            ready_queue_add(&task_table[i]);
        }
    }
    
    interrupts_restore(prev_state);
}

/**
 * 定时器回调（由 PIT 中断调用）
 */
void task_timer_tick(void) {
    if (current_task == NULL) {
        return;
    }
    
    /* 运行时间统计全部在 task_schedule() 中进行
     * 这里只处理时间片管理 */
    
    /* 减少时间片 */
    if (current_task->time_slice > 0) {
        current_task->time_slice--;
    }
    
    /* 时间片用完，触发调度 */
    if (current_task->time_slice == 0) {
        task_schedule();
    }
}
