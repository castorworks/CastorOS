// ============================================================================
// syscall.c - System Call Dispatcher (Architecture-Independent)
// ============================================================================
//
// This file implements the architecture-independent system call dispatcher.
// The actual system call entry mechanism is implemented in architecture-specific
// code under src/arch/{arch}/syscall/.
//
// **Feature: multi-arch-support**
// **Validates: Requirements 8.1**
// ============================================================================

#include <kernel/syscall.h>
#include <kernel/syscalls/fs.h>
#include <kernel/syscalls/process.h>
#include <kernel/syscalls/time.h>
#include <kernel/syscalls/system.h>
#include <kernel/syscalls/mm.h>
#if !defined(ARCH_ARM64)
#include <kernel/syscalls/net.h>
#include <net/socket.h>
#endif
#include <kernel/utsname.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* 系统调用处理函数表 - 使用 syscall_arg_t 支持 32/64 位架构 */
typedef syscall_arg_t (*syscall_handler_t)(syscall_arg_t*, syscall_arg_t, syscall_arg_t, 
                                           syscall_arg_t, syscall_arg_t, syscall_arg_t);

static syscall_handler_t syscall_table[SYS_MAX];

/* 栈帧布局（架构相关）：
 * i686 (syscall_handler 中 "mov ebp, esp" 后)：
 *   frame[0]  = DS
 *   frame[1]  = EAX (syscall_num)
 *   frame[2]  = EBX (arg1)
 *   ...
 *   frame[12] = SS (IRET)
 * 
 * x86_64 (syscall_entry 中保存的寄存器)：
 *   frame[0]  = r15
 *   frame[1]  = r14
 *   ...
 *   frame[15] = user_rsp
 */

/* 默认时间片（与 task.c 保持一致） */
#define DEFAULT_TIME_SLICE 10

/**
 * sys_exit_wrapper - 退出进程（系统调用包装器）
 */
static syscall_arg_t sys_exit_wrapper(syscall_arg_t *frame, syscall_arg_t exit_code, 
                                      syscall_arg_t p2, syscall_arg_t p3, 
                                      syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame;
    (void)p2; (void)p3; (void)p4; (void)p5;
    sys_exit((uint32_t)exit_code);
    return 0;  // 永远不会返回
}

/**
 * sys_fork_wrapper - 创建子进程（系统调用包装器）
 */
static syscall_arg_t sys_fork_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    
    return sys_fork(frame);
}

/**
 * sys_execve_wrapper - 执行新程序（系统调用包装器）
 */
static syscall_arg_t sys_execve_wrapper(syscall_arg_t *frame, syscall_arg_t path_addr, 
                                        syscall_arg_t argv, syscall_arg_t envp, 
                                        syscall_arg_t p4, syscall_arg_t p5) {
    (void)argv; (void)envp; (void)p4; (void)p5;
    
    const char *user_path = (const char *)(uintptr_t)path_addr;
    if (!user_path) {
        return (syscall_arg_t)-1;
    }
    
    // 将路径从用户空间复制到内核空间
    char path[256];
    size_t i;
    
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
/* 系统调用包装器函数 - 使用 syscall_arg_t 支持 32/64 位 */

static syscall_arg_t sys_open_wrapper(syscall_arg_t *frame, syscall_arg_t path, syscall_arg_t flags, 
                                      syscall_arg_t mode, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_open((const char *)(uintptr_t)path, (int32_t)flags, (uint32_t)mode);
}

static syscall_arg_t sys_close_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t p2, 
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    // Sign-extend the 32-bit result to 64-bit for proper error handling
    return (syscall_arg_t)(int32_t)sys_close((int32_t)fd);
}

static syscall_arg_t sys_read_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t buffer, 
                                      syscall_arg_t size, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_read((int32_t)fd, (void *)(uintptr_t)buffer, (size_t)size);
}

static syscall_arg_t sys_write_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t buffer, 
                                       syscall_arg_t size, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_write((int32_t)fd, (const void *)(uintptr_t)buffer, (size_t)size);
}

static syscall_arg_t sys_lseek_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t offset, 
                                       syscall_arg_t whence, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_lseek((int32_t)fd, (int32_t)offset, (int32_t)whence);
}

static syscall_arg_t sys_mkdir_wrapper(syscall_arg_t *frame, syscall_arg_t path, syscall_arg_t mode, 
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_mkdir((const char *)(uintptr_t)path, (uint32_t)mode);
}

static syscall_arg_t sys_unlink_wrapper(syscall_arg_t *frame, syscall_arg_t path, syscall_arg_t p2, 
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_unlink((const char *)(uintptr_t)path);
}

static syscall_arg_t sys_chdir_wrapper(syscall_arg_t *frame, syscall_arg_t path, syscall_arg_t p2, 
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_chdir((const char *)(uintptr_t)path);
}

static syscall_arg_t sys_getcwd_wrapper(syscall_arg_t *frame, syscall_arg_t buffer, syscall_arg_t size, 
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_getcwd((char *)(uintptr_t)buffer, (size_t)size);
}

static syscall_arg_t sys_getdents_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t index, 
                                          syscall_arg_t dirent, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_getdents((int32_t)fd, (uint32_t)index, (void *)(uintptr_t)dirent);
}

static syscall_arg_t sys_stat_wrapper(syscall_arg_t *frame, syscall_arg_t path, syscall_arg_t buf, 
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_stat((const char *)(uintptr_t)path, (struct stat *)(uintptr_t)buf);
}

static syscall_arg_t sys_fstat_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t buf, 
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_fstat((int32_t)fd, (struct stat *)(uintptr_t)buf);
}

static syscall_arg_t sys_ftruncate_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t length, 
                                           syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_ftruncate((int32_t)fd, (uint32_t)length);
}

static syscall_arg_t sys_pipe_wrapper(syscall_arg_t *frame, syscall_arg_t fds, syscall_arg_t p2, 
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_pipe((int32_t *)(uintptr_t)fds);
}

static syscall_arg_t sys_dup_wrapper(syscall_arg_t *frame, syscall_arg_t oldfd, syscall_arg_t p2, 
                                     syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_dup((int32_t)oldfd);
}

static syscall_arg_t sys_dup2_wrapper(syscall_arg_t *frame, syscall_arg_t oldfd, syscall_arg_t newfd, 
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_dup2((int32_t)oldfd, (int32_t)newfd);
}

#if defined(ARCH_ARM64)
/* ARM64: stub for sys_ioctl (network ioctl not supported yet) */
static int32_t sys_ioctl_stub(int32_t fd, uint32_t request, void *argp) {
    (void)fd; (void)request; (void)argp;
    return -38;  /* -ENOSYS */
}
#define sys_ioctl sys_ioctl_stub
#endif

static syscall_arg_t sys_ioctl_wrapper(syscall_arg_t *frame, syscall_arg_t fd, syscall_arg_t request, 
                                       syscall_arg_t argp, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_ioctl((int32_t)fd, (uint32_t)request, (void *)(uintptr_t)argp);
}

static syscall_arg_t sys_getpid_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_getpid();
}

static syscall_arg_t sys_getppid_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                         syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_getppid();
}

static syscall_arg_t sys_yield_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_yield();
}

static syscall_arg_t sys_nanosleep_wrapper(syscall_arg_t *frame, syscall_arg_t req_ptr, 
                                           syscall_arg_t rem_ptr, syscall_arg_t p3,
                                           syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    const struct timespec *req = (const struct timespec *)(uintptr_t)req_ptr;
    struct timespec *rem = (struct timespec *)(uintptr_t)rem_ptr;
    return sys_nanosleep(req, rem);
}

static syscall_arg_t sys_time_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_time();
}

static syscall_arg_t sys_reboot_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    sys_reboot();
    return 0;
}

static syscall_arg_t sys_poweroff_wrapper(syscall_arg_t *frame, syscall_arg_t p1, syscall_arg_t p2, 
                                          syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    sys_poweroff();
    return 0;
}

static syscall_arg_t sys_kill_wrapper(syscall_arg_t *frame, syscall_arg_t pid, syscall_arg_t signal,
                                      syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_kill((uint32_t)pid, (uint32_t)signal);
}

static syscall_arg_t sys_waitpid_wrapper(syscall_arg_t *frame, syscall_arg_t pid, syscall_arg_t wstatus_ptr,
                                         syscall_arg_t options, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return sys_waitpid((int32_t)pid, (uint32_t *)(uintptr_t)wstatus_ptr, (uint32_t)options);
}

static syscall_arg_t sys_brk_wrapper(syscall_arg_t *frame, syscall_arg_t addr, syscall_arg_t p2,
                                     syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_brk((uintptr_t)addr);
}

static syscall_arg_t sys_mmap_wrapper(syscall_arg_t *frame, syscall_arg_t addr, syscall_arg_t length,
                                      syscall_arg_t prot, syscall_arg_t flags, syscall_arg_t fd) {
    // frame[7] 是用户态传递的 ebp/rbp，我们用它作为第 6 个参数 (offset)
    // 用户态需要在调用 int 0x80/syscall 前将 offset 放入 ebp/rbp
    syscall_arg_t offset = frame[7];
    return sys_mmap((uintptr_t)addr, (size_t)length, (uint32_t)prot, (uint32_t)flags, 
                    (int32_t)fd, (uint32_t)offset);
}

static syscall_arg_t sys_munmap_wrapper(syscall_arg_t *frame, syscall_arg_t addr, syscall_arg_t length,
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_munmap((uintptr_t)addr, (size_t)length);
}

static syscall_arg_t sys_uname_wrapper(syscall_arg_t *frame, syscall_arg_t buf, syscall_arg_t p2,
                                       syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p2; (void)p3; (void)p4; (void)p5;
    return sys_uname((struct utsname *)(uintptr_t)buf);
}

static syscall_arg_t sys_rename_wrapper(syscall_arg_t *frame, syscall_arg_t oldpath, syscall_arg_t newpath,
                                        syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return sys_rename((const char *)(uintptr_t)oldpath, (const char *)(uintptr_t)newpath);
}

/* ============================================================================
 * BSD Socket API 系统调用包装器 - 使用 syscall_arg_t 支持 32/64 位
 * ARM64 暂不支持网络功能
 * ============================================================================ */

#if !defined(ARCH_ARM64)
static syscall_arg_t sys_socket_wrapper(syscall_arg_t *frame, syscall_arg_t domain, 
                                        syscall_arg_t type, syscall_arg_t protocol, 
                                        syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_socket((int)domain, (int)type, (int)protocol);
}

static syscall_arg_t sys_bind_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                      syscall_arg_t addr, syscall_arg_t addrlen, 
                                      syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_bind((int)sockfd, (const struct sockaddr *)(uintptr_t)addr, 
                                   (socklen_t)addrlen);
}

static syscall_arg_t sys_listen_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                        syscall_arg_t backlog, syscall_arg_t p3, 
                                        syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return (syscall_arg_t)sys_listen((int)sockfd, (int)backlog);
}

static syscall_arg_t sys_accept_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                        syscall_arg_t addr, syscall_arg_t addrlen, 
                                        syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_accept((int)sockfd, (struct sockaddr *)(uintptr_t)addr, 
                                     (socklen_t *)(uintptr_t)addrlen);
}

static syscall_arg_t sys_connect_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                         syscall_arg_t addr, syscall_arg_t addrlen, 
                                         syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_connect((int)sockfd, (const struct sockaddr *)(uintptr_t)addr, 
                                      (socklen_t)addrlen);
}

static syscall_arg_t sys_send_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                      syscall_arg_t buf, syscall_arg_t len, 
                                      syscall_arg_t flags, syscall_arg_t p5) {
    (void)frame; (void)p5;
    return (syscall_arg_t)sys_send((int)sockfd, (const void *)(uintptr_t)buf, 
                                   (size_t)len, (int)flags);
}

static syscall_arg_t sys_sendto_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                        syscall_arg_t buf, syscall_arg_t len, 
                                        syscall_arg_t flags, syscall_arg_t dest_addr) {
    // 第 6 个参数 addrlen 通过 frame[7] 传递
    syscall_arg_t addrlen = frame[7];
    return (syscall_arg_t)sys_sendto((int)sockfd, (const void *)(uintptr_t)buf, 
                                     (size_t)len, (int)flags,
                                     (const struct sockaddr *)(uintptr_t)dest_addr, 
                                     (socklen_t)addrlen);
}

static syscall_arg_t sys_recv_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                      syscall_arg_t buf, syscall_arg_t len, 
                                      syscall_arg_t flags, syscall_arg_t p5) {
    (void)frame; (void)p5;
    return (syscall_arg_t)sys_recv((int)sockfd, (void *)(uintptr_t)buf, 
                                   (size_t)len, (int)flags);
}

static syscall_arg_t sys_recvfrom_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                          syscall_arg_t buf, syscall_arg_t len, 
                                          syscall_arg_t flags, syscall_arg_t src_addr) {
    // 第 6 个参数 addrlen 指针通过 frame[7] 传递
    socklen_t *addrlen = (socklen_t *)(uintptr_t)frame[7];
    return (syscall_arg_t)sys_recvfrom((int)sockfd, (void *)(uintptr_t)buf, 
                                       (size_t)len, (int)flags,
                                       (struct sockaddr *)(uintptr_t)src_addr, addrlen);
}

static syscall_arg_t sys_shutdown_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                          syscall_arg_t how, syscall_arg_t p3, 
                                          syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p3; (void)p4; (void)p5;
    return (syscall_arg_t)sys_shutdown((int)sockfd, (int)how);
}

static syscall_arg_t sys_setsockopt_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                            syscall_arg_t level, syscall_arg_t optname, 
                                            syscall_arg_t optval, syscall_arg_t optlen) {
    (void)frame;
    return (syscall_arg_t)sys_setsockopt((int)sockfd, (int)level, (int)optname,
                                         (const void *)(uintptr_t)optval, (socklen_t)optlen);
}

static syscall_arg_t sys_getsockopt_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                            syscall_arg_t level, syscall_arg_t optname, 
                                            syscall_arg_t optval, syscall_arg_t optlen) {
    (void)frame;
    return (syscall_arg_t)sys_getsockopt((int)sockfd, (int)level, (int)optname,
                                         (void *)(uintptr_t)optval, 
                                         (socklen_t *)(uintptr_t)optlen);
}

static syscall_arg_t sys_getsockname_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                             syscall_arg_t addr, syscall_arg_t addrlen, 
                                             syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_getsockname((int)sockfd, (struct sockaddr *)(uintptr_t)addr, 
                                          (socklen_t *)(uintptr_t)addrlen);
}

static syscall_arg_t sys_getpeername_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                             syscall_arg_t addr, syscall_arg_t addrlen, 
                                             syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_getpeername((int)sockfd, (struct sockaddr *)(uintptr_t)addr, 
                                          (socklen_t *)(uintptr_t)addrlen);
}

static syscall_arg_t sys_select_wrapper(syscall_arg_t *frame, syscall_arg_t nfds, 
                                        syscall_arg_t readfds, syscall_arg_t writefds, 
                                        syscall_arg_t exceptfds, syscall_arg_t timeout) {
    (void)frame;
    return (syscall_arg_t)sys_select((int)nfds, (fd_set *)(uintptr_t)readfds, 
                                     (fd_set *)(uintptr_t)writefds,
                                     (fd_set *)(uintptr_t)exceptfds, 
                                     (struct timeval *)(uintptr_t)timeout);
}

static syscall_arg_t sys_fcntl_wrapper(syscall_arg_t *frame, syscall_arg_t sockfd, 
                                       syscall_arg_t cmd, syscall_arg_t arg, 
                                       syscall_arg_t p4, syscall_arg_t p5) {
    (void)frame; (void)p4; (void)p5;
    return (syscall_arg_t)sys_fcntl((int)sockfd, (int)cmd, (int)arg);
}
#endif /* !ARCH_ARM64 */

/* Debug counter for syscalls */
static uint32_t syscall_count = 0;

syscall_arg_t syscall_dispatcher(syscall_arg_t syscall_num, syscall_arg_t p1, syscall_arg_t p2, 
                                 syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5, 
                                 syscall_arg_t *frame) {
    
#if defined(ARCH_ARM64)
    /* Debug: Print first few syscalls */
    if (syscall_count < 10) {
        LOG_INFO_MSG("ARM64 syscall: num=%lu, p1=0x%llx, p2=0x%llx\n",
                     (unsigned long)syscall_num,
                     (unsigned long long)p1,
                     (unsigned long long)p2);
        syscall_count++;
    }
#endif
    
    /* 检查系统调用号是否在有效范围内 */
    if (syscall_num >= SYS_MAX) {
        LOG_WARN_MSG("Invalid syscall number: %lu (out of range)\n", (unsigned long)syscall_num);
        return (syscall_arg_t)-1;
    }
    
    syscall_handler_t handler = syscall_table[syscall_num];
    if (handler == NULL) {
        LOG_WARN_MSG("Unimplemented syscall: %lu\n", (unsigned long)syscall_num);
        return (syscall_arg_t)-1;
    }
    
    return handler(frame, p1, p2, p3, p4, p5);
}

/**
 * syscall_init - Initialize system call subsystem
 *
 * This function initializes the system call table and sets up the
 * architecture-specific system call entry mechanism through the HAL.
 *
 * Requirements: 8.1 - System call entry mechanism
 */
void syscall_init(void) {
    LOG_INFO_MSG("Initializing system calls...\n");
    
    /* Clear system call table */
    for (uint32_t i = 0; i < SYS_MAX; i++) {
        syscall_table[i] = NULL;
    }
    
    /* ========================================================================
     * Register system call wrapper functions
     * ======================================================================== */
    
    /* Process lifecycle */
    syscall_table[SYS_EXIT]        = sys_exit_wrapper;   
    syscall_table[SYS_FORK]        = sys_fork_wrapper;   
    syscall_table[SYS_EXECVE]      = sys_execve_wrapper;
    syscall_table[SYS_WAITPID]     = sys_waitpid_wrapper;
    syscall_table[SYS_GETPID]      = sys_getpid_wrapper;
    syscall_table[SYS_GETPPID]     = sys_getppid_wrapper;
    syscall_table[SYS_SCHED_YIELD] = sys_yield_wrapper;
    
    /* Signal and process control */
    syscall_table[SYS_KILL]        = sys_kill_wrapper;
    
    /* File system operations */
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
    
    /* Time related */
    syscall_table[SYS_TIME]        = sys_time_wrapper;
    syscall_table[SYS_NANOSLEEP]   = sys_nanosleep_wrapper;
    
    /* Memory management */
    syscall_table[SYS_BRK]         = sys_brk_wrapper;
    syscall_table[SYS_MMAP]        = sys_mmap_wrapper;
    syscall_table[SYS_MUNMAP]      = sys_munmap_wrapper;
    
    /* Miscellaneous / System control */
    syscall_table[SYS_REBOOT]      = sys_reboot_wrapper;
    syscall_table[SYS_POWEROFF]    = sys_poweroff_wrapper;
    syscall_table[SYS_UNAME]       = sys_uname_wrapper;
    
    /* BSD Socket API (not available on ARM64 yet) */
#if !defined(ARCH_ARM64)
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
#endif
    
    /* Initialize architecture-specific system call entry mechanism via HAL */
    hal_syscall_init(NULL);
    
#if defined(ARCH_ARM64)
    LOG_INFO_MSG("System calls initialized (network syscalls not available on ARM64)\n");
#else
    LOG_INFO_MSG("System calls initialized (POSIX-compliant BSD Socket API enabled)\n");
#endif
}
