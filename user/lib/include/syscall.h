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
    SYS_IPC_SEND        = 14,
    SYS_IPC_RECV        = 15,
    SYS_IPC_CALL        = 16,
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

// ============================================================================
// 进程间通信：同步、定长消息、按 PID 寻址
//
// 服务进程:  for (;;) { ipc_recv(IPC_ANY, &m); ...; ipc_send(m.sender, &m); }
// 客户进程:  ipc_call(server_pid, &m);   // 请求放在 m 里，应答写回 m
// ============================================================================

#define IPC_ANY         0
#define IPC_MSG_WORDS   6

struct ipc_msg {
    uint32_t sender;                // 发送者 PID，由内核填写
    uint32_t label;                 // 请求/应答类型，由通信双方约定
    uint64_t data[IPC_MSG_WORDS];
};

/** 发送并阻塞到对方收下；目标不存在或已退出返回 -1 */
int ipc_send(int dest, const struct ipc_msg *msg);
/** 接收一条消息；from 为 IPC_ANY 或指定 PID */
int ipc_recv(int from, struct ipc_msg *msg);
/** 发送请求并等待 dest 的应答（写回 *msg） */
int ipc_call(int dest, struct ipc_msg *msg);

#endif // _USERLAND_LIB_SYSCALL_H_
