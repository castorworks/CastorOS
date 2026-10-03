#include <kernel/sync/mutex.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <lib/klog.h>

namespace sync {

static inline task_t *mutex_current_task(void) {
    return kernel::Scheduler::get_current();
}

void Mutex::init() {
lock_.init();
    locked_ = false;
    owner_pid_ = 0;
    recursion_ = 0;
}

bool Mutex::try_lock() {
bool irq_state = kernel::Interrupts::disable();
    task_t *current = mutex_current_task();
    bool acquired = false;

    lock_.lock();

    if (!locked_) {
        locked_ = true;
        owner_pid_ = current ? current->pid : 0;
        recursion_ = 1;
        acquired = true;
    } else if (current != NULL && owner_pid_ == current->pid) {
        recursion_++;
        acquired = true;
    }

    lock_.unlock();
    kernel::Interrupts::restore(irq_state);

    return acquired;
}

void Mutex::lock() {
task_t *current = mutex_current_task();
    if (current == NULL) {
        return;
    }

    while (1) {
        bool irq_state = kernel::Interrupts::disable();

        lock_.lock();

        // 检查互斥锁是否可用
        if (!locked_) {
            locked_ = true;
            owner_pid_ = current->pid;
            recursion_ = 1;
            lock_.unlock();
            kernel::Interrupts::restore(irq_state);
            return;
        }

        // 检查是否是递归锁定（同一任务再次获取）
        if (owner_pid_ == current->pid) {
            recursion_++;
            lock_.unlock();
            kernel::Interrupts::restore(irq_state);
            return;
        }

        // 无法获取，在持有锁的情况下设置任务状态为阻塞
        // 这样可以防止 Lost Wakeup
        current->state = TASK_BLOCKED;
        
        lock_.unlock();
        
        // 现在可以安全地调度到其他任务了
        kernel::Scheduler::schedule();
        
        kernel::Interrupts::restore(irq_state);
        
        // 被唤醒后重新尝试
    }
}

void Mutex::unlock() {
task_t *current = mutex_current_task();
    // 如果任务系统还未初始化，直接返回（与 mutex_lock 保持一致）
    if (current == NULL) {
        return;
    }

    bool irq_state = kernel::Interrupts::disable();
    bool should_wakeup = false;

    lock_.lock();

    if (!locked_) {
        LOG_WARN_MSG("mutex_unlock: unlock called on unlocked mutex\n");
    } else if (owner_pid_ != current->pid) {
        LOG_WARN_MSG("mutex_unlock: current task is not the owner (owner=%u)\n",
                     owner_pid_);
    } else {
        if (--recursion_ == 0) {
            locked_ = false;
            owner_pid_ = 0;
            should_wakeup = true;
        }
    }

    lock_.unlock();

    if (should_wakeup) {
        kernel::Scheduler::wakeup(this);
    }

    kernel::Interrupts::restore(irq_state);
}

bool Mutex::is_locked() const {
return locked_;
}

} // namespace sync
