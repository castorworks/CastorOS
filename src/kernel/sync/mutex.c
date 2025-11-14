#include <kernel/sync/mutex.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <lib/klog.h>

static inline task_t *mutex_current_task(void) {
    return task_get_current();
}

void mutex_init(mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    spinlock_init(&mutex->lock);
    mutex->locked = false;
    mutex->owner_pid = 0;
    mutex->recursion = 0;
}

bool mutex_try_lock(mutex_t *mutex) {
    if (mutex == NULL) {
        return false;
    }

    bool irq_state = interrupts_disable();
    task_t *current = mutex_current_task();
    bool acquired = false;

    spinlock_lock(&mutex->lock);

    if (!mutex->locked) {
        mutex->locked = true;
        mutex->owner_pid = current ? current->pid : 0;
        mutex->recursion = 1;
        acquired = true;
    } else if (current != NULL && mutex->owner_pid == current->pid) {
        mutex->recursion++;
        acquired = true;
    }

    spinlock_unlock(&mutex->lock);
    interrupts_restore(irq_state);

    return acquired;
}

void mutex_lock(mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    while (1) {
        bool irq_state = interrupts_disable();
        task_t *current = mutex_current_task();

        spinlock_lock(&mutex->lock);

        if (!mutex->locked) {
            mutex->locked = true;
            mutex->owner_pid = current ? current->pid : 0;
            mutex->recursion = 1;
            spinlock_unlock(&mutex->lock);
            interrupts_restore(irq_state);
            return;
        }

        if (current != NULL && mutex->owner_pid == current->pid) {
            mutex->recursion++;
            spinlock_unlock(&mutex->lock);
            interrupts_restore(irq_state);
            return;
        }

        spinlock_unlock(&mutex->lock);
        task_block(mutex);
        interrupts_restore(irq_state);
    }
}

void mutex_unlock(mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    bool irq_state = interrupts_disable();
    task_t *current = mutex_current_task();
    bool should_wakeup = false;

    spinlock_lock(&mutex->lock);

    if (!mutex->locked) {
        LOG_WARN_MSG("mutex_unlock: unlock called on unlocked mutex\n");
    } else if (current == NULL || mutex->owner_pid != current->pid) {
        LOG_WARN_MSG("mutex_unlock: current task is not the owner (owner=%u)\n",
                     mutex->owner_pid);
    } else {
        if (--mutex->recursion == 0) {
            mutex->locked = false;
            mutex->owner_pid = 0;
            should_wakeup = true;
        }
    }

    spinlock_unlock(&mutex->lock);

    if (should_wakeup) {
        task_wakeup(mutex);
    }

    interrupts_restore(irq_state);
}

bool mutex_is_locked(const mutex_t *mutex) {
    if (mutex == NULL) {
        return false;
    }
    return mutex->locked;
}

