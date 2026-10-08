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
    SYS_DMA_ALLOC       = 24,
    SYS_UPTIME_MS       = 25,
    SYS_TIMER_SET       = 26,
    SYS_MEM_FREE        = 27,  // mem_free_pages()：还没有分配出去的物理页数
    SYS_DEVICE_FIND     = 28,  // device_find(info*)：按型号查平台设备的地址和中断号（需要特权）
    SYS_HW_ALLOW        = 29,
    SYS_HW_ALLOWED      = 30,
    SYS_CPU_INFO        = 31,
    SYS_POWER           = 32,
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
/**
 * 用内存中的 ELF 映像替换当前进程；成功不返回。
 * argv 是以 NULL 结尾的参数数组（argv[0] 习惯上是程序名），可以为 NULL；
 * 新程序在 main(argc, argv) 里收到它。所有参数连同结尾的 NUL 合计不能超过约 4KB。
 */
int exec(const void *image, size_t size, const char *const argv[]);
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
// 时间
// ============================================================================

/**
 * device_find 的参数和结果。调用者填 compatible 和 index，内核填其余的。
 * （内核和用户库各有一份定义，必须一致）
 */
struct device_info {
    char     compatible[32];    /**< 入：要找的设备型号，如 "virtio,mmio"、"arm,pl011"；设备的
                                 *   compatible 列表里有这一项就算（不必是第一项） */
    uint32_t index;             /**< 入：同一型号的第几个（从 0 开始） */
    uint32_t irq;               /**< 出：中断号（可以直接交给 irq_claim / hw_allow） */
    uint32_t has_irq;           /**< 出：这个设备有没有中断 */
    uint32_t reserved;
    uint64_t base;              /**< 出：寄存器的物理地址（交给 map_device / hw_allow） */
    uint64_t size;              /**< 出：寄存器区的大小 */
    char     name[32];          /**< 出：设备的名字，如 "virtio_mmio@a000000" */
};

/**
 * 按型号查找平台设备（需要特权）。
 *
 * 有些机器上设备的位置不是靠探测得到的，而是固件用一份设备树告诉内核的（arm64）；
 * init 通过这个调用查到每个驱动的设备在哪里，再许可给它（hw_allow），谁都不用把
 * 某块板子上的地址写死。在靠探测发现设备的机器上（x86 的 PCI）没有这样的表，只有一个例外：
 * 电源键 "acpi,power-button"（它在哪里写在 ACPI 的表里），base 是它的寄存器所在的端口。
 *
 * @param index 同一型号有多个时取第几个，从 0 开始
 * @return 0 找到了，结果在 info 里；-1 没有这个设备（或者没有特权）
 */
int device_find(const char *compatible, uint32_t index, struct device_info *info);

/**
 * 调用者此刻在哪个 CPU 上运行（编号从 0 开始）；count 不是 NULL 的话得到正在运行的 CPU 个数。
 * 只是一个观察值：返回之后进程随时可能被换到别的 CPU 上去。
 */
int cpu_info(uint32_t *count);

/** 还没有分配出去的物理页数（每页 4KB）。只是一个观察值：别的进程随时可能分配或释放 */
long mem_free_pages(void);

/** 开机以来的毫秒数（x86 上精度是一个时钟滴答，10ms） */
uint64_t uptime_ms(void);

/**
 * 设置本进程的一次性定时器：ms 毫秒后内核发来一条 sender == IPC_KERNEL、
 * label == IPC_LABEL_TIMER 的消息（ipc_recv 时收到）。再次调用覆盖上一次，ms 为 0 取消。
 * 服务进程用它在等消息的同时处理超时。
 */
void timer_set(uint32_t ms);

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
#define IPC_FROM_KERNEL 0xFFFFFFFFu     // ipc_recv 的 from：只收内核发来的消息（设备中断、定时器到期）
#define IPC_KERNEL      0       // 内核发来的消息的 sender
// 最高位为 1 的 label 保留给内核：用户进程发不出，收到就说明内容是内核担保的
#define IPC_LABEL_RESERVED  0x80000000u
#define IPC_LABEL_IRQ       0x80000001u     // sender == IPC_KERNEL：设备中断，data[0] 是中断号
#define IPC_LABEL_GRANT     0x80000002u     // sender 用 mem_grant 共享来一段内存：data[0] 地址，data[1] 长度
#define IPC_LABEL_TIMER     0x80000003u     // sender == IPC_KERNEL：timer_set 设的定时器到期
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
// 硬件访问
//
// 特权从 init 开始，fork 和 exec 都保留，drop_privilege 之后永久失去。有特权的进程
// 什么硬件都能碰；没有特权的进程只碰得到许可表里有的端口、设备内存和中断线。
// 许可表在还有特权时用 hw_allow 填，同样被 fork 和 exec 保留，放弃特权之后不能再加：
// init 就是这样把每个驱动限制在它自己的设备上的。
// ============================================================================

#define HW_PORTS        0       // x86 的 I/O 端口 [start, start + count)
#define HW_MEMORY       1       // 设备内存 [start, start + count)，单位是字节
#define HW_IRQ          2       // 中断线 [start, start + count)

/** 许可表里的一条（内核和用户库各有一份定义，必须一致） */
struct hw_range {
    uint32_t kind;
    uint32_t reserved;
    uint64_t start;
    uint64_t count;
};

/** 往自己的许可表里加一条。需要特权；表满（8 条）或范围不合法返回 -1 */
int hw_allow(uint32_t kind, uintptr_t start, uintptr_t count);

/** 自己许可表里的第 index 条（从 0 开始）；没有这么多条返回 -1 */
int hw_allowed(uint32_t index, struct hw_range *range);

/**
 * 许可表里种类是 kind 的第 n 条（从 0 开始）。驱动用它得知自己的设备在哪里：
 * 许可给它的端口、设备内存、中断线就是它的设备。
 * @return 没有返回 false
 */
bool hw_find(uint32_t kind, uint32_t n, struct hw_range *range);

/**
 * 读/写 x86 的 I/O 端口，width 是 1、2 或 4 字节。arm64 没有端口，恒返回 -1。
 * 许可表里的端口直接用 in/out 指令访问（内核在本进程运行期间把它们对用户态打开了），
 * 别的端口走系统调用：有特权才成功。
 */
int io_read(uintptr_t port, int width, uint32_t *value);
int io_write(uintptr_t port, int width, uint32_t value);

/**
 * 把设备内存 [phys, phys+length) 映射进自己的地址空间（不缓存）。phys 必须页对齐，
 * 且不能是普通内存。许可按页算：许可了一页里的一部分就可以映射整页。
 * @return 映射的地址；失败返回 MAP_FAILED
 */
void *map_device(uintptr_t phys, size_t length);

/**
 * 分配一段物理上连续、已清零的内存用于 DMA：*phys 得到物理地址（交给设备），
 * 返回值是它在自己地址空间里的地址。用 munmap 释放。只给驱动（有特权，或者持有任何一条许可）。
 * @return 失败返回 MAP_FAILED
 */
void *dma_alloc(size_t length, uint64_t *phys);

/**
 * 认领一条设备中断线。之后每次中断，内核屏蔽这条线并发来一条
 * sender == IPC_KERNEL、label == IPC_LABEL_IRQ、data[0] == irq 的消息；
 * 处理完设备后调用 irq_ack 重新打开它。
 */
int irq_claim(int irq);
int irq_ack(int irq);

/** 放弃特权（不可恢复）。许可表留着 */
void drop_privilege(void);

// ============================================================================
// 电源
// ============================================================================

#define POWER_OFF       0       // 关机
#define POWER_REBOOT    1       // 重启：机器复位，从固件重新启动

/**
 * 关机或重启，立刻：内核不知道文件系统，什么都不等。需要特权，所以只有 init 调用它；
 * 别的程序用 power_request（power.h）请 init 来做，它会先让文件系统和磁盘停稳。
 * @return 成功不返回；-1 = 没有特权，或者这台机器的固件没有给出办法
 */
int power(int action);

#endif // _USERLAND_LIB_SYSCALL_H_
