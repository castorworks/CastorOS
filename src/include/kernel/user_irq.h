#ifndef _KERNEL_USER_IRQ_H_
#define _KERNEL_USER_IRQ_H_

#include <types.h>
#include <kernel/ipc.h>

/**
 * @file user_irq.h
 * @brief 把设备中断交给用户态驱动
 *
 * 特权进程用 irq_claim 认领一条中断线。中断到来时内核屏蔽这条线，
 * 并给属主投递一条 sender == IPC_KERNEL、label == IPC_LABEL_IRQ 的消息
 * （data[0] 是中断号）；属主处理完设备后用 irq_ack 重新打开这条线。
 * 属主没在 recv 时中断记为待处理，下一次 recv(IPC_ANY) 先收到它。
 */

/** 一共能认领多少条中断线（待处理位图是 task_t 里的一个 uint32_t） */
#define USER_IRQ_MAX    16

struct task;

namespace kernel {

class UserIrq {
public:
    /** 当前进程认领 irq。@return 0 成功；-1 线号无效、已被内核或别的进程占用、表满 */
    static int claim(uint32_t irq);

    /** 当前进程处理完了 irq：重新打开中断线。@return 0 成功；-1 不是属主 */
    static int ack(uint32_t irq);

    /**
     * 中断分发时调用（中断上下文）：irq 有用户态属主就屏蔽并通知它
     * @return 是否有属主
     */
    static bool raise(uint32_t irq);

    /** task 有待处理的中断时取出一条填进 *msg（由 Ipc::recv 调用，已关中断） */
    static bool take_pending(struct task *task, ipc_msg *msg);

    /** task 正在退出：屏蔽并释放它认领的中断线 */
    static void on_exit(struct task *task);
};

} // namespace kernel

#endif // _KERNEL_USER_IRQ_H_
