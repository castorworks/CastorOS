#include <kernel/sync/semaphore.h>
#include <kernel/interrupt.h>
#include <kernel/task.h>

#define INT32_MAX ((int32_t)0x7FFFFFFF)

void semaphore_init(semaphore_t *sem, int32_t initial_count) {
    if (sem == NULL) {
        return;
    }

    spinlock_init(&sem->lock);
    sem->count = initial_count;
}

static bool semaphore_try_consume(semaphore_t *sem) {
    if (sem->count > 0) {
        sem->count--;
        return true;
    }
    return false;
}

void semaphore_wait(semaphore_t *sem) {
    if (sem == NULL) {
        return;
    }

    task_t *current = task_get_current();
    if (current == NULL) {
        return;
    }

    while (1) {
        bool irq_state = interrupts_disable();

        spinlock_lock(&sem->lock);
        
        // 尝试获取信号量
        if (semaphore_try_consume(sem)) {
            spinlock_unlock(&sem->lock);
            interrupts_restore(irq_state);
            return;
        }

        // 无法获取，在持有锁的情况下设置任务状态为阻塞
        // 这样可以防止 Lost Wakeup
        current->state = TASK_BLOCKED;
        
        spinlock_unlock(&sem->lock);
        
        // 现在可以安全地调度到其他任务了
        task_schedule();
        
        interrupts_restore(irq_state);
        
        // 被唤醒后重新尝试
    }
}

bool semaphore_try_wait(semaphore_t *sem) {
    if (sem == NULL) {
        return false;
    }

    bool irq_state = interrupts_disable();
    bool acquired = false;

    spinlock_lock(&sem->lock);
    acquired = semaphore_try_consume(sem);
    spinlock_unlock(&sem->lock);

    interrupts_restore(irq_state);
    return acquired;
}

void semaphore_signal(semaphore_t *sem) {
    if (sem == NULL) {
        return;
    }

    bool irq_state = interrupts_disable();

    spinlock_lock(&sem->lock);
    
    // 防止整数溢出
    if (sem->count < INT32_MAX) {
        sem->count++;
    }
    
    spinlock_unlock(&sem->lock);

    task_wakeup(sem);
    interrupts_restore(irq_state);
}

int32_t semaphore_get_value(semaphore_t *sem) {
    if (sem == NULL) {
        return 0;
    }

    bool irq_state = interrupts_disable();
    spinlock_lock(&sem->lock);
    int32_t value = sem->count;
    spinlock_unlock(&sem->lock);
    interrupts_restore(irq_state);
    return value;
}

