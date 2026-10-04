// ============================================================================
// user_irq.cpp - 把设备中断交给用户态驱动
// ============================================================================

#include <kernel/user_irq.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <hal/hal.h>

namespace kernel {

/* 认领表：槽位下标同时是 task_t::irq_pending 里的位号。owner == 0 表示空闲 */
static struct {
    uint32_t irq;
    uint32_t owner;
} claims[USER_IRQ_MAX];

static void fill_irq_msg(ipc_msg *msg, uint32_t irq) {
    *msg = {};
    msg->sender = IPC_KERNEL;
    msg->label = IPC_LABEL_IRQ;
    msg->data[0] = irq;
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
        } else if (claims[i].irq == irq) {
            return -1;
        }
    }
    if (free_slot < 0) {
        return -1;
    }

    claims[free_slot].irq = irq;
    claims[free_slot].owner = current->pid;
    hal::Interrupt::unmask_irq(irq);
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
            hal::Interrupt::unmask_irq(irq);
            return 0;
        }
    }
    return -1;
}

bool UserIrq::raise(uint32_t irq) {
    for (int i = 0; i < USER_IRQ_MAX; i++) {
        if (claims[i].owner == 0 || claims[i].irq != irq) {
            continue;
        }

        // 电平触发的设备在驱动处理之前会一直拉着中断线：先屏蔽，等 ack 再打开
        hal::Interrupt::mask_irq(irq);

        task_t *owner = Scheduler::get_by_pid(claims[i].owner);
        if (owner) {
            owner->irq_pending |= (1u << i);
            Ipc::notify(owner);
        }
        return true;
    }
    return false;
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
        if (claims[i].owner == task->pid) {
            hal::Interrupt::mask_irq(claims[i].irq);
            claims[i].owner = 0;
        }
    }
    task->irq_pending = 0;
}

} // namespace kernel
