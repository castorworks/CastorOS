#ifndef _KERNEL_SYNC_SPINLOCK_H_
#define _KERNEL_SYNC_SPINLOCK_H_

#include <types.h>
#include <kernel/interrupt.h>

typedef struct {
    volatile uint32_t value;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
bool spinlock_try_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
bool spinlock_is_locked(const spinlock_t *lock);

void spinlock_lock_irqsave(spinlock_t *lock, bool *irq_state);
void spinlock_unlock_irqrestore(spinlock_t *lock, bool irq_state);

#endif // _KERNEL_SYNC_SPINLOCK_H_

