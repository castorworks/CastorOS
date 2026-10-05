// ============================================================================
// user_irq.cpp - 把设备中断交给用户态驱动
// ============================================================================

#include <kernel/user_irq.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <hal/hal.h>

namespace kernel {

/* 认领表：槽位下标同时是 task_t::irq_pending 里的位号。owner == 0 表示空闲。
 * 一条中断线可以被多个进程认领（PCI 设备共享中断线是常态），每个进程占一个槽位。 */
static struct {
    uint32_t irq;
    uint32_t owner;
    bool awaiting_ack;      // 已经通知了属主，还没等到它的 irq_ack
} claims[USER_IRQ_MAX];

static void fill_irq_msg(ipc_msg *msg, uint32_t irq) {
    *msg = {};
    msg->sender = IPC_KERNEL;
    msg->label = IPC_LABEL_IRQ;
    msg->data[0] = irq;
}

/** irq 这条线上是否还有属主没应答 */
static bool anyone_awaiting(uint32_t irq) {
    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (claims[i].owner != 0 && claims[i].irq == irq && claims[i].awaiting_ack) {
            return true;
        }
    }
    return false;
}

int UserIrq::claim(uint32_t irq) {
    task_t *current = Scheduler::get_current();
    if (!current || !hal::Interrupt::irq_is_free(irq)) {
        return -1;
    }

    InterruptGuard guard;

    int free_slot = -1;
    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (claims[i].owner == 0) {
            if (free_slot < 0) {
                free_slot = i;
            }
        } else if (claims[i].irq == irq && claims[i].owner == current->pid) {
            return -1;      // 同一个进程不能认领两次
        }
    }
    if (free_slot < 0) {
        return -1;
    }

    claims[free_slot].irq = irq;
    claims[free_slot].owner = current->pid;
    claims[free_slot].awaiting_ack = false;
    if (!anyone_awaiting(irq)) {
        hal::Interrupt::unmask_irq(irq);
    }
    return 0;
}

int UserIrq::ack(uint32_t irq) {
    task_t *current = Scheduler::get_current();
    if (!current) {
        return -1;
    }

    InterruptGuard guard;

    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (claims[i].owner == current->pid && claims[i].irq == irq) {
            claims[i].awaiting_ack = false;
            // 共享这条线的进程都处理完了才重新打开：否则还没处理的那个设备会立刻再次触发
            if (!anyone_awaiting(irq)) {
                hal::Interrupt::unmask_irq(irq);
            }
            return 0;
        }
    }
    return -1;
}

bool UserIrq::raise(uint32_t irq) {
    bool claimed = false;
    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (claims[i].owner == 0 || claims[i].irq != irq) {
            continue;
        }
        if (!claimed) {
            // 电平触发的设备在驱动处理之前会一直拉着中断线：先屏蔽，等 ack 再打开
            hal::Interrupt::mask_irq(irq);
            claimed = true;
        }

        // 内核不知道是这条线上的哪个设备发的中断：每个属主都通知，由驱动自己看设备状态
        task_t *owner = Scheduler::get_by_pid(claims[i].owner);
        if (owner) {
            claims[i].awaiting_ack = true;
            owner->irq_pending |= (1u << i);
            Ipc::notify(owner);
        }
    }
    return claimed;
}

bool UserIrq::take_pending(task_t *task, ipc_msg *msg) {
    if (task->irq_pending == 0) {
        return false;
    }
    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (task->irq_pending & (1u << i)) {
            task->irq_pending &= ~(1u << i);
            fill_irq_msg(msg, claims[i].irq);
            return true;
        }
    }
    return false;
}

void UserIrq::on_exit(task_t *task) {
    InterruptGuard guard;

    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (claims[i].owner != task->pid) {
            continue;
        }
        uint32_t irq = claims[i].irq;
        claims[i].owner = 0;
        claims[i].awaiting_ack = false;

        // 先屏蔽；这条线上还有别的属主、并且它们都不欠应答时再打开
        hal::Interrupt::mask_irq(irq);
        bool shared = false;
        for (int j = 0; j < USER_IRQ_MAX; j++) {
            shared = shared || (claims[j].owner != 0 && claims[j].irq == irq);
        }
        if (shared && !anyone_awaiting(irq)) {
            hal::Interrupt::unmask_irq(irq);
        }
    }
    task->irq_pending = 0;
}

} // namespace kernel
