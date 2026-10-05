// ============================================================================
// ipc.cpp - 同步消息传递
// ============================================================================
//
// 没有消息队列：消息只存放在阻塞一方的 PCB 里（task_t::ipc_buf）。
//   - 发送者先到：消息留在发送者的 ipc_buf，状态 IPC_SENDING；
//     接收者到来时扫描任务表找到它，取走消息并唤醒它。
//   - 接收者先到：状态 IPC_RECEIVING；发送者到来时把消息写进接收者的
//     ipc_buf 并唤醒它，接收者醒来后再拷回自己的用户缓冲区。
//
// 并发：内核不可抢占，任务之间不会交错；但设备中断会从中断上下文调用
// notify() 来唤醒接收者，所以“检查条件 + 进入阻塞”必须全程关中断。
// ============================================================================

#include <kernel/ipc.h>
#include <kernel/task.h>
#include <kernel/user_irq.h>
#include <kernel/interrupt.h>

namespace kernel {

/** task 有待处理的内核消息（设备中断、到期的定时器）时取出一条填进 *msg */
static bool take_kernel_msg(task_t *task, ipc_msg *msg) {
    if (UserIrq::take_pending(task, msg)) {
        return true;
    }
    if (task->timer_pending) {
        task->timer_pending = false;
        *msg = {};
        msg->sender = IPC_KERNEL;
        msg->label = IPC_LABEL_TIMER;
        return true;
    }
    return false;
}

/** pid 对应的、还能参与通信的任务（存在且尚未退出） */
static task_t *live_task(uint32_t pid) {
    if (pid == 0) {
        return NULL;  // idle
    }
    task_t *task = Scheduler::get_by_pid(pid);
    if (!task || task->state == TASK_ZOMBIE || task->state == TASK_TERMINATED) {
        return NULL;
    }
    return task;
}

/** 结束 task 的等待：记下结果并唤醒它（它阻塞在自己的 ipc_state 上） */
static void finish_wait(task_t *task, int result) {
    task->ipc_state = IPC_IDLE;
    task->ipc_result = result;
    Scheduler::wakeup(&task->ipc_state);
}

/**
 * 阻塞当前任务，直到对方（或 on_exit）把 ipc_state 改回 IPC_IDLE。
 * 调用前已关中断并设置好 ipc_state。等待期间被 kill 则放弃。
 */
static int wait_for_peer(task_t *current) {
    while (current->ipc_state != IPC_IDLE) {
        if (current->kill_pending) {
            current->ipc_state = IPC_IDLE;
            return -1;
        }
        Scheduler::block(&current->ipc_state);
    }
    return current->ipc_result;
}

/** target 是否正阻塞在 recv 上，并且愿意接收 sender 发来的消息 */
static bool ready_to_receive(task_t *target, uint32_t sender) {
    // BLOCKED 之外的 RECEIVING 是刚被 kill 唤醒、即将放弃等待的任务
    return target->state == TASK_BLOCKED && target->ipc_state == IPC_RECEIVING &&
           (target->ipc_peer == IPC_ANY || target->ipc_peer == sender);
}

/** 把 *msg 交给正在等待的 target 并唤醒它 */
static void deliver(task_t *target, const ipc_msg *msg, uint32_t sender) {
    target->ipc_buf = *msg;
    target->ipc_buf.sender = sender;
    finish_wait(target, 0);
}

/**
 * send 和 call 的公共部分。
 * is_call 时，消息被对方收下后当前任务不返回，而是直接转入“等 dest 的应答”：
 * 中间没有空档，对方的 reply 不会因为当前任务还没开始接收而失败。
 */
static int send_common(uint32_t dest, ipc_msg *msg, bool is_call) {
    task_t *current = Scheduler::get_current();
    if (!current || !msg || dest == current->pid) {
        return -1;
    }

    InterruptGuard guard;

    task_t *target = live_task(dest);
    if (!target) {
        return -1;
    }

    if (ready_to_receive(target, current->pid)) {
        // 对方已经在等这条消息：直接交给它
        deliver(target, msg, current->pid);
        if (!is_call) {
            return 0;
        }
        current->ipc_peer = dest;
        current->ipc_state = IPC_RECEIVING;
    } else {
        // 否则把消息留在自己这里，等对方来取（见 Ipc::recv）
        current->ipc_buf = *msg;
        current->ipc_buf.sender = current->pid;
        current->ipc_peer = dest;
        current->ipc_calling = is_call;
        current->ipc_state = IPC_SENDING;
    }

    int result = wait_for_peer(current);
    current->ipc_calling = false;
    if (result == 0 && is_call) {
        *msg = current->ipc_buf;
    }
    return result;
}

int Ipc::send(uint32_t dest, const ipc_msg *msg) {
    return send_common(dest, const_cast<ipc_msg *>(msg), false);
}

int Ipc::call(uint32_t dest, ipc_msg *msg) {
    return send_common(dest, msg, true);
}

int Ipc::reply(uint32_t dest, const ipc_msg *msg) {
    task_t *current = Scheduler::get_current();
    if (!current || !msg) {
        return -1;
    }

    InterruptGuard guard;

    // 只投递给正在专门等当前任务的接收者（call 的后半段）；否则立刻失败，绝不阻塞
    task_t *target = live_task(dest);
    if (!target || target->ipc_peer != current->pid || !ready_to_receive(target, current->pid)) {
        return -1;
    }
    deliver(target, msg, current->pid);
    return 0;
}

int Ipc::recv(uint32_t from, ipc_msg *msg) {
    task_t *current = Scheduler::get_current();
    if (!current || !msg || from == current->pid) {
        return -1;
    }

    InterruptGuard guard;

    // 待处理的内核消息（设备中断、定时器）优先于普通消息
    if ((from == IPC_ANY || from == IPC_FROM_KERNEL) && take_kernel_msg(current, msg)) {
        return 0;
    }

    if (from == IPC_FROM_KERNEL) {
        // 只等内核消息：不看排队的发送者，由 notify() 唤醒
        current->ipc_peer = IPC_FROM_KERNEL;
        current->ipc_state = IPC_RECEIVING;
        int result = wait_for_peer(current);
        if (result == 0) {
            *msg = current->ipc_buf;
        }
        return result;
    }

    // 已经有发送者在等：取走它的消息
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *sender = &task_pool[i];
        if (sender->state != TASK_BLOCKED || sender->ipc_state != IPC_SENDING ||
            sender->ipc_peer != current->pid) {
            continue;
        }
        if (from != IPC_ANY && sender->pid != from) {
            continue;
        }
        *msg = sender->ipc_buf;
        if (sender->ipc_calling) {
            // 对方在 call：留在阻塞状态，直接改为等我们的应答（ipc_peer 已经是我们）
            sender->ipc_state = IPC_RECEIVING;
        } else {
            finish_wait(sender, 0);
        }
        return 0;
    }

    if (from != IPC_ANY && !live_task(from)) {
        return -1;
    }

    current->ipc_peer = from;
    current->ipc_state = IPC_RECEIVING;
    int result = wait_for_peer(current);
    if (result == 0) {
        *msg = current->ipc_buf;
    }
    return result;
}

void Ipc::notify(task_t *task) {
    InterruptGuard guard;

    if (task->state == TASK_BLOCKED && task->ipc_state == IPC_RECEIVING &&
        (task->ipc_peer == IPC_ANY || task->ipc_peer == IPC_FROM_KERNEL) &&
        take_kernel_msg(task, &task->ipc_buf)) {
        finish_wait(task, 0);
    }
}

void Ipc::on_exit(task_t *task) {
    InterruptGuard guard;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *other = &task_pool[i];
        if (other == task || other->state != TASK_BLOCKED || other->ipc_peer != task->pid) {
            continue;
        }
        // 正在给它发消息，或者只等它一个人的消息：都等不到了
        if (other->ipc_state == IPC_SENDING || other->ipc_state == IPC_RECEIVING) {
            finish_wait(other, -1);
        }
    }
    task->ipc_state = IPC_IDLE;
}

} // namespace kernel
