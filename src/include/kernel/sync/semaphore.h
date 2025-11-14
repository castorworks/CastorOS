#ifndef _KERNEL_SYNC_SEMAPHORE_H_
#define _KERNEL_SYNC_SEMAPHORE_H_

#include <types.h>
#include <kernel/sync/spinlock.h>

typedef struct {
    spinlock_t lock;
    int32_t count;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int32_t initial_count);
void semaphore_wait(semaphore_t *sem);
bool semaphore_try_wait(semaphore_t *sem);
void semaphore_signal(semaphore_t *sem);
int32_t semaphore_get_value(semaphore_t *sem);

#endif // _KERNEL_SYNC_SEMAPHORE_H_

