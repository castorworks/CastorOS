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

```cpp
typedef struct task {
    // 标识信息
    uint32_t pid;                // 进程 ID（init 固定是 1，PID 不复用）
    char name[32];               // 进程名（取自 argv[0]）
    task_state_t state;          // 状态

    // 调度信息
    uint32_t priority;
    uint32_t time_slice;
    uint64_t sleep_until_ms;     // 睡眠截止时间
    void *wait_object;           // 阻塞时等待的对象

    // CPU 上下文和内核栈
    cpu_context_t context;
    uintptr_t kernel_stack;

    // 地址空间
    uintptr_t page_dir_phys;
    uintptr_t heap_start, heap_end, heap_max;   // brk
    uintptr_t user_stack;

    // 进程关系和退出信息
    struct task *parent;
    uint32_t exit_code;
    bool kill_pending;           // 有待处理的 kill

    // 进程间通信
    ipc_state_t ipc_state;       // 是否阻塞在 send/recv 上
    uint32_t ipc_peer;
    ipc_msg ipc_buf;             // 在途的消息
    uint32_t irq_pending;        // 还没被 recv 取走的设备中断

    bool privileged;             // 可以访问硬件
} task_t;
```

PCB 里没有文件描述符表和工作目录：内核不认识文件，这些概念属于用户态的文件服务。

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

```cpp
uint32_t syscall::Process::fork(uintptr_t *frame) {
    task_t *parent = Scheduler::get_current();

    // 1. 分配子进程 PCB
    task_t *child = Scheduler::alloc();

    // 2. 克隆地址空间：普通页变成只读 + COW，共享页（设备内存、进程间共享内存）原样共享
    child->page_dir_phys = mm::Vmm::clone_page_directory(parent->page_dir_phys);

    // 3. 分配内核栈
    child->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);

    // 4. 用系统调用入口保存的用户寄存器（frame）构造子进程的上下文
    //    子进程从 fork() 返回处继续执行，返回值是 0
    child->context.eax = 0;
    child->context.eip = frame[8];
    ...

    // 5. 继承父子关系和特权
    child->parent = parent;
    child->privileged = parent->privileged;

    // 6. 加入就绪队列
    child->state = TASK_READY;
    Scheduler::ready_queue_add(child);

    return child->pid;  // 父进程返回子进程 PID
}
```

### exec()

内核不认识路径。调用者自己把程序读进内存（比如通过文件服务），把 ELF 映像和参数交给内核：

```cpp
uint32_t syscall::Process::exec(uintptr_t *frame, const void *image, size_t size,
                                const char *args, size_t args_size) {
    // 1. 把映像和参数块复制进内核：接下来要换地址空间，旧的就看不到了
    void *elf_data = kmalloc(size);
    memcpy(elf_data, image, size);

    // 2. 校验 ELF，在新的地址空间里加载各个段、建立用户栈
    //    （任何一步失败都还能回到原来的程序）
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    kernel::Elf::load(elf_data, size, new_dir, &entry_point, &program_end);
    Scheduler::setup_user_stack(current);

    // 3. 切到新地址空间，释放旧的
    mm::Vmm::switch_page_directory(new_dir);
    mm::Vmm::free_page_directory(old_dir);

    // 4. 把参数写进参数页（用户栈区域最顶上的一页，地址固定）
    memcpy(((user_args_t *)USER_ARGS_ADDR)->data, kargs, args_size);

    // 5. 改写系统调用的返回帧：这次系统调用“返回”到新程序的入口
    frame[8] = entry_point;           // EIP
    frame[11] = current->user_stack;  // ESP
    return 0;
}
```

新程序的启动代码（`user/lib/src/crt0.cpp`）从参数页取出参数，组装成 `argv` 再调用 `main`。

## 进程终止

### exit()

```cpp
void Scheduler::exit_current(uint32_t exit_code, bool signaled, uint32_t signal) {
    // 1. 等着和本任务通信的任务不会再等到结果；释放认领的中断线
    Ipc::on_exit(current_task);
    UserIrq::on_exit(current_task);

    // 2. 子进程：已经是僵尸的直接回收，还在运行的变成孤儿
    ...

    // 3. 有父进程就变成僵尸等它来收，并唤醒可能正在 waitpid 的父进程
    if (current_task->parent) {
        current_task->state = TASK_ZOMBIE;
        Scheduler::wakeup(current_task->parent);
    } else {
        current_task->state = TASK_TERMINATED;
    }

    // 4. 切走，不再回来。地址空间和内核栈不能在自己的栈上释放，
    //    由父进程的 waitpid 或下一次调度来回收
    Scheduler::schedule();
}
```

### wait()

```cpp
uint32_t syscall::Process::waitpid(int32_t pid, uint32_t *wstatus, uint32_t options) {
    while (true) {
        // 找一个已经退出（僵尸）的匹配子进程
        task_t *child = find_zombie_child(current, pid);
        if (child) {
            *wstatus = encode_status(child);
            Scheduler::free(child);         // 回收它的地址空间、内核栈和 PCB
            return child_pid;
        }
        if (no_matching_child)   return -1;
        if (options & WNOHANG)   return 0;
        if (current->kill_pending) return -1;

        // 阻塞到有子进程退出：exit_current 会唤醒我们
        Scheduler::block(current);
    }
}
```

等待必须是真正的阻塞。如果父进程在内核里轮询（让出 CPU 再停机等下一次中断），它停机的
那段时间里别的任务都得不到运行，系统里每一次唤醒都可能被拖到下一个时钟滴答。

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

## 终止别的进程：kill

CastorOS 没有信号处理函数。`kill(pid, sig)` 只有两种用法：`sig == 0` 探测进程是否存在，
其他值终止目标进程。

终止不是由调用者就地完成的。目标进程一定停在内核里的某个点上，可能正持有锁、
正排在某个等待队列里；直接改它的状态或释放它的资源会破坏内核。所以 `kill` 只给目标
记一个待处理的标记，由目标自己在安全的地方退出：

```cpp
bool Scheduler::request_kill(task_t *target, uint32_t signal) {
    target->kill_pending = true;
    target->kill_signal = signal;

    // 正在 sleep、等待 IPC 或等待子进程的任务提前唤醒，让它尽快走到系统调用出口
    if (target->state == TASK_BLOCKED && (sleeping || waiting_for_ipc || waiting_for_child)) {
        target->state = TASK_READY;
        ready_queue_add(target);
    }
    return true;
}

// 系统调用返回用户态之前、以及时钟中断即将返回用户态时调用
void Scheduler::deliver_pending_kill() {
    if (current_task->kill_pending) {
        exit_current(128 + signal, true, signal);
    }
}
```

非特权进程只能终止自己和自己的子孙。

## 最佳实践

1. **内核栈大小**：通常 4-16KB，需要足够处理嵌套中断
2. **PID 分配**：使用位图或循环计数器
3. **就绪队列**：考虑优先级队列或多级反馈队列
4. **COW 优化**：fork() 后立即 exec() 的场景很常见
5. **资源清理**：exit() 时确保释放所有资源

