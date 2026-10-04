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
// 并发：内核不可抢占，这里的状态只在任务上下文里访问（中断处理函数不碰），
// 关中断只是为了让“检查条件 + 进入阻塞”与唤醒不交错。
// ============================================================================

#include <kernel/ipc.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>

namespace kernel {

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

int Ipc::send(uint32_t dest, const ipc_msg *msg) {
    task_t *current = Scheduler::get_current();
    if (!current || !msg || dest == current->pid) {
        return -1;
    }

    InterruptGuard guard;

    task_t *target = live_task(dest);
    if (!target) {
        return -1;
    }

    // 对方已经在等这条消息：直接交给它
    // （BLOCKED 之外的 RECEIVING 是刚被 kill 唤醒、即将放弃等待的任务）
    if (target->state == TASK_BLOCKED && target->ipc_state == IPC_RECEIVING &&
        (target->ipc_peer == IPC_ANY || target->ipc_peer == current->pid)) {
        target->ipc_buf = *msg;
        target->ipc_buf.sender = current->pid;
        finish_wait(target, 0);
        return 0;
    }

    // 否则把消息留在自己这里，等对方来取
    current->ipc_buf = *msg;
    current->ipc_buf.sender = current->pid;
    current->ipc_peer = dest;
    current->ipc_state = IPC_SENDING;
    return wait_for_peer(current);
}

int Ipc::recv(uint32_t from, ipc_msg *msg) {
    task_t *current = Scheduler::get_current();
    if (!current || !msg || from == current->pid) {
        return -1;
    }

    InterruptGuard guard;

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
        finish_wait(sender, 0);
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

int Ipc::call(uint32_t dest, ipc_msg *msg) {
    if (send(dest, msg) != 0) {
        return -1;
    }
    return recv(dest, msg);
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
