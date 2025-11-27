/**
 * 进程管理相关系统调用实现
 * 
 * 实现 POSIX 标准的进程管理系统调用：
 * - exit(2), fork(2), execve(2)
 * - getpid(2), yield(2), nanosleep(2)
 */

#include <kernel/syscalls/process.h>
#include <kernel/syscalls/fs.h>
#include <kernel/task.h>
#include <kernel/elf.h>
#include <kernel/fd_table.h>
#include <kernel/gdt.h>
#include <kernel/interrupt.h>
#include <kernel/user.h>
#include <fs/vfs.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/klog.h>
#include <lib/string.h>

// 辅助函数：检查页目录项是否存在
static inline bool is_present(uint32_t pde) { return pde & 0x1; }
// 辅助函数：从页目录项中提取物理地址
static inline uint32_t get_frame(uint32_t pde) { return pde & 0xFFFFF000; }

/* 默认时间片（与 task.c 保持一致） */
#define DEFAULT_TIME_SLICE 10

/**
 * sys_exit - 退出当前进程
 */
void sys_exit(uint32_t code) {
    LOG_DEBUG_MSG("sys_exit: exit_code=%u\n", code);
    
    task_t *current = task_get_current();
    if (current) {
        current->exit_code = code;
        LOG_DEBUG_MSG("sys_exit: process %u (%s) exiting with code %u\n", 
                      current->pid, current->name, code);
    }
    
    // 调用任务管理器的退出函数
    task_exit(code);
    
    // 永远不会执行到这里
    while (1) {
        asm volatile("hlt");
    }
}

/**
 * sys_fork - 创建子进程（简化包装器）
 * 
 * 注意：这个函数实际上不会被直接调用
 * 系统调用包装器会调用 sys_fork_with_frame
 */
uint32_t sys_fork(uint32_t *frame) {
    // 禁用中断，保证 fork 过程的原子性
    bool prev_state = interrupts_disable();
    
    task_t *parent = task_get_current();
    if (!parent) {
        LOG_ERROR_MSG("sys_fork: No current task\n");
        interrupts_restore(prev_state);
        return (uint32_t)-1;
    }
    
    LOG_INFO_MSG("sys_fork: Parent PID %u\n", parent->pid);
    
    // 只有用户进程才能 fork
    if (!parent->is_user_process) {
        LOG_ERROR_MSG("sys_fork: Cannot fork kernel thread\n");
        interrupts_restore(prev_state);
        return (uint32_t)-1;
    }
    
    // 【内存安全检查】检查是否有足够内存创建新进程
    // 需要：1个页目录 + 页表（最多512个） + 其他开销
    // 注意：由于使用 COW，不需要预留用户栈的全部 2048 页
    // 但需要预留足够的页表和页目录
    pmm_info_t mem_info = pmm_get_info();
    uint32_t min_required_frames = 64;  // 页目录 + 页表 + 内核栈 + 其他
    if (mem_info.free_frames < min_required_frames) {
        LOG_ERROR_MSG("sys_fork: Insufficient memory (free=%u, required>=%u)\n",
                     mem_info.free_frames, min_required_frames);
        interrupts_restore(prev_state);
        return (uint32_t)-12;  // ENOMEM
    }
    
    // 直接从传递的 frame 参数读取用户态寄存器
    // frame 指针由 syscall_handler 传递，指向它保存寄存器的位置
    uint32_t user_ds     = frame[0];   // DS
    uint32_t user_eax    = frame[1];   // EAX (系统调用号)
    uint32_t user_ebx    = frame[2];   // EBX
    uint32_t user_ecx    = frame[3];   // ECX
    uint32_t user_edx    = frame[4];   // EDX
    uint32_t user_esi    = frame[5];   // ESI
    uint32_t user_edi    = frame[6];   // EDI
    uint32_t user_ebp    = frame[7];   // EBP
    uint32_t user_eip    = frame[8];   // EIP (IRET)
    uint32_t user_cs     = frame[9];   // CS (IRET)
    uint32_t user_eflags = frame[10];  // EFLAGS (IRET)
    uint32_t user_esp    = frame[11];  // ESP (IRET)
    uint32_t user_ss     = frame[12];  // SS (IRET)
    
    (void)user_eax;  // 系统调用号，不需要复制
    
    LOG_DEBUG_MSG("sys_fork: Captured user context:\n");
    LOG_DEBUG_MSG("  EIP=%x ESP=%x EBP=%x\n", user_eip, user_esp, user_ebp);
    LOG_DEBUG_MSG("  CS=%x SS=%x DS=%x EFLAGS=%x\n", 
                  user_cs, user_ss, user_ds, user_eflags);
    
    // 【安全检查】验证父进程页目录的完整性（仅检查前几个 PDE）
    page_directory_t *parent_dir = parent->page_dir;
    for (uint32_t i = 0; i < 10; i++) {
        if (is_present(parent_dir->entries[i])) {
            uint32_t phys = get_frame(parent_dir->entries[i]);
            if (phys == 0 || phys >= 0x80000000) {
                LOG_ERROR_MSG("sys_fork: Parent PDE[%u] corrupted: 0x%x (phys=0x%x)\n", 
                             i, parent_dir->entries[i], phys);
                LOG_ERROR_MSG("  Parent: PID=%u, name=%s, page_dir=%p, page_dir_phys=0x%x\n",
                             parent->pid, parent->name, parent_dir, parent->page_dir_phys);
                // 打印更多 PDE 以帮助诊断
                LOG_ERROR_MSG("  PDE[0]=%x, PDE[1]=%x, PDE[2]=%x, PDE[3]=%x\n",
                             parent_dir->entries[0], parent_dir->entries[1],
                             parent_dir->entries[2], parent_dir->entries[3]);
                interrupts_restore(prev_state);
                return (uint32_t)-1;
            }
        }
    }
    
    // 分配子进程 PCB
    task_t *child = task_alloc();
    if (!child) {
        LOG_ERROR_MSG("sys_fork: Failed to allocate PCB\n");
        interrupts_restore(prev_state);
        return (uint32_t)-12;
    }
    
    // 复制父进程信息
    snprintf(child->name, sizeof(child->name), "%s-child", parent->name);
    child->is_user_process = true;
    child->priority = parent->priority;
    child->time_slice = DEFAULT_TIME_SLICE;
    
    // 克隆页目录（深拷贝，完全复制物理页）
    child->page_dir_phys = vmm_clone_page_directory(parent->page_dir_phys);
    if (!child->page_dir_phys) {
        LOG_ERROR_MSG("sys_fork: Failed to clone page directory\n");
        task_free(child);
        interrupts_restore(prev_state);
        return (uint32_t)-12;
    }
    child->page_dir = (page_directory_t*)PHYS_TO_VIRT(child->page_dir_phys);
    
    // 分配内核栈
    child->kernel_stack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack_base) {
        LOG_ERROR_MSG("sys_fork: Failed to allocate kernel stack\n");
        vmm_free_page_directory(child->page_dir_phys);
        task_free(child);
        interrupts_restore(prev_state);
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
    
    // 初始化子进程上下文
    // 按照 Unix fork 语义：子进程从 fork() 调用返回处继续执行
    memset(&child->context, 0, sizeof(cpu_context_t));
    
    // 复制父进程的所有用户态寄存器
    child->context.eax = 0;  // 子进程 fork 返回 0（唯一的区别）
    child->context.ebx = user_ebx;
    child->context.ecx = user_ecx;
    child->context.edx = user_edx;
    child->context.esi = user_esi;
    child->context.edi = user_edi;
    child->context.ebp = user_ebp;
    child->context.esp = user_esp;  // 使用父进程当前的用户栈指针
    child->context.eip = user_eip;  // 从 fork() 调用返回处继续
    
    // 清理 EFLAGS 中的敏感位，防止权限提升
    // 保留：CF, PF, AF, ZF, SF, OF, DF, IF
    // 清除：IOPL, NT, RF, VM, AC, VIF, VIP, ID
    child->context.eflags = (user_eflags & 0x00000CD5) | 0x00000202;  // IF=1
    
    child->context.cr3 = child->page_dir_phys;
    
    // 复制段寄存器（强制使用 Ring 3 段，防止权限提升）
    // 不信任用户提供的段选择子，强制设置为用户态段
    child->context.cs = 0x1B;  // 用户代码段（Ring 3）
    child->context.ss = 0x23;  // 用户栈段（Ring 3）
    child->context.ds = 0x23;  // 用户数据段（Ring 3）
    child->context.es = 0x23;
    child->context.fs = 0x23;
    child->context.gs = 0x23;
    
    // 分配并复制文件描述符表
    if (parent->fd_table) {
        child->fd_table = (fd_table_t*)kmalloc(sizeof(fd_table_t));
        if (!child->fd_table) {
            LOG_ERROR_MSG("sys_fork: Failed to allocate fd_table\n");
            kfree((void*)child->kernel_stack_base);
            vmm_free_page_directory(child->page_dir_phys);
            task_free(child);
            interrupts_restore(prev_state);
            return (uint32_t)-12;
        }
        
        // 必须先初始化 fd_table（包括其中的锁），再复制内容
        fd_table_init(child->fd_table);
        
        if (fd_table_copy(parent->fd_table, child->fd_table) != 0) {
            LOG_ERROR_MSG("sys_fork: failed to copy fd_table\n");
            kfree(child->fd_table);
            kfree((void*)child->kernel_stack_base);
            vmm_free_page_directory(child->page_dir_phys);
            task_free(child);
            interrupts_restore(prev_state);
            return (uint32_t)-1;
        }
    }
    
    // 继承当前工作目录
    strcpy(child->cwd, parent->cwd);
    
    // 设置父子关系
    child->parent = parent;
    
    // 添加到就绪队列
    child->state = TASK_READY;
    ready_queue_add(child);
    
    LOG_INFO_MSG("sys_fork: Created child PID %u\n", child->pid);
    
    // 恢复中断状态
    interrupts_restore(prev_state);
    
    // 父进程返回子进程 PID
    return child->pid;
}

/**
 * sys_execve - 执行新程序（替换当前进程）
 * 
 * @param frame 系统调用栈帧指针
 * @param path  程序路径
 * @return 成功则不返回，失败返回 -1
 */
uint32_t sys_execve(uint32_t *frame, const char *path) {
    if (!path) {
        LOG_ERROR_MSG("sys_execve: path is NULL\n");
        return (uint32_t)-1;
    }
    
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_execve: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_execve: loading '%s' for PID %u\n", path, current->pid);
    
    // 打开 ELF 文件
    fs_node_t *file = vfs_path_to_node(path);
    if (!file) {
        LOG_ERROR_MSG("sys_execve: file '%s' not found\n", path);
        return (uint32_t)-1;
    }
    
    // 读取整个 ELF 文件到内存
    uint32_t file_size = file->size;
    void *elf_data = kmalloc(file_size);
    if (!elf_data) {
        LOG_ERROR_MSG("sys_execve: failed to allocate memory for ELF file\n");
        vfs_release_node(file);  // 释放节点
        return (uint32_t)-1;
    }
    
    vfs_open(file, 0);
    uint32_t bytes_read = vfs_read(file, 0, file_size, (uint8_t *)elf_data);
    vfs_close(file);
    
    if (bytes_read != file_size) {
        LOG_ERROR_MSG("sys_execve: failed to read ELF file (read %u, expected %u)\n", 
                      bytes_read, file_size);
        kfree(elf_data);
        vfs_release_node(file);  // 释放节点
        return (uint32_t)-1;
    }
    
    // 验证 ELF 文件头
    if (!elf_validate_header(elf_data)) {
        LOG_ERROR_MSG("sys_execve: invalid ELF file '%s'\n", path);
        kfree(elf_data);
        vfs_release_node(file);  // 释放节点
        return (uint32_t)-1;
    }
    
    // 获取入口点
    uint32_t entry_point = elf_get_entry(elf_data);
    if (entry_point == 0) {
        LOG_ERROR_MSG("sys_execve: failed to get entry point from '%s'\n", path);
        kfree(elf_data);
        vfs_release_node(file);  // 释放节点
        return (uint32_t)-1;
    }
    
    // 文件已读取完毕，释放节点
    vfs_release_node(file);
    
    // ============================================================================
    // 创建新的地址空间
    // 必须使用新的页目录，否则直接在旧页目录上加载会导致：
    // 1. 覆盖旧映射时泄露物理页
    // 2. 失败时无法回滚
    // ============================================================================
    
    uint32_t new_dir_phys = vmm_create_page_directory();
    if (!new_dir_phys) {
        LOG_ERROR_MSG("sys_execve: failed to create new page directory\n");
        kfree(elf_data);
        return (uint32_t)-1;
    }
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(new_dir_phys);
    
    // 保存旧的页目录信息，用于回滚或释放
    page_directory_t *old_dir = current->page_dir;
    uint32_t old_dir_phys = current->page_dir_phys;
    
    // 加载 ELF 到新页目录
    uint32_t program_end;
    if (!elf_load(elf_data, file_size, new_dir, &entry_point, &program_end)) {
        LOG_ERROR_MSG("sys_execve: failed to load ELF '%s'\n", path);
        vmm_free_page_directory(new_dir_phys);
        kfree(elf_data);
        return (uint32_t)-1;
    }
    
    // 释放 ELF 数据（已经加载到新页目录的物理页中了）
    kfree(elf_data);
    
    // 临时更新进程的页目录指针，以便 task_setup_user_stack 操作新目录
    current->page_dir = new_dir;
    current->page_dir_phys = new_dir_phys;
    
    // 【内存安全检查】在分配用户栈前检查是否有足够内存
    // USER_STACK_SIZE / PAGE_SIZE = 需要的页数，再加一些页表开销
    uint32_t stack_pages_needed = (USER_STACK_SIZE / PAGE_SIZE) + 4;  // +4 用于页表
    pmm_info_t execve_mem_info = pmm_get_info();
    if (execve_mem_info.free_frames < stack_pages_needed) {
        LOG_ERROR_MSG("sys_execve: Insufficient memory for user stack (free=%u, required=%u)\n",
                     execve_mem_info.free_frames, stack_pages_needed);
        // 回滚
        current->page_dir = old_dir;
        current->page_dir_phys = old_dir_phys;
        vmm_free_page_directory(new_dir_phys);
        return (uint32_t)-1;  // ENOMEM
    }
    
    // 在新页目录中设置用户栈
    if (!task_setup_user_stack(current)) {
        LOG_ERROR_MSG("sys_execve: failed to setup user stack\n");
        // 回滚
        current->page_dir = old_dir;
        current->page_dir_phys = old_dir_phys;
        vmm_free_page_directory(new_dir_phys);
        return (uint32_t)-1;
    }
    
    // 设置堆管理
    // 堆从程序结束后的下一页开始
    current->heap_start = PAGE_ALIGN_UP(program_end);
    current->heap_end = current->heap_start;
    // 堆最大值：留出 8MB 给栈
    current->heap_max = current->user_stack_base - (8 * 1024 * 1024);
    
    LOG_DEBUG_MSG("sys_execve: heap: start=%x, end=%x, max=%x\n", 
                 current->heap_start, current->heap_end, current->heap_max);
    
    // ============================================================================
    // 切换到新地址空间
    // ============================================================================
    
    vmm_switch_page_directory(new_dir_phys);
    
    // 释放旧页目录及其映射的所有用户空间物理页
    // 这解决了 exec 覆盖映射导致的内存泄露问题
    vmm_free_page_directory(old_dir_phys);
    
    // 初始化标准文件描述符（如果还没有初始化）
    // 这对于 fork + exec 模式很重要：
    // - fork 出的子进程可能没有 stdio
    // - exec 时需要为新程序初始化 stdin/stdout/stderr
    if (current->fd_table) {
        // 检查是否已经有 fd 0（stdin）
        fd_entry_t *fd0 = fd_table_get(current->fd_table, 0);
        if (!fd0 || !fd0->node) {
            // 初始化标准文件描述符
            fs_node_t *console = vfs_path_to_node("/dev/console");
            if (console) {
                // 注意：fd_table_alloc 会自动增加引用计数
                // 所以我们可以安全地为多个 fd 使用同一个节点
                
                // 分配 fd 0 (stdin)
                int32_t fd = fd_table_alloc(current->fd_table, console, O_RDONLY);
                if (fd != 0) {
                    LOG_WARN_MSG("sys_execve: failed to assign STDIN (fd=%d)\n", fd);
                }
                
                // 分配 fd 1 (stdout) - fd_table_alloc 会增加引用计数
                fd = fd_table_alloc(current->fd_table, console, O_WRONLY);
                if (fd != 1) {
                    LOG_WARN_MSG("sys_execve: failed to assign STDOUT (fd=%d)\n", fd);
                }
                
                // 分配 fd 2 (stderr) - fd_table_alloc 会增加引用计数
                fd = fd_table_alloc(current->fd_table, console, O_WRONLY);
                if (fd != 2) {
                    LOG_WARN_MSG("sys_execve: failed to assign STDERR (fd=%d)\n", fd);
                }
                
                // 关键修复：释放 vfs_path_to_node 的初始引用
                // 三个 fd_table_alloc 调用已经增加了引用计数（每个 fd 一次）
                // 现在释放初始引用，console 的 ref_count = 3（每个 fd 一个）
                vfs_release_node(console);
                
                // 当所有 fd 关闭时，引用计数会降到 0，节点才会被释放
            } else {
                LOG_WARN_MSG("sys_execve: /dev/console not available, stdio not initialized\n");
            }
        }
    }
    
    // 更新进程信息
    current->user_entry = entry_point;
    current->is_user_process = true;
    
    // 更新进程名称（从路径中提取文件名）
    const char *filename = strrchr(path, '/');
    if (filename) {
        filename++; // 跳过 '/'
    } else {
        filename = path;
    }
    strncpy(current->name, filename, sizeof(current->name) - 1);
    current->name[sizeof(current->name) - 1] = '\0';
    
    LOG_DEBUG_MSG("sys_execve: loaded '%s' at entry 0x%x for PID %u\n", 
                  path, entry_point, current->pid);
    
    // 设置用户态上下文
    current->context.eip = entry_point;
    current->context.cs = 0x1B;  // 用户代码段（Ring 3）
    current->context.ds = 0x23;  // 用户数据段（Ring 3）
    current->context.es = 0x23;
    current->context.fs = 0x23;
    current->context.gs = 0x23;
    current->context.ss = 0x23;  // 用户栈段（Ring 3）
    current->context.esp = current->user_stack;
    current->context.eflags = 0x202;  // 中断使能
    current->context.cr3 = current->page_dir_phys;
    
    // ============================================================================
    // 关键修复：修改系统调用栈帧，让 iret 返回到新程序的入口点
    // ============================================================================
    // 
    // 系统调用栈帧布局（从 syscall_asm.asm）：
    //   frame[0] = DS
    //   frame[1] = EAX (返回值)
    //   frame[2] = EBX
    //   frame[3] = ECX
    //   frame[4] = EDX
    //   frame[5] = ESI
    //   frame[6] = EDI
    //   frame[7] = EBP
    //   
    // 在 frame 之后（更高地址），CPU 自动压入的 IRET 栈帧：
    //   frame[8] = EIP  (用户返回地址)
    //   frame[9] = CS   (代码段)
    //   frame[10] = EFLAGS
    //   frame[11] = ESP (用户栈指针)
    //   frame[12] = SS  (栈段)
    //
    // 我们需要修改这些值，让系统调用返回时跳转到新程序
    
    if (frame) {
        // 修改用户段寄存器（syscall_handler 会在返回前恢复这些）
        frame[0] = 0x23;   // DS = 用户数据段
        
        // 修改 IRET 栈帧
        frame[8] = entry_point;        // EIP = 新程序入口点
        frame[9] = 0x1B;               // CS = 用户代码段 (Ring 3)
        frame[10] = 0x202;             // EFLAGS = 中断使能
        frame[11] = current->user_stack;  // ESP = 用户栈顶
        frame[12] = 0x23;              // SS = 用户栈段 (Ring 3)
        
        LOG_DEBUG_MSG("sys_execve: modified syscall frame to return to 0x%x\n", entry_point);
    } else {
        // 如果没有 frame（不应该发生），使用原来的方法
        LOG_WARN_MSG("sys_execve: no frame provided, using fallback method\n");
        task_enter_usermode(entry_point, current->user_stack);
    }
    
    // 返回 0，让系统调用正常返回（通过 iret 到新程序）
    return 0;
}

/**
 * sys_getpid - 获取当前进程 PID
 */
uint32_t sys_getpid(void) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_getpid: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_getpid: returning PID %u\n", current->pid);
    return current->pid;
}

/**
 * sys_getppid - 获取父进程 PID
 * @return 父进程 PID，如果没有父进程返回 0
 */
uint32_t sys_getppid(void) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_getppid: no current task\n");
        return 0;
    }
    
    if (current->parent && current->parent->state != TASK_UNUSED) {
        LOG_DEBUG_MSG("sys_getppid: returning PPID %u\n", current->parent->pid);
        return current->parent->pid;
    }
    
    // 没有父进程（如 init 进程或孤儿进程）
    LOG_DEBUG_MSG("sys_getppid: no parent, returning 0\n");
    return 0;
}

/**
 * sys_yield - 主动让出 CPU
 */
uint32_t sys_yield(void) {
    LOG_DEBUG_MSG("sys_yield: yielding CPU\n");
    
    // 调用任务管理器的让出函数
    task_yield();
    
    return 0;
}

/**
 * sys_nanosleep - 睡眠指定时间
 */
uint32_t sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) {
        LOG_ERROR_MSG("sys_nanosleep: req is NULL\n");
        return (uint32_t)-1;
    }

    if (req->tv_nsec >= 1000000000u) {
        LOG_ERROR_MSG("sys_nanosleep: invalid tv_nsec=%u\n", req->tv_nsec);
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
        task_sleep(sleep_ms);
    }

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

/**
 * sys_kill - 向进程发送信号
 * 
 * 简化实现：目前所有信号都直接终止目标进程
 * 未来可以扩展为支持信号处理和不同的信号行为
 */
uint32_t sys_kill(uint32_t pid, uint32_t signal) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_kill: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("sys_kill: PID %u sending signal %u to PID %u\n", 
                  current->pid, signal, pid);
    
    // 不能杀死 idle 进程（PID 0）
    if (pid == 0) {
        LOG_WARN_MSG("sys_kill: cannot kill idle process (PID 0)\n");
        return (uint32_t)-1;
    }
    
    // 查找目标进程
    task_t *target = task_get_by_pid(pid);
    if (!target) {
        LOG_WARN_MSG("sys_kill: process %u not found\n", pid);
        return (uint32_t)-1;
    }
    
    // 检查进程状态
    if (target->state == TASK_UNUSED || target->state == TASK_TERMINATED) {
        LOG_WARN_MSG("sys_kill: process %u is already terminated\n", pid);
        return (uint32_t)-1;
    }
    
    // 如果进程已经是僵尸状态，说明它已经退出了，不需要再 kill
    // 只是返回成功（kill 一个已经死亡的进程被认为是成功的）
    if (target->state == TASK_ZOMBIE) {
        LOG_DEBUG_MSG("sys_kill: process %u is already zombie\n", pid);
        return 0;
    }
    
    // 简化实现：所有信号都直接终止进程
    // 未来可以扩展为：
    // - SIGTERM: 设置终止标志，让进程优雅退出
    // - SIGKILL: 强制立即终止
    // - SIGINT: 中断信号
    // - 其他信号: 根据信号类型处理
    
    // 安全地访问进程名称（避免在日志中访问可能无效的内存）
    const char *proc_name = (target->name[0] != '\0') ? target->name : "unknown";
    LOG_DEBUG_MSG("sys_kill: terminating process %u (%s) with signal %u\n", 
                  pid, proc_name, signal);
    
    // 设置进程为终止状态
    // 注意：不能直接调用 task_exit，因为那是给当前进程用的
    // 我们需要直接修改目标进程的状态
    // 但是要小心：如果目标进程正在运行，我们需要确保安全地修改它
    bool prev_state = interrupts_disable();
    
    // 再次检查进程状态（在禁用中断后，状态可能已经改变）
    if (target->state == TASK_UNUSED || target->state == TASK_TERMINATED) {
        interrupts_restore(prev_state);
        LOG_DEBUG_MSG("sys_kill: process %u already terminated\n", pid);
        return 0;  // 已经终止，返回成功
    }
    
    // 设置退出信息
    target->exit_code = 128 + signal;  // 标准退出码：128 + 信号号
    target->exit_signaled = true;
    target->exit_signal = signal;
    
    // 处理目标进程的所有子进程
    // 遍历任务池，查找目标进程的子进程
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = task_get_by_pid(i);
        if (!task || task->state == TASK_UNUSED) {
            continue;
        }
        
        // 检查是否为目标进程的子进程
        if (task->parent == target) {
            if (task->state == TASK_ZOMBIE) {
                // 僵尸子进程：直接清理（没有父进程来回收了）
                LOG_DEBUG_MSG("sys_kill: cleaning up zombie child %u of process %u\n", 
                             task->pid, target->pid);
                // 直接释放资源，因为僵尸进程不在就绪队列中
                task_free(task);
            } else {
                // 运行中的子进程：变成孤儿进程
                LOG_DEBUG_MSG("sys_kill: orphaning child %u of process %u\n", 
                             task->pid, target->pid);
                task->parent = NULL;
            }
        }
    }
    
    // 如果目标进程在就绪队列中，需要移除它
    if (target->state == TASK_READY) {
        ready_queue_remove(target);
        LOG_DEBUG_MSG("sys_kill: removed process %u from ready queue\n", pid);
    }
    
    // 根据是否有父进程，决定进程状态
    // 如果有父进程，变成僵尸进程等待父进程回收
    // 否则直接终止（孤儿进程）
    
    // 调试日志：显示父进程信息
    if (target->parent) {
        LOG_DEBUG_MSG("sys_kill: process %u has parent PID %u (state=%d)\n", 
                     target->pid, target->parent->pid, target->parent->state);
    } else {
        LOG_DEBUG_MSG("sys_kill: process %u has NO parent\n", target->pid);
    }
    
    if (target->parent && target->parent->state != TASK_UNUSED) {
        target->state = TASK_ZOMBIE;
        LOG_DEBUG_MSG("sys_kill: process %u becomes zombie, waiting for parent %u\n", 
                     target->pid, target->parent->pid);
        
        interrupts_restore(prev_state);
        LOG_DEBUG_MSG("sys_kill: process %u marked as zombie\n", pid);
    } else {
        // 没有父进程的进程，直接清理
        // 注意：如果目标进程是当前进程（自己 kill 自己），不能立即清理
        // 但这种情况很少见，应该使用 exit() 而不是 kill(自己)
        if (target->parent) {
            LOG_WARN_MSG("sys_kill: process %u parent is UNUSED, treating as orphan\n", pid);
        }
        
        if (target == current) {
            // 进程 kill 自己，标记为 TERMINATED，让调度器清理
            target->state = TASK_TERMINATED;
            interrupts_restore(prev_state);
            LOG_DEBUG_MSG("sys_kill: process %u killed itself\n", pid);
        } else {
            // kill 其他没有父进程的进程，可以安全地立即清理
            LOG_DEBUG_MSG("sys_kill: process %u has no valid parent, freeing immediately\n", 
                         target->pid);
            task_free(target);
            interrupts_restore(prev_state);
            LOG_DEBUG_MSG("sys_kill: process %u freed\n", pid);
        }
    }
    
    return 0;
}

/**
 * sys_waitpid - 等待子进程退出
 * 
 * @param pid     要等待的进程 PID（-1 表示任意子进程，>0 表示特定进程）
 * @param wstatus 退出状态存储地址（可为 NULL）
 * @param options 等待选项（WNOHANG = 非阻塞）
 * @return 成功返回子进程 PID，没有子进程返回 (uint32_t)-1，WNOHANG 时无退出子进程返回 0
 */
uint32_t sys_waitpid(int32_t pid, uint32_t *wstatus, uint32_t options) {
    task_t *current = task_get_current();
    if (!current) {
        LOG_ERROR_MSG("sys_waitpid: no current task\n");
        return (uint32_t)-1;
    }
    
    bool non_blocking = (options & WNOHANG) != 0;
    
    LOG_DEBUG_MSG("sys_waitpid: PID %u waiting for child PID %d (options=%u)\n", 
                  current->pid, pid, options);
    
    // 循环等待，直到找到退出的子进程
    while (true) {
        bool prev_state = interrupts_disable();
        
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
                // 被信号终止：低 8 位 = 信号号
                status = found_child->exit_signal & 0xFF;
            } else {
                // 正常退出：低 8 位 = 0，高 8 位 = 退出码
                status = (found_child->exit_code & 0xFF) << 8;
            }
            
            // 将状态写回用户空间（如果提供了地址）
            if (wstatus != NULL) {
                *wstatus = status;
            }
            
            LOG_DEBUG_MSG("sys_waitpid: found zombie child PID %u, status=%u\n", 
                         child_pid, status);
            
            // 回收子进程资源
            task_free(found_child);
            
            interrupts_restore(prev_state);
            return child_pid;
        }
        
        interrupts_restore(prev_state);
        
        // 如果没有符合条件的子进程（指定的进程不存在或不是子进程），返回错误
        if (!has_waited_child) {
            if (pid == -1) {
                LOG_DEBUG_MSG("sys_waitpid: no child processes\n");
            } else {
                LOG_DEBUG_MSG("sys_waitpid: child PID %d not found or not a child\n", pid);
            }
            return (uint32_t)-1;
        }
        
        // 如果是非阻塞模式且没有退出的子进程，返回 0
        if (non_blocking) {
            LOG_DEBUG_MSG("sys_waitpid: WNOHANG and no exited children\n");
            return 0;
        }
        
        // 阻塞等待：让出 CPU，稍后重试
        // 这里使用简单的轮询 + yield 策略
        // 更好的实现应该让进程进入 BLOCKED 状态，并在子进程退出时唤醒
        task_yield();
    }
}