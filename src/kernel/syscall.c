// ============================================================================
// syscall.c - 系统调用实现
// ============================================================================

#include <kernel/syscall.h>
#include <kernel/syscalls/fs.h>
#include <kernel/syscalls/process.h>
#include <kernel/syscalls/time.h>
#include <kernel/syscalls/system.h>
#include <kernel/syscalls/mm.h>
#include <kernel/syscalls/net.h>
#include <kernel/utsname.h>
#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <kernel/isr.h>
#include <net/socket.h>
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

static uint32_t sys_stat_wrapper(uint32_t *frame, uint32_t path, uint32_t buf, uint32_t p3,
                                 uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_stat((const char *)path, (struct stat *)buf);
}

static uint32_t sys_fstat_wrapper(uint32_t *frame, uint32_t fd, uint32_t buf, uint32_t p3,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_fstat((int32_t)fd, (struct stat *)buf);
}

static uint32_t sys_ftruncate_wrapper(uint32_t *frame, uint32_t fd, uint32_t length, uint32_t p3,
                                      uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_ftruncate((int32_t)fd, length);
}

static uint32_t sys_pipe_wrapper(uint32_t *frame, uint32_t fds, uint32_t p2, uint32_t p3,
                                 uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_pipe((int32_t *)fds);
}

static uint32_t sys_dup_wrapper(uint32_t *frame, uint32_t oldfd, uint32_t p2, uint32_t p3,
                                uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_dup((int32_t)oldfd);
}

static uint32_t sys_dup2_wrapper(uint32_t *frame, uint32_t oldfd, uint32_t newfd, uint32_t p3,
                                 uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_dup2((int32_t)oldfd, (int32_t)newfd);
}

static uint32_t sys_ioctl_wrapper(uint32_t *frame, uint32_t fd, uint32_t request, uint32_t argp,
                                  uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_ioctl((int32_t)fd, request, (void *)argp);
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

static uint32_t sys_brk_wrapper(uint32_t *frame, uint32_t addr, uint32_t p2,
                                uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_brk(addr);
}

static uint32_t sys_mmap_wrapper(uint32_t *frame, uint32_t addr, uint32_t length,
                                 uint32_t prot, uint32_t flags, uint32_t fd) {
    // frame[7] 是用户态传递的 ebp，我们用它作为第 6 个参数 (offset)
    // 用户态需要在调用 int 0x80 前将 offset 放入 ebp
    uint32_t offset = frame[7];
    return sys_mmap(addr, length, prot, flags, (int32_t)fd, offset);
}

static uint32_t sys_munmap_wrapper(uint32_t *frame, uint32_t addr, uint32_t length,
                                   uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_munmap(addr, length);
}

static uint32_t sys_uname_wrapper(uint32_t *frame, uint32_t buf, uint32_t p2,
                                  uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_uname((struct utsname *)buf);
}

static uint32_t sys_rename_wrapper(uint32_t *frame, uint32_t oldpath, uint32_t newpath,
                                   uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_rename((const char *)oldpath, (const char *)newpath);
}

/* ============================================================================
 * BSD Socket API 系统调用包装器
 * ============================================================================ */

static uint32_t sys_socket_wrapper(uint32_t *frame, uint32_t domain, uint32_t type, 
                                   uint32_t protocol, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_socket((int)domain, (int)type, (int)protocol);
}

static uint32_t sys_bind_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t addr, 
                                 uint32_t addrlen, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_bind((int)sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
}

static uint32_t sys_listen_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t backlog, 
                                   uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return (uint32_t)sys_listen((int)sockfd, (int)backlog);
}

static uint32_t sys_accept_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t addr, 
                                   uint32_t addrlen, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_accept((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
}

static uint32_t sys_connect_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t addr, 
                                    uint32_t addrlen, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_connect((int)sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
}

static uint32_t sys_send_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t buf, 
                                 uint32_t len, uint32_t flags, uint32_t p5) {
    (void)frame; (void)p5;
    return (uint32_t)sys_send((int)sockfd, (const void *)buf, (size_t)len, (int)flags);
}

static uint32_t sys_sendto_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t buf, 
                                   uint32_t len, uint32_t flags, uint32_t dest_addr) {
    // 第 6 个参数 addrlen 通过 frame[7] 传递
    uint32_t addrlen = frame[7];
    return (uint32_t)sys_sendto((int)sockfd, (const void *)buf, (size_t)len, (int)flags,
                                (const struct sockaddr *)dest_addr, (socklen_t)addrlen);
}

static uint32_t sys_recv_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t buf, 
                                 uint32_t len, uint32_t flags, uint32_t p5) {
    (void)frame; (void)p5;
    return (uint32_t)sys_recv((int)sockfd, (void *)buf, (size_t)len, (int)flags);
}

static uint32_t sys_recvfrom_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t buf, 
                                     uint32_t len, uint32_t flags, uint32_t src_addr) {
    // 第 6 个参数 addrlen 指针通过 frame[7] 传递
    socklen_t *addrlen = (socklen_t *)frame[7];
    return (uint32_t)sys_recvfrom((int)sockfd, (void *)buf, (size_t)len, (int)flags,
                                  (struct sockaddr *)src_addr, addrlen);
}

static uint32_t sys_shutdown_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t how, 
                                     uint32_t p3, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return (uint32_t)sys_shutdown((int)sockfd, (int)how);
}

static uint32_t sys_setsockopt_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t level, 
                                       uint32_t optname, uint32_t optval, uint32_t optlen) {
    (void)frame;
    return (uint32_t)sys_setsockopt((int)sockfd, (int)level, (int)optname,
                                    (const void *)optval, (socklen_t)optlen);
}

static uint32_t sys_getsockopt_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t level, 
                                       uint32_t optname, uint32_t optval, uint32_t optlen) {
    (void)frame;
    return (uint32_t)sys_getsockopt((int)sockfd, (int)level, (int)optname,
                                    (void *)optval, (socklen_t *)optlen);
}

static uint32_t sys_getsockname_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t addr, 
                                        uint32_t addrlen, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_getsockname((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
}

static uint32_t sys_getpeername_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t addr, 
                                        uint32_t addrlen, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_getpeername((int)sockfd, (struct sockaddr *)addr, (socklen_t *)addrlen);
}

static uint32_t sys_select_wrapper(uint32_t *frame, uint32_t nfds, uint32_t readfds, 
                                   uint32_t writefds, uint32_t exceptfds, uint32_t timeout) {
    (void)frame;
    return (uint32_t)sys_select((int)nfds, (fd_set *)readfds, (fd_set *)writefds,
                                (fd_set *)exceptfds, (struct timeval *)timeout);
}

static uint32_t sys_fcntl_wrapper(uint32_t *frame, uint32_t sockfd, uint32_t cmd, 
                                  uint32_t arg, uint32_t p4, uint32_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (uint32_t)sys_fcntl((int)sockfd, (int)cmd, (int)arg);
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
    syscall_table[SYS_STAT]        = sys_stat_wrapper;
    syscall_table[SYS_FSTAT]       = sys_fstat_wrapper;
    syscall_table[SYS_MKDIR]       = sys_mkdir_wrapper;  
    syscall_table[SYS_UNLINK]      = sys_unlink_wrapper;
    syscall_table[SYS_RENAME]      = sys_rename_wrapper; 
    syscall_table[SYS_GETCWD]      = sys_getcwd_wrapper;
    syscall_table[SYS_CHDIR]       = sys_chdir_wrapper;
    syscall_table[SYS_GETDENTS]    = sys_getdents_wrapper;
    syscall_table[SYS_FTRUNCATE]   = sys_ftruncate_wrapper;
    syscall_table[SYS_PIPE]        = sys_pipe_wrapper;
    syscall_table[SYS_DUP]         = sys_dup_wrapper;
    syscall_table[SYS_DUP2]        = sys_dup2_wrapper;
    syscall_table[SYS_IOCTL]       = sys_ioctl_wrapper;
    
    /* 时间相关 */
    syscall_table[SYS_TIME]        = sys_time_wrapper;
    syscall_table[SYS_NANOSLEEP]   = sys_nanosleep_wrapper;
    
    /* 内存管理 */
    syscall_table[SYS_BRK]         = sys_brk_wrapper;
    syscall_table[SYS_MMAP]        = sys_mmap_wrapper;
    syscall_table[SYS_MUNMAP]      = sys_munmap_wrapper;
    
    /* 杂项 / 系统控制 */
    syscall_table[SYS_REBOOT]      = sys_reboot_wrapper;
    syscall_table[SYS_POWEROFF]    = sys_poweroff_wrapper;
    syscall_table[SYS_UNAME]       = sys_uname_wrapper;
    
    /* BSD Socket API */
    syscall_table[SYS_SOCKET]      = sys_socket_wrapper;
    syscall_table[SYS_BIND]        = sys_bind_wrapper;
    syscall_table[SYS_LISTEN]      = sys_listen_wrapper;
    syscall_table[SYS_ACCEPT]      = sys_accept_wrapper;
    syscall_table[SYS_CONNECT]     = sys_connect_wrapper;
    syscall_table[SYS_SEND]        = sys_send_wrapper;
    syscall_table[SYS_SENDTO]      = sys_sendto_wrapper;
    syscall_table[SYS_RECV]        = sys_recv_wrapper;
    syscall_table[SYS_RECVFROM]    = sys_recvfrom_wrapper;
    syscall_table[SYS_SHUTDOWN]    = sys_shutdown_wrapper;
    syscall_table[SYS_SETSOCKOPT]  = sys_setsockopt_wrapper;
    syscall_table[SYS_GETSOCKOPT]  = sys_getsockopt_wrapper;
    syscall_table[SYS_GETSOCKNAME] = sys_getsockname_wrapper;
    syscall_table[SYS_GETPEERNAME] = sys_getpeername_wrapper;
    syscall_table[SYS_SELECT]      = sys_select_wrapper;
    syscall_table[SYS_FCNTL]       = sys_fcntl_wrapper;
    
    /* 注册 INT 0x80 处理程序 */
    idt_set_gate(0x80, (uint32_t)syscall_handler, GDT_KERNEL_CODE_SEGMENT, IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_TRAP);
    
    LOG_INFO_MSG("System calls initialized (POSIX-compliant BSD Socket API enabled)\n");
}
