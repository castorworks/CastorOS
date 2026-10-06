# 进程管理

CastorOS 实现了抢占式多任务，支持用户态和内核态任务。每个任务有独立的地址空间和内核栈。

代码在 `src/kernel/` 的两个文件里，一起实现 `kernel::Scheduler`：`task.cpp` 是任务表和进程的一生（PCB 的分配和释放、创建第一个进程、退出、kill），`sched.cpp` 是调度（就绪队列、idle、`schedule()`、时钟滴答、让出/睡眠/阻塞/唤醒）。`fork`、`exec`、`waitpid` 在 `src/kernel/syscalls/process.cpp`。

## 任务状态

```text
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

任务不在 CPU 上运行的时候，它的寄存器内容存在 PCB 里的 `cpu_context_t` 中。这个结构每个架构都不一样（寄存器不同），所以定义不在通用的 `kernel/task.h` 里，而是每个架构各有一份 `src/arch/<arch>/include/task_context.h`。在 i686 上它是通用寄存器、段寄存器、指令指针、标志寄存器、栈指针，再加上 `cr3`——这个任务的地址空间。

通用的调度和进程代码不直接读写里面的字段。需要构造或改写现场的地方只有几处，都通过 `hal::UserContext`（`src/arch/<arch>/task/user_context.cpp`）：

| 函数 | 什么时候用 |
|------|------------|
| `init(ctx, entry, user_sp, space, kernel_sp)` | 新进程、`exec` 之后：从入口开始在用户态运行 |
| `init_kernel(ctx, entry, kernel_sp, space)` | idle 任务：在内核态运行一个函数 |
| `fork(child, frame, space, kernel_sp)` | 子进程：和父进程进入系统调用时一样，只是返回值是 0 |
| `exec_return(frame, entry, user_sp)` | 改写系统调用的返回帧，让它“返回”到新程序 |
| `set_kernel_stack(kernel_sp)` | 告诉 CPU 这个任务从用户态陷入内核时用哪个内核栈 |
| `fp_reset(state)` / `fp_save(state)` / `fp_restore(state)` | 浮点/SIMD 寄存器：新程序的初始状态、存进 PCB、从 PCB 装回 |

其中 `frame` 是系统调用入口保存在内核栈上的用户寄存器，它的布局由各架构的汇编入口决定。

浮点/SIMD 寄存器不在 `cpu_context_t` 里，而是 PCB 里单独的一块 `fp_state`（x86 上是 `FXSAVE` 的 512 字节，arm64 上是 V0-V31 加两个状态寄存器）。分开放是因为它们的保存时机不一样：内核自己不用这些寄存器，所以进出内核不用管它们，只有换一个用户任务上 CPU 时才换。

## 调度器

### 调度算法

时间片轮转：就绪的任务排成一个队列，每个任务运行一个时间片，用完了排到队尾。

`Scheduler::schedule()` 做的事：

1. 当前任务如果还能运行，放回就绪队列的队尾。
2. 从队首取下一个任务；队列空了就运行 idle 任务。
3. 如果下一个是用户任务，`hal::UserContext::set_kernel_stack()` 换好它的内核栈。
4. 换浮点/SIMD 寄存器：当前任务是用户任务就 `fp_save` 存进它的 PCB，下一个是用户任务就 `fp_restore` 装回它的。内核线程（idle）不用这些寄存器，轮到它时什么都不做：用户任务 A 换下去时已经存好了，之后不管中间隔了几次 idle，换上来的用户任务都从自己的 PCB 里装。
5. `task_switch_context(&old_ctx_ptr, &next->context)` 切换（第一个参数是“旧任务的现场存到哪里”，是指向指针的指针）。

idle 任务只做一件事：关着中断检查有没有任务可运行，没有就开中断并停机，等下一次中断。检查和停机必须是一个不可分的动作，否则可能在两者之间错过一次唤醒。

调度发生在三种时候：定时器中断发现时间片用完（抢占）、任务自己 `yield`、任务阻塞。

机器有几个 CPU 时，就绪队列还是这一个，每个 CPU 各有自己的“当前任务”
和 idle 任务：哪个 CPU 进了 `schedule()`，就从队首取下一个任务，所以一个进程前后两次可能在
不同的 CPU 上运行。几个 CPU 不会同时在 `schedule()` 里——进内核要先拿内核锁，见
[多个 CPU](../reference/smp.md)。

### 上下文切换

`task_switch_context` 是汇编写的（`src/arch/<arch>/task/`）。它把当前的寄存器存进旧任务的 `cpu_context_t`，从新任务的 `cpu_context_t` 里恢复寄存器和地址空间，然后跳到新任务上次停下的地方。

对一个从没运行过的任务，“上次停下的地方”就是 `hal::UserContext::init` 填好的初始现场：切换代码照常恢复它，其中的特权级是用户态，于是用“从中断返回”的指令（x86 的 `iret`，arm64 的 `eret`）落到用户程序的入口。进入用户态没有单独的代码路径，它就是一次上下文切换。

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

    // 4. 用系统调用入口保存的用户寄存器（frame）构造子进程的现场：
    //    子进程从 fork() 返回处继续执行，返回值是 0
    hal::UserContext::fork(&child->context, frame, child->page_dir_phys, child->kernel_stack);

    // 5. 继承父子关系、特权和硬件许可
    child->parent = parent;
    child->privileged = parent->privileged;
    memcpy(child->hw_allowed, parent->hw_allowed, sizeof(child->hw_allowed));
    child->hw_allowed_count = parent->hw_allowed_count;

    //    浮点寄存器也一样：父进程正在 CPU 上，最新的值在寄存器里，直接存进子进程的 PCB
    hal::UserContext::fp_save(&child->fp_state);

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

    // 3. 切到新地址空间
    mm::Vmm::switch_page_directory(new_dir);

    // 4. 把参数写进参数页（用户栈区域最顶上的一页，地址固定），然后释放旧的地址空间
    memcpy(((user_args_t *)USER_ARGS_ADDR)->data, kargs, args_size);
    mm::Vmm::free_page_directory(old_dir);

    // 5. 这次系统调用不回到原来的程序，而是“返回”到新程序的入口
    hal::UserContext::init(&current->context, entry_point, current->user_stack,
                           current->page_dir_phys, current->kernel_stack);
    hal::UserContext::exec_return(frame, entry_point, current->user_stack);

    // 6. 新程序从干净的浮点状态开始
    hal::UserContext::fp_reset(&current->fp_state);
    hal::UserContext::fp_restore(&current->fp_state);
    return 0;
}
```

新程序的启动代码（`user/lib/src/crt0.cpp`）从参数页取出参数，组装成 `argv` 再调用 `main`（见 [程序参数](../reference/shell.md#程序参数)）。

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
        // 找一个已经退出（僵尸）的匹配子进程（这里写成一个函数，实际是对任务表的一次扫描）
        task_t *child = find_zombie_child(current, pid);
        if (child) {
            *wstatus = encode_status(child);    // 正常退出的退出码，或者终止它的信号
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

等待必须是真正的阻塞。如果父进程在内核里轮询（让出 CPU 再停机等下一次中断），它停机的那段时间里别的任务都得不到运行，系统里每一次唤醒都可能被拖到下一个时钟滴答。

## 内核栈

每个任务有两个栈：用户栈在它自己的地址空间里，内核栈在内核里（`kmalloc` 分配）。任务在用户态运行时用用户栈；一旦因为系统调用、中断或异常进入内核，CPU 换到它的内核栈上——内核不能信任用户栈指针指向的是有效内存。

CPU 怎么知道该换到哪个栈，各架构不同，所以由 `hal::UserContext::set_kernel_stack()` 负责：

- **i686**：写进 TSS（任务状态段）的 `esp0` 字段，CPU 在特权级切换时从那里取。
- **x86_64**：除了 TSS，`SYSCALL` 指令不经过 TSS，系统调用入口自己从一个变量里取，也要更新。
- **arm64**：内核栈指针随现场一起保存和恢复，这里什么都不用做。

## 终止别的进程：kill

CastorOS 没有信号处理函数。`kill(pid, sig)` 只有两种用法：`sig == 0` 探测进程是否存在，其他值终止目标进程。

终止不是由调用者就地完成的。目标进程一定停在内核里的某个点上，可能正持有锁、正排在某个等待队列里；直接改它的状态或释放它的资源会破坏内核。所以 `kill` 只给目标记一个待处理的标记，由目标自己在安全的地方退出：

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

// 系统调用返回用户态之前、以及任何中断即将返回用户态时调用
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
