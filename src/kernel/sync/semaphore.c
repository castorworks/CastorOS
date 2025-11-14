#include <kernel/sync/semaphore.h>
#include <kernel/interrupt.h>
#include <kernel/task.h>

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

    while (1) {
        bool irq_state = interrupts_disable();

        spinlock_lock(&sem->lock);
        bool acquired = semaphore_try_consume(sem);
        spinlock_unlock(&sem->lock);

        if (acquired) {
            interrupts_restore(irq_state);
            return;
        }

        task_block(sem);
        interrupts_restore(irq_state);
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
    sem->count++;
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

