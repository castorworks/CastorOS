#ifndef _USERLAND_LIB_SYSCALL_H_
#define _USERLAND_LIB_SYSCALL_H_

#include <types.h>

// ============================================================================
// 系统调用号（与内核 src/include/kernel/syscall.h 保持一致）
// ============================================================================

enum {
    SYS_EXIT            = 0,
    SYS_FORK            = 1,
    SYS_EXEC            = 2,
    SYS_WAITPID         = 3,
    SYS_GETPID          = 4,
    SYS_GETPPID         = 5,
    SYS_SCHED_YIELD     = 6,
    SYS_KILL            = 7,
    SYS_NANOSLEEP       = 8,
    SYS_BRK             = 9,
    SYS_MMAP            = 10,
    SYS_MUNMAP          = 11,
    SYS_CONSOLE_WRITE   = 12,
    SYS_CONSOLE_READ    = 13,
};

typedef uintptr_t syscall_arg_t;

// 架构相关的陷入指令封装（src/arch/<arch>/syscall.S）
extern "C" syscall_arg_t syscall0(syscall_arg_t num);
extern "C" syscall_arg_t syscall1(syscall_arg_t num, syscall_arg_t arg0);
extern "C" syscall_arg_t syscall2(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1);
extern "C" syscall_arg_t syscall3(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2);
extern "C" syscall_arg_t syscall4(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2, syscall_arg_t arg3);
extern "C" syscall_arg_t syscall5(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2, syscall_arg_t arg3, syscall_arg_t arg4);
extern "C" syscall_arg_t syscall6(syscall_arg_t num, syscall_arg_t arg0, syscall_arg_t arg1,
                       syscall_arg_t arg2, syscall_arg_t arg3, syscall_arg_t arg4,
                       syscall_arg_t arg5);

// ============================================================================
// 进程
// ============================================================================

void exit(int status) __attribute__((noreturn));
int fork(void);
/** 用内存中的 ELF 映像替换当前进程；成功不返回 */
int exec(const void *image, size_t size);
int waitpid(int pid, int *wstatus, int options);
int wait(int *wstatus);
int getpid(void);
int getppid(void);
void yield(void);
int kill(int pid, int signal);
int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

// ============================================================================
// 内存
// ============================================================================

void *brk(void *addr);
void *sbrk(int increment);
/** 只支持匿名映射：flags 必须包含 MAP_ANONYMOUS，fd 传 -1 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);

// ============================================================================
// 调试控制台（串口）
// ============================================================================

ssize_t console_write(const void *buf, size_t count);
/** 非阻塞：返回读到的字节数，没有输入时返回 0 */
ssize_t console_read(void *buf, size_t count);

#endif // _USERLAND_LIB_SYSCALL_H_
