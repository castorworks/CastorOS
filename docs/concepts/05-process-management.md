# 进程管理

## 概述

CastorOS 实现了抢占式多任务，支持用户态和内核态任务。每个任务有独立的地址空间和内核栈。

## 任务状态

```
                    创建
                      ↓
    +------------→ READY ←-----------+
    |                ↓               |
    |            RUNNING             |
    |           ↙       ↘           |
    |      时间片         阻塞等待    |
    |      用完          I/O/锁/信号  |
    |        ↓              ↓        |
    +--- READY          BLOCKED ----+
                           ↓
                  等待条件满足
                       ↓
                    READY
                       
    RUNNING → exit() → ZOMBIE → wait() → TERMINATED
```

```c
typedef enum {
    TASK_UNUSED,      // 未使用的任务槽
    TASK_READY,       // 就绪，等待调度
    TASK_RUNNING,     // 正在运行
    TASK_BLOCKED,     // 阻塞，等待事件
    TASK_ZOMBIE,      // 已退出，等待父进程回收
    TASK_TERMINATED   // 已终止，可回收
} task_state_t;
```

## 任务控制块 (PCB)

```c
typedef struct task {
    // 标识信息
    uint32_t pid;              // 进程 ID
    char name[32];             // 进程名
    task_state_t state;        // 状态
    
    // 调度信息
    uint32_t priority;         // 优先级
    uint32_t time_slice;       // 剩余时间片
    uint64_t total_ticks;      // 总运行滴答数
    
    // 内存信息
    uint32_t page_dir_phys;    // 页目录物理地址
    page_directory_t *page_dir;// 页目录虚拟地址
    uint32_t brk;              // 堆顶地址
    
    // 栈信息
    uint32_t kernel_stack;     // 内核栈顶
    uint32_t user_stack;       // 用户栈顶
    
    // 上下文信息
    cpu_context_t context;     // 保存的 CPU 状态
    
    // 文件信息
    fd_entry_t *fd_table;      // 文件描述符表
    int fd_count;              // 打开文件数
    
    // 进程关系
    struct task *parent;       // 父进程
    struct task *children;     // 子进程链表
    struct task *sibling;      // 兄弟进程
    
    // 退出信息
    int exit_code;             // 退出码
    
    // 链表指针
    struct task *next;         // 就绪队列链接
} task_t;
```

## CPU 上下文

```c
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp;
    uint32_t eip;              // 指令指针
    uint32_t esp;              // 栈指针
    uint32_t eflags;           // 标志寄存器
    uint32_t cr3;              // 页目录基址
} cpu_context_t;
```

## 调度器

### 调度算法

CastorOS 使用简单的时间片轮转调度：

```c
static task_t *ready_queue_head = NULL;
static task_t *current_task = NULL;

void schedule(void) {
    if (!current_task) return;
    
    // 保存当前任务状态
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
        enqueue_ready(current_task);
    }
    
    // 选择下一个任务
    task_t *next = dequeue_ready();
    if (!next) {
        next = idle_task;  // 无就绪任务时运行空闲任务
    }
    
    // 切换任务
    if (next != current_task) {
        task_t *prev = current_task;
        current_task = next;
        current_task->state = TASK_RUNNING;
        
        context_switch(prev, next);
    }
}
```

### 上下文切换

```asm
; void context_switch(task_t *prev, task_t *next)
context_switch:
    ; 保存调用者保存的寄存器
    push ebp
    push ebx
    push esi
    push edi
    
    ; 保存当前栈指针到 prev->context.esp
    mov eax, [esp + 20]     ; prev
    mov [eax + CONTEXT_ESP], esp
    
    ; 切换到 next 的栈
    mov eax, [esp + 24]     ; next
    mov esp, [eax + CONTEXT_ESP]
    
    ; 切换页目录
    mov ebx, [eax + CONTEXT_CR3]
    mov cr3, ebx
    
    ; 恢复寄存器
    pop edi
    pop esi
    pop ebx
    pop ebp
    
    ret
```

## 进程创建

### fork()

```c
int sys_fork(void) {
    task_t *parent = current_task;
    
    // 1. 分配子进程 PCB
    task_t *child = task_alloc();
    if (!child) return -ENOMEM;
    
    // 2. 复制页目录 (COW)
    child->page_dir_phys = vmm_clone_page_directory(parent->page_dir_phys);
    child->page_dir = PHYS_TO_VIRT(child->page_dir_phys);
    
    // 3. 分配内核栈
    child->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    
    // 4. 复制上下文
    memcpy(&child->context, &parent->context, sizeof(cpu_context_t));
    child->context.eax = 0;  // 子进程 fork() 返回 0
    
    // 5. 复制文件描述符表
    child->fd_table = fd_table_clone(parent->fd_table);
    
    // 6. 设置进程关系
    child->parent = parent;
    child->sibling = parent->children;
    parent->children = child;
    
    // 7. 加入就绪队列
    child->state = TASK_READY;
    enqueue_ready(child);
    
    return child->pid;  // 父进程返回子进程 PID
}
```

### exec()

```c
int sys_execve(const char *path, char *const argv[], char *const envp[]) {
    task_t *task = current_task;
    
    // 1. 打开可执行文件
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return fd;
    
    // 2. 读取 ELF 头
    elf_header_t header;
    vfs_read(fd, &header, sizeof(header));
    
    // 3. 验证 ELF
    if (!elf_validate(&header)) {
        vfs_close(fd);
        return -ENOEXEC;
    }
    
    // 4. 清除旧地址空间
    vmm_destroy_user_space(task->page_dir);
    
    // 5. 加载程序段
    for (每个程序头) {
        load_segment(fd, phdr, task->page_dir);
    }
    
    // 6. 设置用户栈
    uint32_t user_stack = setup_user_stack(argv, envp);
    
    // 7. 设置入口点
    task->context.eip = header.e_entry;
    task->context.esp = user_stack;
    
    vfs_close(fd);
    return 0;
}
```

## 进程终止

### exit()

```c
void sys_exit(int status) {
    task_t *task = current_task;
    
    // 1. 关闭所有文件
    for (int i = 0; i < task->fd_count; i++) {
        if (task->fd_table[i].valid) {
            vfs_close(i);
        }
    }
    
    // 2. 释放用户地址空间
    vmm_destroy_user_space(task->page_dir);
    
    // 3. 设置退出状态
    task->exit_code = status;
    task->state = TASK_ZOMBIE;
    
    // 4. 重新设置子进程的父进程
    task_t *child = task->children;
    while (child) {
        child->parent = init_task;  // 孤儿进程由 init 收养
        child = child->sibling;
    }
    
    // 5. 通知父进程
    if (task->parent) {
        wake_up(task->parent);
    }
    
    // 6. 触发调度
    schedule();
    
    // 不会到达这里
}
```

### wait()

```c
int sys_waitpid(int pid, int *status, int options) {
    task_t *parent = current_task;
    
    while (1) {
        // 查找匹配的子进程
        task_t *child = find_child(parent, pid);
        
        if (child && child->state == TASK_ZOMBIE) {
            // 获取退出状态
            if (status) *status = child->exit_code;
            
            // 回收资源
            int child_pid = child->pid;
            task_free(child);
            
            return child_pid;
        }
        
        if (options & WNOHANG) {
            return 0;  // 非阻塞模式
        }
        
        // 阻塞等待
        parent->state = TASK_BLOCKED;
        schedule();
    }
}
```

## 用户态切换

### 从内核态进入用户态

```c
void switch_to_user_mode(uint32_t entry, uint32_t user_stack) {
    // 设置用户态段选择子
    uint32_t user_cs = 0x1B;  // 用户代码段 | RPL=3
    uint32_t user_ds = 0x23;  // 用户数据段 | RPL=3
    
    __asm__ volatile (
        // 设置数据段
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        
        // 构造 iret 帧
        "push %0\n"     // SS
        "push %1\n"     // ESP
        "pushf\n"       // EFLAGS
        "orl $0x200, (%%esp)\n"  // 启用中断
        "push %2\n"     // CS
        "push %3\n"     // EIP
        "iret\n"
        :
        : "r"(user_ds), "r"(user_stack), "r"(user_cs), "r"(entry)
    );
}
```

### TSS (Task State Segment)

TSS 用于在特权级切换时提供内核栈：

```c
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;      // 内核栈指针
    uint32_t ss0;       // 内核栈段
    // ... 其他字段
} tss_t;

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

// 每次切换任务时更新 TSS
void context_switch(task_t *prev, task_t *next) {
    tss_set_kernel_stack(next->kernel_stack + KERNEL_STACK_SIZE);
    // ...
}
```

## 信号处理

### 信号发送

```c
int sys_kill(int pid, int sig) {
    task_t *target = find_task_by_pid(pid);
    if (!target) return -ESRCH;
    
    // 添加待处理信号
    target->pending_signals |= (1 << sig);
    
    // 如果目标阻塞，唤醒它
    if (target->state == TASK_BLOCKED) {
        target->state = TASK_READY;
        enqueue_ready(target);
    }
    
    return 0;
}
```

### 信号处理（返回用户态前）

```c
void handle_pending_signals(task_t *task) {
    while (task->pending_signals) {
        int sig = find_first_signal(task->pending_signals);
        task->pending_signals &= ~(1 << sig);
        
        if (task->signal_handlers[sig]) {
            // 调用用户定义的处理程序
            call_signal_handler(task, sig);
        } else {
            // 默认处理
            default_signal_action(task, sig);
        }
    }
}
```

## 最佳实践

1. **内核栈大小**：通常 4-16KB，需要足够处理嵌套中断
2. **PID 分配**：使用位图或循环计数器
3. **就绪队列**：考虑优先级队列或多级反馈队列
4. **COW 优化**：fork() 后立即 exec() 的场景很常见
5. **资源清理**：exit() 时确保释放所有资源

