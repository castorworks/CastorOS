#ifndef _KERNEL_SYNC_MUTEX_H_
#define _KERNEL_SYNC_MUTEX_H_

#include <types.h>
#include <kernel/sync/spinlock.h>

typedef struct {
    spinlock_t lock;
    bool locked;
    uint32_t owner_pid;
    uint32_t recursion;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
bool mutex_try_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
bool mutex_is_locked(const mutex_t *mutex);

#endif // _KERNEL_SYNC_MUTEX_H_

