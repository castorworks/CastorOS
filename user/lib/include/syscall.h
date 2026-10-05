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
    SYS_IPC_SEND        = 13,
    SYS_IPC_RECV        = 14,
    SYS_IPC_CALL        = 15,
    SYS_IPC_REPLY       = 16,
    SYS_MEM_GRANT       = 17,
    SYS_IO_READ         = 18,
    SYS_IO_WRITE        = 19,
    SYS_MAP_DEVICE      = 20,
    SYS_IRQ_CLAIM       = 21,
    SYS_IRQ_ACK         = 22,
    SYS_DROP_PRIVILEGE  = 23,
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

/**
 * 把自己的 [addr, addr+length) 共享给进程 pid：之后两个进程读写的是同一批物理页。
 * addr 必须页对齐，区间必须已映射且可写（例如 mmap 得到的内存）。
 * 内核给对方发一条 label == IPC_LABEL_GRANT 的消息告诉它映射的位置
 * （data[0] 地址，data[1] 长度，sender 是调用者）；调用阻塞到对方收下为止。
 * @return 0 成功，-1 失败
 */
int mem_grant(int pid, void *addr, size_t length);

// ============================================================================
// 调试输出（内核串口控制台）
// ============================================================================

ssize_t console_write(const void *buf, size_t count);

// ============================================================================
// 进程间通信：同步、定长消息、按 PID 寻址
//
// 服务进程:  for (;;) { ipc_recv(IPC_ANY, &m); ...; ipc_reply(m.sender, &m); }
// 客户进程:  ipc_call(server_pid, &m);   // 请求放在 m 里，应答写回 m
// ============================================================================

#define IPC_ANY         0
#define IPC_KERNEL      0       // 内核发来的消息的 sender
// 最高位为 1 的 label 保留给内核：用户进程发不出，收到就说明内容是内核担保的
#define IPC_LABEL_RESERVED  0x80000000u
#define IPC_LABEL_IRQ       0x80000001u     // sender == IPC_KERNEL：设备中断，data[0] 是中断号
#define IPC_LABEL_GRANT     0x80000002u     // sender 用 mem_grant 共享来一段内存：data[0] 地址，data[1] 长度
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
/** 应答正在 ipc_call 自己的进程。从不阻塞；对方已不在等待时返回 -1 */
int ipc_reply(int dest, const struct ipc_msg *msg);

// ============================================================================
// 硬件访问（仅特权进程）
//
// 特权从 init 开始，fork 和 exec 都保留，drop_privilege 之后永久失去。
// ============================================================================

/** 读/写 x86 的 I/O 端口，width 是 1、2 或 4 字节。arm64 没有端口，恒返回 -1 */
int io_read(uintptr_t port, int width, uint32_t *value);
int io_write(uintptr_t port, int width, uint32_t value);

/**
 * 把设备内存 [phys, phys+length) 映射进自己的地址空间（不缓存）。phys 必须页对齐，
 * 且不能是普通内存。
 * @return 映射的地址；失败返回 MAP_FAILED
 */
void *map_device(uintptr_t phys, size_t length);

/**
 * 认领一条设备中断线。之后每次中断，内核屏蔽这条线并发来一条
 * sender == IPC_KERNEL、label == IPC_LABEL_IRQ、data[0] == irq 的消息；
 * 处理完设备后调用 irq_ack 重新打开它。
 */
int irq_claim(int irq);
int irq_ack(int irq);

/** 放弃特权（不可恢复） */
void drop_privilege(void);

#endif // _USERLAND_LIB_SYSCALL_H_
