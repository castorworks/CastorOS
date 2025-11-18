#include <kernel/sync/spinlock.h>

#define SPINLOCK_UNLOCKED 0
#define SPINLOCK_LOCKED   1

static inline uint32_t atomic_xchg(volatile uint32_t *addr, uint32_t new_value) {
    uint32_t old_value;
    __asm__ volatile("xchg %0, %1"
                     : "=r"(old_value), "+m"(*addr)
                     : "0"(new_value)
                     : "memory");
    return old_value;
}

void spinlock_init(spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }
    lock->value = SPINLOCK_UNLOCKED;
}

bool spinlock_try_lock(spinlock_t *lock) {
    if (lock == NULL) {
        return false;
    }
    return atomic_xchg(&lock->value, SPINLOCK_LOCKED) == SPINLOCK_UNLOCKED;
}

void spinlock_lock(spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }

    while (!spinlock_try_lock(lock)) {
        __asm__ volatile("pause");
    }
}

void spinlock_unlock(spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }
    // 使用原子交换操作确保多核可见性
    atomic_xchg(&lock->value, SPINLOCK_UNLOCKED);
}

bool spinlock_is_locked(const spinlock_t *lock) {
    if (lock == NULL) {
        return false;
    }
    return lock->value == SPINLOCK_LOCKED;
}

void spinlock_lock_irqsave(spinlock_t *lock, bool *irq_state) {
    if (lock == NULL) {
        if (irq_state != NULL) {
            *irq_state = false;
        }
        return;
    }
    bool prev = interrupts_disable();
    if (irq_state != NULL) {
        *irq_state = prev;
    }
    spinlock_lock(lock);
}

void spinlock_unlock_irqrestore(spinlock_t *lock, bool irq_state) {
    if (lock != NULL) {
        spinlock_unlock(lock);
    }
    interrupts_restore(irq_state);
}

