/**
 * 进程管理相关系统调用实现
 * 
 * exit / fork / exec / waitpid / getpid / getppid / yield / nanosleep / kill
 */

#include <kernel/syscalls/process.h>
#include <kernel/task.h>
#include <kernel/elf.h>
#include <kernel/interrupt.h>
#include <hal/hal.h>
#include <hal/user_context.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/klog.h>
#include <lib/string.h>

/* 默认时间片（与 task.c 保持一致） */
#define DEFAULT_TIME_SLICE 10

/* exec 接受的 ELF 映像大小上限 */
#define EXEC_MAX_IMAGE_SIZE (16u * 1024 * 1024)

/**
 * syscall::Process::exit - 退出当前进程
 */
void syscall::Process::exit(uint32_t code) {
    LOG_DEBUG_MSG("syscall::Process::exit: exit_code=%u\n", code);
    
    task_t *current = kernel::Scheduler::get_current();
    if (current) {
        current->exit_code = code;
        LOG_DEBUG_MSG("syscall::Process::exit: process %u (%s) exiting with code %u\n", 
                      current->pid, current->name, code);
    }
    
    // 调用任务管理器的退出函数
    task_exit(code);
    
    // 永远不会执行到这里
    while (1) {
        hal::Cpu::halt();
    }
}

/**
 * syscall::Process::fork - 创建子进程（简化包装器）
 * 
 * 注意：这个函数实际上不会被直接调用
 * 系统调用包装器会调用 sys_fork_with_frame
 */
uint32_t syscall::Process::fork(uintptr_t *frame) {
    // 禁用中断，保证 fork 过程的原子性
    bool prev_state = kernel::Interrupts::disable();
    
    task_t *parent = kernel::Scheduler::get_current();
    if (!parent) {
        LOG_ERROR_MSG("syscall::Process::fork: No current task\n");
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("syscall::Process::fork: Parent PID %u\n", parent->pid);
    
    // 只有用户进程才能 fork
    if (!parent->is_user_process) {
        LOG_ERROR_MSG("syscall::Process::fork: Cannot fork kernel thread\n");
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-1;
    }
    
    // 【内存安全检查】检查是否有足够内存创建新进程
    // 需要：1个页目录 + 页表（最多512个） + 其他开销
    // 注意：由于使用 COW，不需要预留用户栈的全部 2048 页
    // 但需要预留足够的页表和页目录
    mm::PmmInfo mem_info = mm::Pmm::get_info();
    uint32_t min_required_frames = 64;  // 页目录 + 页表 + 内核栈 + 其他
    if (mem_info.free_frames < min_required_frames) {
        LOG_ERROR_MSG("syscall::Process::fork: Insufficient memory (free=%llu, required>=%u)\n",
                     (unsigned long long)mem_info.free_frames, min_required_frames);
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;  // ENOMEM
    }
    
    // 分配子进程 PCB
    task_t *child = kernel::Scheduler::alloc();
    if (!child) {
        LOG_ERROR_MSG("syscall::Process::fork: Failed to allocate PCB\n");
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;
    }
    
    // 复制父进程信息
    strncpy(child->name, parent->name, sizeof(child->name) - 1);
    child->name[sizeof(child->name) - 1] = '\0';
    child->is_user_process = true;
    child->priority = parent->priority;
    child->time_slice = DEFAULT_TIME_SLICE;
    
    // 以下错误路径的约定：一旦资源记录到子进程 PCB（page_dir_phys、
    // kernel_stack_base），就只由 kernel::Scheduler::free(child) 释放。
    // 不要在调用它之前再手动释放一次——那会二次释放内核栈，并把与父进程
    // COW 共享的物理页的引用计数多减一次。

    // 克隆页目录（深拷贝，完全复制物理页）
    child->page_dir_phys = mm::Vmm::clone_page_directory(parent->page_dir_phys);
    if (!child->page_dir_phys) {
        LOG_ERROR_MSG("syscall::Process::fork: Failed to clone page directory\n");
        kernel::Scheduler::free(child);
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;
    }
    
    // 分配内核栈
    child->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack_base) {
        LOG_ERROR_MSG("syscall::Process::fork: Failed to allocate kernel stack\n");
        kernel::Scheduler::free(child);
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;
    }
    child->kernel_stack = child->kernel_stack_base + KERNEL_STACK_SIZE;
    
    // 复制用户空间信息（已在页目录中共享）
    child->user_stack_base = parent->user_stack_base;
    child->user_stack = parent->user_stack;
    child->user_entry = parent->user_entry;
    
    // 复制堆信息
    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->heap_max = parent->heap_max;
    
    // 子进程的寄存器和父进程进入这次系统调用时一样，只是 fork() 在它那里返回 0
    hal::UserContext::fork(&child->user_context, frame, child->page_dir_phys, child->kernel_stack);
    kernel::Scheduler::start_in_user_mode(child);
    
    // 设置父子关系
    child->parent = parent;
    child->privileged = parent->privileged;  // fork 继承特权和硬件许可
    memcpy(child->hw_allowed, parent->hw_allowed, sizeof(child->hw_allowed));
    child->hw_allowed_count = parent->hw_allowed_count;

    // 子进程的浮点寄存器和父进程此刻的一样：父进程正在 CPU 上，最新的值在寄存器里
    hal::UserContext::fp_save(&child->fp_state);
    
    // 添加到就绪队列
    child->state = TASK_READY;
    kernel::Scheduler::ready_queue_add(child);
    
    LOG_DEBUG_MSG("syscall::Process::fork: Created child PID %u\n", child->pid);
    
    // 恢复中断状态
    kernel::Interrupts::restore(prev_state);
    
    // 父进程返回子进程 PID
    return child->pid;
}

/**
 * syscall::Process::exec - 用一个 ELF 映像替换当前进程
 *
 * 内核不认识文件：映像由调用者提供（位于调用者的用户地址空间，
 * 系统调用入口已校验可读）。
 *
 * @param frame 系统调用栈帧指针（架构相关大小）
 * @param image ELF 映像
 * @param size  映像大小
 * @param args  参数块（"arg0\0arg1\0..."），可为 NULL
 * @param args_size 参数块长度，不超过 USER_ARGS_MAX
 * @return 成功则不返回到原程序，失败返回 -1
 */
uint32_t syscall::Process::exec(uintptr_t *frame, const void *image, size_t size,
                                const char *args, size_t args_size) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current || !frame || !image) {
        return (uint32_t)-1;
    }

    if (size == 0 || size > EXEC_MAX_IMAGE_SIZE) {
        LOG_ERROR_MSG("syscall::Process::exec: bad image size %llu\n", (unsigned long long)size);
        return (uint32_t)-1;
    }
    uint32_t file_size = (uint32_t)size;

    // 复制到内核：加载时要切到新地址空间，旧地址空间里的映像就看不到了
    void *elf_data = kmalloc(file_size);
    if (!elf_data) {
        LOG_ERROR_MSG("syscall::Process::exec: out of memory for ELF image\n");
        return (uint32_t)-1;
    }
    memcpy(elf_data, image, file_size);

    // 参数块同样要先拿进内核；它总是以 NUL 结尾
    if (!args || args_size > USER_ARGS_MAX) {
        args_size = 0;
    }
    char *kargs = NULL;
    if (args_size > 0) {
        kargs = (char *)kmalloc(args_size);
        if (!kargs) {
            kfree(elf_data);
            return (uint32_t)-1;
        }
        memcpy(kargs, args, args_size);
        kargs[args_size - 1] = '\0';
    }

    // 完整校验 ELF 映像（文件头、程序头表、各段的文件范围和地址范围、入口点），
    // 在创建新地址空间之前就拒绝不合法的映像
    if (!kernel::Elf::validate(elf_data, file_size)) {
        LOG_DEBUG_MSG("syscall::Process::exec: invalid ELF image\n");
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }

    uintptr_t entry_point = kernel::Elf::get_entry(elf_data, file_size);
    if (entry_point == 0) {
        LOG_ERROR_MSG("syscall::Process::exec: no entry point\n");
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }

    // ============================================================================
    // 创建新的地址空间
    // 必须使用新的页目录，否则直接在旧页目录上加载会导致：
    // 1. 覆盖旧映射时泄露物理页
    // 2. 失败时无法回滚
    // ============================================================================
    
    uintptr_t new_dir_phys = mm::Vmm::create_page_directory();
    if (!new_dir_phys) {
        LOG_ERROR_MSG("syscall::Process::exec: failed to create new page directory\n");
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }
    
    // 保存旧的页目录信息，用于回滚或释放
    uintptr_t old_dir_phys = current->page_dir_phys;
    
    // 加载 ELF 到新页目录
    uintptr_t program_end;
    if (!kernel::Elf::load(elf_data, file_size, new_dir_phys, &entry_point, &program_end)) {
        LOG_ERROR_MSG("syscall::Process::exec: failed to load ELF\n");
        mm::Vmm::free_page_directory(new_dir_phys);
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }
    
    // 释放 ELF 数据（已经加载到新页目录的物理页中了）
    kfree(elf_data);
    
    // 临时更新进程的页目录指针，以便 kernel::Scheduler::setup_user_stack 操作新目录
    current->page_dir_phys = new_dir_phys;
    
    // 【内存安全检查】在分配用户栈前检查是否有足够内存
    // USER_STACK_SIZE / PAGE_SIZE = 需要的页数，再加一些页表开销
    uint32_t stack_pages_needed = (USER_STACK_SIZE / PAGE_SIZE) + 4;  // +4 用于页表
    mm::PmmInfo execve_mem_info = mm::Pmm::get_info();
    if (execve_mem_info.free_frames < stack_pages_needed) {
        LOG_ERROR_MSG("syscall::Process::exec: Insufficient memory for user stack (free=%llu, required=%u)\n",
                     (unsigned long long)execve_mem_info.free_frames, stack_pages_needed);
        // 回滚
        current->page_dir_phys = old_dir_phys;
        mm::Vmm::free_page_directory(new_dir_phys);
        kfree(kargs);
        return (uint32_t)-1;  // ENOMEM
    }
    
    // 在新页目录中设置用户栈
    if (!kernel::Scheduler::setup_user_stack(current)) {
        LOG_ERROR_MSG("syscall::Process::exec: failed to setup user stack\n");
        // 回滚
        current->page_dir_phys = old_dir_phys;
        mm::Vmm::free_page_directory(new_dir_phys);
        kfree(kargs);
        return (uint32_t)-1;
    }
    
    // 设置堆管理
    // 堆从程序结束后的下一页开始
    current->heap_start = PAGE_ALIGN_UP(program_end);
    current->heap_end = current->heap_start;
    // 堆最大值：留出 8MB 给栈
    current->heap_max = current->user_stack_base - (8 * 1024 * 1024);
    
    LOG_DEBUG_MSG("syscall::Process::exec: heap: start=0x%llx, end=0x%llx, max=0x%llx\n", 
                 (unsigned long long)current->heap_start, 
                 (unsigned long long)current->heap_end, 
                 (unsigned long long)current->heap_max);
    
    // ============================================================================
    // 切换到新地址空间
    // ============================================================================
    
    mm::Vmm::switch_page_directory(new_dir_phys);

    // 现在处于新地址空间：把参数写进参数页（新栈的页都已清零）
    if (kargs) {
        // 进程以 argv[0] 命名
        strncpy(current->name, kargs, sizeof(current->name) - 1);
        current->name[sizeof(current->name) - 1] = '\0';

        user_args_t *uargs = (user_args_t *)USER_ARGS_ADDR;
        uargs->length = (uint32_t)args_size;
        memcpy(uargs->data, kargs, args_size);
        kfree(kargs);
    }
    
    // 释放旧页目录及其映射的所有用户空间物理页
    // 这解决了 exec 覆盖映射导致的内存泄露问题
    mm::Vmm::free_page_directory(old_dir_phys);
    
    // 更新进程信息
    current->user_entry = entry_point;
    current->is_user_process = true;
    
    // 这次系统调用不回到原来的程序，而是"返回"到新程序的入口；任务自己的现场也换成新的
    // （里面记着地址空间，下次被换上 CPU 时要用）
    hal::UserContext::init(&current->context, entry_point, current->user_stack,
                           current->page_dir_phys, current->kernel_stack);
    hal::UserContext::exec_return(frame, entry_point, current->user_stack);

    // 新程序从干净的浮点状态开始，不继承旧程序留在寄存器里的东西
    hal::UserContext::fp_reset(&current->fp_state);
    hal::UserContext::fp_restore(&current->fp_state);

    // 返回 0，让系统调用正常返回（通过 iret 到新程序）
    return 0;
}

/**
 * syscall::Process::getpid - 获取当前进程 PID
 */
uint32_t syscall::Process::getpid() {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::getpid: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("syscall::Process::getpid: returning PID %u\n", current->pid);
    return current->pid;
}

/**
 * syscall::Process::getppid - 获取父进程 PID
 * @return 父进程 PID，如果没有父进程返回 0
 */
uint32_t syscall::Process::getppid() {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::getppid: no current task\n");
        return 0;
    }
    
    if (current->parent && current->parent->state != TASK_UNUSED) {
        LOG_DEBUG_MSG("syscall::Process::getppid: returning PPID %u\n", current->parent->pid);
        return current->parent->pid;
    }
    
    // 没有父进程（如 init 进程或孤儿进程）
    LOG_DEBUG_MSG("syscall::Process::getppid: no parent, returning 0\n");
    return 0;
}

/**
 * syscall::Process::yield - 主动让出 CPU
 */
uint32_t syscall::Process::yield() {
    LOG_DEBUG_MSG("syscall::Process::yield: yielding CPU\n");
    
    // 调用任务管理器的让出函数
    kernel::Scheduler::yield();
    
    return 0;
}

/**
 * syscall::Process::nanosleep - 睡眠指定时间
 */
uint32_t syscall::Process::nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) {
        LOG_ERROR_MSG("syscall::Process::nanosleep: req is NULL\n");
        return (uint32_t)-1;
    }

    if (req->tv_nsec >= 1000000000u) {
        LOG_ERROR_MSG("syscall::Process::nanosleep: invalid tv_nsec=%u\n", req->tv_nsec);
        return (uint32_t)-1;
    }

    uint64_t total_ns = (uint64_t)req->tv_sec * 1000000000ull + req->tv_nsec;
    uint64_t total_ms = total_ns / 1000000ull;

    if (total_ms == 0 && total_ns > 0) {
        total_ms = 1;
    }

    if (total_ms > 0) {
        if (total_ms > 0xFFFFFFFFull) {
            total_ms = 0xFFFFFFFFull;
        }
        uint32_t sleep_ms = (uint32_t)total_ms;
        kernel::Scheduler::sleep(sleep_ms);
    }

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

/* kill 接受的信号号上限（不含）。退出状态用低 7 位编码信号号 */
#define KILL_SIGNAL_MAX 64

/**
 * syscall::Process::kill - 向进程发送信号
 *
 * 简化实现：没有信号处理函数，除 0 以外的信号都终止目标进程；
 * 信号 0 只检查目标是否存在。
 *
 * 终止不是由调用者就地完成的。调度是协作式的，别的任务一定停在内核里的
 * 某个 yield/block 点上，可能正持有互斥锁、正排在等待队列里；在这里改它的
 * 状态或释放它的内核栈都会破坏内核。所以只给目标记一个待处理的信号，
 * 由目标自己在系统调用返回用户态前退出（kernel::Scheduler::deliver_pending_kill）。
 * 阻塞在锁上的目标要等到被正常唤醒后才会退出。
 */
uint32_t syscall::Process::kill(uint32_t pid, uint32_t signal) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::kill: no current task\n");
        return (uint32_t)-1;
    }

    LOG_DEBUG_MSG("syscall::Process::kill: PID %u sending signal %u to PID %u\n",
                  current->pid, signal, pid);

    // 不能杀死 idle 进程（PID 0）
    if (pid == 0) {
        LOG_WARN_MSG("syscall::Process::kill: cannot kill idle process (PID 0)\n");
        return (uint32_t)-1;
    }

    if (signal >= KILL_SIGNAL_MAX) {
        LOG_WARN_MSG("syscall::Process::kill: invalid signal %u\n", signal);
        return (uint32_t)-1;
    }

    // 查找目标进程
    task_t *target = kernel::Scheduler::get_by_pid(pid);
    if (!target || target->state == TASK_UNUSED || target->state == TASK_TERMINATED) {
        LOG_DEBUG_MSG("syscall::Process::kill: process %u not found\n", pid);
        return (uint32_t)-1;
    }

    // 内核线程不是用户进程能终止的对象
    if (!target->is_user_process) {
        LOG_WARN_MSG("syscall::Process::kill: PID %u is a kernel thread, refused\n", pid);
        return (uint32_t)-1;
    }

    // 信号 0：只探测目标是否存在，任何进程都可以问
    // （服务进程靠它发现客户已经退出，回收为客户保留的资源）
    if (signal == 0) {
        return 0;
    }

    // 非特权进程只能向自己和自己的子孙进程发信号
    if (!kernel::Scheduler::current_is_privileged() && target != current &&
        !kernel::Scheduler::is_descendant(target, current)) {
        LOG_DEBUG_MSG("syscall::Process::kill: PID %u may not signal PID %u\n", current->pid, pid);
        return (uint32_t)-1;
    }

    // 已经是僵尸：进程已退出，等待父进程回收，视为成功
    if (target->state == TASK_ZOMBIE) {
        LOG_DEBUG_MSG("syscall::Process::kill: process %u is already zombie\n", pid);
        return 0;
    }

    // 杀死自己：这里就是自己的上下文，直接走正常的退出路径（不返回）
    if (target == current) {
        kernel::Scheduler::exit_current(128 + signal, true, signal);
    }

    if (!kernel::Scheduler::request_kill(target, signal)) {
        // 目标在此期间已经退出
        return 0;
    }

    LOG_DEBUG_MSG("syscall::Process::kill: signal %u queued for process %u (%s)\n",
                  signal, pid, target->name);
    return 0;
}

/**
 * syscall::Process::waitpid - 等待子进程退出
 * 
 * @param pid     要等待的进程 PID（-1 表示任意子进程，>0 表示特定进程）
 * @param wstatus 退出状态存储地址（可为 NULL）
 * @param options 等待选项（WNOHANG = 非阻塞）
 * @return 成功返回子进程 PID，没有子进程返回 (uint32_t)-1，WNOHANG 时无退出子进程返回 0
 */
uint32_t syscall::Process::waitpid(int32_t pid, uint32_t *wstatus, uint32_t options) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::waitpid: no current task\n");
        return (uint32_t)-1;
    }
    
    bool non_blocking = (options & WNOHANG) != 0;
    
    LOG_DEBUG_MSG("syscall::Process::waitpid: PID %u waiting for child PID %d (options=%u)\n", 
                  current->pid, pid, options);
    
    // 循环等待，直到找到退出的子进程
    while (true) {
        bool prev_state = kernel::Interrupts::disable();
        
        // 查找符合条件的子进程
        task_t *found_child = NULL;
        bool has_waited_child = false;
        
        // 遍历所有任务，查找子进程
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            task_t *task = &task_pool[i];
            
            // 跳过未使用的任务
            if (task->state == TASK_UNUSED) {
                continue;
            }
            
            // 检查是否为当前进程的子进程
            if (task->parent != current) {
                continue;
            }
            
            // 检查 PID 是否匹配
            if (pid > 0 && (int32_t)task->pid != pid) {
                continue;  // 不是我们要等待的特定进程
            }
            
            // 找到了符合条件的子进程（无论状态如何）
            has_waited_child = true;
            
            // 检查是否为僵尸进程（已退出）
            if (task->state == TASK_ZOMBIE) {
                found_child = task;
                break;
            }
        }
        
        // 如果找到了退出的子进程
        if (found_child) {
            uint32_t child_pid = found_child->pid;
            uint32_t status = 0;
            
            // 构造退出状态
            if (found_child->exit_signaled) {
                // 被信号终止：低 7 位 = 信号号（与用户库的 WTERMSIG 一致）
                status = found_child->exit_signal & 0x7F;
            } else {
                // 正常退出：低 8 位 = 0，高 8 位 = 退出码
                status = (found_child->exit_code & 0xFF) << 8;
            }
            
            // 将状态写回用户空间（如果提供了地址）
            if (wstatus != NULL) {
                *wstatus = status;
            }
            
            LOG_DEBUG_MSG("syscall::Process::waitpid: found zombie child PID %u, status=%u\n", 
                         child_pid, status);
            
            // 回收子进程资源
            kernel::Scheduler::free(found_child);
            
            kernel::Interrupts::restore(prev_state);
            return child_pid;
        }
        
        kernel::Interrupts::restore(prev_state);
        
        // 如果没有符合条件的子进程（指定的进程不存在或不是子进程），返回错误
        if (!has_waited_child) {
            if (pid == -1) {
                LOG_DEBUG_MSG("syscall::Process::waitpid: no child processes\n");
            } else {
                LOG_DEBUG_MSG("syscall::Process::waitpid: child PID %d not found or not a child\n", pid);
            }
            return (uint32_t)-1;
        }
        
        // 如果是非阻塞模式且没有退出的子进程，返回 0
        if (non_blocking) {
            LOG_DEBUG_MSG("syscall::Process::waitpid: WNOHANG and no exited children\n");
            return 0;
        }
        
        // 自己被 kill 了（request_kill 会把等子进程的任务唤醒）：返回到系统调用出口去执行退出
        if (current->kill_pending) {
            return (uint32_t)-1;
        }

        // 阻塞到有子进程退出：exit_current 在子进程变成僵尸时唤醒我们
        // （等待对象就是自己的 PCB）。从上面的检查到这里没有调度点，不会错过唤醒。
        kernel::Scheduler::block(current);
    }
}