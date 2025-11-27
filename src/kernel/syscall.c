// ============================================================================
// syscall.c - 系统调用实现
// ============================================================================

#include <kernel/syscall.h>
#include <kernel/syscalls/fs.h>
#include <kernel/syscalls/process.h>
#include <kernel/syscalls/time.h>
#include <kernel/syscalls/system.h>
#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <kernel/isr.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* 系统调用处理函数表 */
typedef uint32_t (*syscall_handler_t)(uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static syscall_handler_t syscall_table[SYS_MAX];

/* 栈帧布局（syscall_handler 中 "mov ebp, esp" 后）：
 * frame[0]  = DS
 * frame[1]  = EAX (syscall_num)
 * frame[2]  = EBX (arg1)
 * frame[3]  = ECX (arg2)
 * frame[4]  = EDX (arg3)
 * frame[5]  = ESI (arg4)
 * frame[6]  = EDI (arg5)
 * frame[7]  = EBP
 * frame[8]  = EIP (IRET)
 * frame[9]  = CS (IRET)
 * frame[10] = EFLAGS (IRET)
 * frame[11] = ESP (IRET)
 * frame[12] = SS (IRET)
 */

/* 默认时间片（与 task.c 保持一致） */
#define DEFAULT_TIME_SLICE 10

/**
 * sys_exit_wrapper - 退出进程（系统调用包装器）
 */
static uint32_t sys_exit_wrapper(uint32_t *frame, uint32_t exit_code, uint32_t p2, uint32_t p3, 
                                 uint32_t p4, uint32_t p5) {
    (void)frame;
    (void)p2; (void)p3; (void)p4; (void)p5;
    sys_exit(exit_code);
    return 0;  // 永远不会返回
}

/**
 * sys_fork_wrapper - 创建子进程（系统调用包装器）
 */
static uint32_t sys_fork_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3, 
                                 uint32_t p4, uint32_t p5) {
    (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    
    return sys_fork(frame);
}

/**
 * sys_execve_wrapper - 执行新程序（系统调用包装器）
 */
static uint32_t sys_execve_wrapper(uint32_t *frame, uint32_t path_addr, uint32_t argv, uint32_t envp, 
                                 uint32_t p4, uint32_t p5) {
    (void)argv; (void)envp; (void)p4; (void)p5;
    
    const char *user_path = (const char *)path_addr;
    if (!user_path) {
        return (uint32_t)-1;
    }
    
    // 将路径从用户空间复制到内核空间
    char path[256];
    uint32_t i;
    
    for (i = 0; i < sizeof(path) - 1; i++) {
        path[i] = user_path[i];
        if (path[i] == '\0') {
            break;
        }
    }
    path[sizeof(path) - 1] = '\0';
    
    // 传递 frame 指针给 sys_execve
    return sys_execve(frame, path);
}

/**
 * 通用系统调用处理入口（从汇编调用）
 * @param syscall_num 系统调用号
 * @param p1-p5 系统调用参数
 * @param frame 栈帧指针，指向 syscall_handler 保存的寄存器
 */
/* 系统调用包装器函数 */

static uint32_t sys_open_wrapper(uint32_t *frame, uint32_t path, uint32_t flags, uint32_t mode,
                                 uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_open((const char *)path, (int32_t)flags, mode);
}

static uint32_t sys_close_wrapper(uint32_t *frame, uint32_t fd, uint32_t p2, uint32_t p3,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_close((int32_t)fd);
}

static uint32_t sys_read_wrapper(uint32_t *frame, uint32_t fd, uint32_t buffer, uint32_t size,
                                 uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_read((int32_t)fd, (void *)buffer, size);
}

static uint32_t sys_write_wrapper(uint32_t *frame, uint32_t fd, uint32_t buffer, uint32_t size,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_write((int32_t)fd, (const void *)buffer, size);
}

static uint32_t sys_lseek_wrapper(uint32_t *frame, uint32_t fd, uint32_t offset, uint32_t whence,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_lseek((int32_t)fd, (int32_t)offset, (int32_t)whence);
}

static uint32_t sys_mkdir_wrapper(uint32_t *frame, uint32_t path, uint32_t mode, uint32_t p3,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_mkdir((const char *)path, mode);
}

static uint32_t sys_unlink_wrapper(uint32_t *frame, uint32_t path, uint32_t p2, uint32_t p3,
                                   uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_unlink((const char *)path);
}

static uint32_t sys_chdir_wrapper(uint32_t *frame, uint32_t path, uint32_t p2, uint32_t p3,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_chdir((const char *)path);
}

static uint32_t sys_getcwd_wrapper(uint32_t *frame, uint32_t buffer, uint32_t size, uint32_t p3,
                                   uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_getcwd((char *)buffer, size);
}

static uint32_t sys_getdents_wrapper(uint32_t *frame, uint32_t fd, uint32_t index, uint32_t dirent,
                                     uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_getdents((int32_t)fd, index, (void *)dirent);
}

static uint32_t sys_getpid_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3,
                                   uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_getpid();
}

static uint32_t sys_getppid_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3,
                                    uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_getppid();
}

static uint32_t sys_yield_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_yield();
}

static uint32_t sys_nanosleep_wrapper(uint32_t *frame, uint32_t req_ptr, uint32_t rem_ptr, uint32_t p3,
                                      uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    const struct timespec *req = (const struct timespec *)req_ptr;
    struct timespec *rem = (struct timespec *)rem_ptr;
    return sys_nanosleep(req, rem);
}

static uint32_t sys_time_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3,
                                 uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_time();
}

static uint32_t sys_reboot_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3,
                                   uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    sys_reboot();
    return 0;
}

static uint32_t sys_poweroff_wrapper(uint32_t *frame, uint32_t p1, uint32_t p2, uint32_t p3,
                                     uint32_t p4, uint32_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    sys_poweroff();
    return 0;
}

static uint32_t sys_kill_wrapper(uint32_t *frame, uint32_t pid, uint32_t signal,
                                 uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_kill(pid, signal);
}

static uint32_t sys_waitpid_wrapper(uint32_t *frame, uint32_t pid, uint32_t wstatus_ptr,
                                    uint32_t options, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_waitpid((int32_t)pid, (uint32_t *)wstatus_ptr, options);
}

uint32_t syscall_dispatcher(uint32_t syscall_num, uint32_t p1, uint32_t p2, 
                            uint32_t p3, uint32_t p4, uint32_t p5, uint32_t *frame) {
    /* 检查系统调用号是否在有效范围内 */
    if (syscall_num >= SYS_MAX) {
        LOG_WARN_MSG("Invalid syscall number: %u (out of range)\n", syscall_num);
        return (uint32_t)-1;
    }
    
    syscall_handler_t handler = syscall_table[syscall_num];
    if (handler == NULL) {
        LOG_WARN_MSG("Unimplemented syscall: %u\n", syscall_num);
        return (uint32_t)-1;
    }
    
    return handler(frame, p1, p2, p3, p4, p5);
}

/**
 * 初始化系统调用
 */
void syscall_init(void) {
    LOG_INFO_MSG("Initializing system calls...\n");
    
    /* 清空系统调用表 */
    for (uint32_t i = 0; i < SYS_MAX; i++) {
        syscall_table[i] = NULL;
    }
    
    /* ========================================================================
     * 注册系统调用包装器函数
     * ======================================================================== */
    
    /* 进程生命周期 */
    syscall_table[SYS_EXIT]        = sys_exit_wrapper;   
    syscall_table[SYS_FORK]        = sys_fork_wrapper;   
    syscall_table[SYS_EXECVE]      = sys_execve_wrapper;
    syscall_table[SYS_WAITPID]     = sys_waitpid_wrapper;
    syscall_table[SYS_GETPID]      = sys_getpid_wrapper;
    syscall_table[SYS_GETPPID]     = sys_getppid_wrapper;
    syscall_table[SYS_SCHED_YIELD] = sys_yield_wrapper;
    
    /* 信号与进程控制 */
    syscall_table[SYS_KILL]        = sys_kill_wrapper;
    
    /* 文件系统操作 */
    syscall_table[SYS_OPEN]        = sys_open_wrapper;   
    syscall_table[SYS_CLOSE]       = sys_close_wrapper;  
    syscall_table[SYS_READ]        = sys_read_wrapper;   
    syscall_table[SYS_WRITE]       = sys_write_wrapper;  
    syscall_table[SYS_LSEEK]       = sys_lseek_wrapper;
    syscall_table[SYS_MKDIR]       = sys_mkdir_wrapper;  
    syscall_table[SYS_UNLINK]      = sys_unlink_wrapper; 
    syscall_table[SYS_GETCWD]      = sys_getcwd_wrapper;
    syscall_table[SYS_CHDIR]       = sys_chdir_wrapper;
    syscall_table[SYS_GETDENTS]    = sys_getdents_wrapper;
    
    /* 时间相关 */
    syscall_table[SYS_TIME]        = sys_time_wrapper;
    syscall_table[SYS_NANOSLEEP]   = sys_nanosleep_wrapper;
    
    /* 杂项 / 系统控制 */
    syscall_table[SYS_REBOOT]      = sys_reboot_wrapper;
    syscall_table[SYS_POWEROFF]    = sys_poweroff_wrapper;
    
    /* 注册 INT 0x80 处理程序 */
    idt_set_gate(0x80, (uint32_t)syscall_handler, GDT_KERNEL_CODE_SEGMENT, IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_TRAP);
    
    LOG_INFO_MSG("System calls initialized\n");
}
