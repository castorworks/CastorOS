#include <kernel/sync/semaphore.h>
#include <kernel/interrupt.h>
#include <kernel/task.h>
#include <types.h>

namespace sync {

void Semaphore::init(int32_t initial_count) {
lock_.init();
    count_ = initial_count;
}

bool Semaphore::try_consume() {
    if (count_ > 0) {
        count_--;
        return true;
    }
    return false;
}

void Semaphore::wait() {
    assert_may_sleep("Semaphore::wait");
task_t *current = kernel::Scheduler::get_current();
    if (current == NULL) {
        return;
    }

    while (1) {
        bool irq_state = kernel::Interrupts::disable();

        lock_.lock();
        
        // 尝试获取信号量
        if (try_consume()) {
            lock_.unlock();
            kernel::Interrupts::restore(irq_state);
            return;
        }

        // 无法获取，在持有锁的情况下设置任务状态为阻塞
        // 这样可以防止 Lost Wakeup
        current->wait_object = this;
        current->state = TASK_BLOCKED;
        
        lock_.unlock();
        
        // 现在可以安全地调度到其他任务了
        kernel::Scheduler::schedule();
        
        kernel::Interrupts::restore(irq_state);
        
        // 被唤醒后重新尝试
    }
}

bool Semaphore::try_wait() {
bool irq_state = kernel::Interrupts::disable();
    bool acquired = false;

    lock_.lock();
    acquired = try_consume();
    lock_.unlock();

    kernel::Interrupts::restore(irq_state);
    return acquired;
}

void Semaphore::signal() {
bool irq_state = kernel::Interrupts::disable();

    lock_.lock();
    
    // 防止整数溢出
    if (count_ < INT32_MAX) {
        count_++;
    }
    
    lock_.unlock();

    kernel::Scheduler::wakeup(this);
    kernel::Interrupts::restore(irq_state);
}

int32_t Semaphore::value() {
bool irq_state = kernel::Interrupts::disable();
    lock_.lock();
    int32_t value = count_;
    lock_.unlock();
    kernel::Interrupts::restore(irq_state);
    return value;
}

} // namespace sync
