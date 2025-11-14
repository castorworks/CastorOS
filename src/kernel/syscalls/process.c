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
#include <fs/vfs.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <lib/klog.h>
#include <lib/string.h>

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
    
    task_t *parent = task_get_current();
    if (!parent) {
        LOG_ERROR_MSG("sys_fork: No current task\n");
        return (uint32_t)-1;
    }
    
    LOG_INFO_MSG("sys_fork: Parent PID %u\n", parent->pid);
    
    // 只有用户进程才能 fork
    if (!parent->is_user_process) {
        LOG_ERROR_MSG("sys_fork: Cannot fork kernel thread\n");
        return (uint32_t)-1;
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
    
    // 分配子进程 PCB
    task_t *child = task_alloc();
    if (!child) {
        LOG_ERROR_MSG("sys_fork: Failed to allocate PCB\n");
        return (uint32_t)-12;
    }
    
    // 复制父进程信息
    snprintf(child->name, sizeof(child->name), "%s-child", parent->name);
    child->is_user_process = true;
    child->priority = parent->priority;
    child->time_slice = DEFAULT_TIME_SLICE;
    
    // 克隆页目录（浅拷贝，共享物理页）
    child->page_dir_phys = vmm_clone_page_directory(parent->page_dir_phys);
    if (!child->page_dir_phys) {
        LOG_ERROR_MSG("sys_fork: Failed to clone page directory\n");
        task_free(child);
        return (uint32_t)-12;
    }
    child->page_dir = (page_directory_t*)PHYS_TO_VIRT(child->page_dir_phys);
    
    // 分配内核栈
    child->kernel_stack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack_base) {
        LOG_ERROR_MSG("sys_fork: Failed to allocate kernel stack\n");
        vmm_free_page_directory(child->page_dir_phys);
        task_free(child);
        return (uint32_t)-12;
    }
    child->kernel_stack = child->kernel_stack_base + KERNEL_STACK_SIZE;
    
    // 复制用户空间信息（已在页目录中共享）
    child->user_stack_base = parent->user_stack_base;
    child->user_stack = parent->user_stack;
    child->user_entry = parent->user_entry;
    
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
    child->context.eflags = user_eflags;
    child->context.cr3 = child->page_dir_phys;
    
    // 复制段寄存器
    child->context.cs = (uint16_t)user_cs;
    child->context.ss = (uint16_t)user_ss;
    child->context.ds = (uint16_t)user_ds;
    child->context.es = (uint16_t)user_ds;  // 通常 ES = DS
    child->context.fs = (uint16_t)user_ds;  // 通常 FS = DS
    child->context.gs = (uint16_t)user_ds;  // 通常 GS = DS
    
    // 复制文件描述符表
    if (parent->fd_table) {
        if (fd_table_copy(parent->fd_table, child->fd_table) != 0) {
            LOG_ERROR_MSG("sys_fork: failed to copy fd_table\n");
            vmm_free_page_directory(child->page_dir_phys);
            task_free(child);
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
    
    // 父进程返回子进程 PID
    return child->pid;
}

/**
 * sys_execve - 执行新程序（替换当前进程）
 */
uint32_t sys_execve(const char *path) {
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
        return (uint32_t)-1;
    }
    
    vfs_open(file, 0);
    uint32_t bytes_read = vfs_read(file, 0, file_size, (uint8_t *)elf_data);
    vfs_close(file);
    
    if (bytes_read != file_size) {
        LOG_ERROR_MSG("sys_execve: failed to read ELF file (read %u, expected %u)\n", 
                      bytes_read, file_size);
        kfree(elf_data);
        return (uint32_t)-1;
    }
    
    // 验证 ELF 文件头
    if (!elf_validate_header(elf_data)) {
        LOG_ERROR_MSG("sys_execve: invalid ELF file '%s'\n", path);
        kfree(elf_data);
        return (uint32_t)-1;
    }
    
    // 获取入口点
    uint32_t entry_point = elf_get_entry(elf_data);
    if (entry_point == 0) {
        LOG_ERROR_MSG("sys_execve: failed to get entry point from '%s'\n", path);
        kfree(elf_data);
        return (uint32_t)-1;
    }
    
    // 加载 ELF 到当前进程的页目录
    if (!elf_load(elf_data, file_size, current->page_dir, &entry_point)) {
        LOG_ERROR_MSG("sys_execve: failed to load ELF '%s'\n", path);
        kfree(elf_data);
        return (uint32_t)-1;
    }
    
    // 释放 ELF 数据
    kfree(elf_data);
    
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
                // 分配 fd 0, 1, 2
                int32_t fd = fd_table_alloc(current->fd_table, console, O_RDONLY);
                if (fd != 0) {
                    LOG_WARN_MSG("sys_execve: failed to assign STDIN (fd=%d)\n", fd);
                }
                
                fd = fd_table_alloc(current->fd_table, console, O_WRONLY);
                if (fd != 1) {
                    LOG_WARN_MSG("sys_execve: failed to assign STDOUT (fd=%d)\n", fd);
                }
                
                fd = fd_table_alloc(current->fd_table, console, O_WRONLY);
                if (fd != 2) {
                    LOG_WARN_MSG("sys_execve: failed to assign STDERR (fd=%d)\n", fd);
                }
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
    
    // 直接进入用户模式执行新程序
    // 注意：这个函数不会返回
    task_enter_usermode(entry_point, current->user_stack);
    
    // 永远不会执行到这里
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
        LOG_DEBUG_MSG("sys_nanosleep: sleeping for %u ms\n", sleep_ms);
        task_sleep(sleep_ms);
    }

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}