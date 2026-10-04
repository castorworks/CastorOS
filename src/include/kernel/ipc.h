#ifndef _KERNEL_IPC_H_
#define _KERNEL_IPC_H_

#include <types.h>

/**
 * @file ipc.h
 * @brief 同步消息传递
 *
 * 进程之间唯一的通信方式。消息定长，按 PID 寻址，没有缓冲：
 * send 阻塞到对方 recv 为止，recv 阻塞到有人 send 为止（会合）。
 * 服务进程的典型循环是 recv(IPC_ANY) -> 处理 -> send(msg.sender)；
 * 客户用 call 一次完成“发请求 + 等这个服务的应答”。
 */

/** recv 的 from 参数：接收任何进程发来的消息 */
#define IPC_ANY         0

/** 内核发来的消息的 sender（PID 0 是 idle，不会是真正的发送者） */
#define IPC_KERNEL      0

/** 内核消息的 label：设备中断，data[0] 是中断号（见 kernel/user_irq.h） */
#define IPC_LABEL_IRQ   1

#define IPC_MSG_WORDS   6

/** 消息布局在所有架构上相同（与 user/lib/include/syscall.h 保持一致） */
struct ipc_msg {
    uint32_t sender;                ///< 发送者 PID，由内核在投递时填写
    uint32_t label;                 ///< 请求/应答类型，含义由通信双方约定
    uint64_t data[IPC_MSG_WORDS];   ///< 载荷
};

/** 任务在 IPC 中的状态（task_t::ipc_state） */
typedef enum {
    IPC_IDLE = 0,
    IPC_SENDING,        ///< 阻塞在 send：消息存放在自己的 ipc_buf 里，等对方来取
    IPC_RECEIVING       ///< 阻塞在 recv：等别人把消息放进自己的 ipc_buf
} ipc_state_t;

struct task;

namespace kernel {

class Ipc {
public:
    /**
     * 把 *msg 发给 dest，阻塞到对方收下
     * @return 0 成功；-1 目标不存在/已退出，或自己在等待期间被 kill
     */
    static int send(uint32_t dest, const ipc_msg *msg);

    /**
     * 接收一条消息到 *msg
     * @param from IPC_ANY，或只接收指定 PID 发来的消息
     * @return 0 成功；-1 指定的发送者不存在/已退出，或自己在等待期间被 kill
     */
    static int recv(uint32_t from, ipc_msg *msg);

    /** send(dest) 之后 recv(dest)：请求-应答，结果写回 *msg */
    static int call(uint32_t dest, ipc_msg *msg);

    /**
     * task 有了待处理的内核消息（设备中断）：如果它正阻塞在 recv(IPC_ANY) 上，
     * 把消息交给它并唤醒。可以在中断上下文调用。
     */
    static void notify(struct task *task);

    /** task 正在退出：让等它的发送者/接收者带着错误返回 */
    static void on_exit(struct task *task);
};

} // namespace kernel

#endif // _KERNEL_IPC_H_
