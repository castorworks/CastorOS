#ifndef _KERNEL_SYSCALL_H_
#define _KERNEL_SYSCALL_H_

#include <types.h>   // uint32_t, size_t, pid_t 等定义

// 系统调用参数使用 uintptr_t 以支持 32 位和 64 位架构
typedef uintptr_t syscall_arg_t;

// ============================================================================
// 系统调用号（与 user/lib/include/syscall.h 保持一致）
//
// 内核只提供进程、内存、调试输出、进程间通信（消息和共享内存），以及给用户态驱动用的硬件访问；
// 文件系统、网络、设备驱动不在内核里。
// ============================================================================

enum {
    // 进程
    SYS_EXIT            = 0,
    SYS_FORK            = 1,
    SYS_EXEC            = 2,   // exec(image, size, args, args_size)：用用户内存里的 ELF 映像替换当前进程
    SYS_WAITPID         = 3,
    SYS_GETPID          = 4,
    SYS_GETPPID         = 5,
    SYS_SCHED_YIELD     = 6,
    SYS_KILL            = 7,
    SYS_NANOSLEEP       = 8,

    // 内存
    SYS_BRK             = 9,
    SYS_MMAP            = 10,  // 只支持匿名映射
    SYS_MUNMAP          = 11,

    // 调试输出（内核串口控制台）
    SYS_CONSOLE_WRITE   = 12,  // console_write(buf, len)

    // 进程间通信（同步、定长消息，见 kernel/ipc.h）
    SYS_IPC_SEND        = 13,  // ipc_send(dest, msg)
    SYS_IPC_RECV        = 14,  // ipc_recv(from, msg)：from 为 IPC_ANY 或指定 PID
    SYS_IPC_CALL        = 15,  // ipc_call(dest, msg)：发送后等待 dest 的应答
    SYS_IPC_REPLY       = 16,  // ipc_reply(dest, msg)：应答正在 call 自己的进程，从不阻塞
    SYS_MEM_GRANT       = 17,  // mem_grant(pid, addr, len)：把自己的一段内存共享给 pid，对方收到 IPC_LABEL_GRANT 消息

    // 硬件访问（仅特权进程，供用户态驱动使用）
    SYS_IO_READ         = 18,  // io_read(port, width, value*)：x86 I/O 端口（arm64 上没有，恒失败）
    SYS_IO_WRITE        = 19,  // io_write(port, width, value)
    SYS_MAP_DEVICE      = 20,  // map_device(phys, len)：把设备内存映射进自己的地址空间
    SYS_IRQ_CLAIM       = 21,  // irq_claim(irq)：中断以 IPC 消息的形式投递（见 kernel/user_irq.h）
    SYS_IRQ_ACK         = 22,  // irq_ack(irq)：处理完毕，重新打开中断线
    SYS_DROP_PRIVILEGE  = 23,  // drop_privilege()：放弃特权，不可恢复
    SYS_DMA_ALLOC       = 24,  // dma_alloc(len, phys*)：物理连续的内存，返回虚拟地址并告知物理地址

    SYS_MAX
};

// 初始化 syscall 表
void syscall_init(void);

/**
 * 系统调用处理函数（汇编调用）
 */
extern "C" void syscall_handler(void);

/**
 * 系统调用分发器
 * @param syscall_num 系统调用号
 * @param p1-p5 系统调用参数
 * @param frame 栈帧指针
 * @return 系统调用返回值
 */
extern "C" syscall_arg_t syscall_dispatcher(syscall_arg_t syscall_num, syscall_arg_t p1, syscall_arg_t p2,
                                 syscall_arg_t p3, syscall_arg_t p4, syscall_arg_t p5,
                                 syscall_arg_t *frame);

#endif // _KERNEL_SYSCALL_H_
