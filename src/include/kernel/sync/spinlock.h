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

/**
 * @brief RAII 自旋锁守卫
 *
 * 构造时加锁，离开作用域时自动解锁，避免在多个 return 路径上遗漏解锁。
 */
class SpinlockGuard {
public:
    explicit SpinlockGuard(spinlock_t &lock) : lock_(lock) { spinlock_lock(&lock_); }
    ~SpinlockGuard() { spinlock_unlock(&lock_); }

    SpinlockGuard(const SpinlockGuard &) = delete;
    SpinlockGuard &operator=(const SpinlockGuard &) = delete;

private:
    spinlock_t &lock_;
};

/**
 * @brief RAII 自旋锁守卫（同时保存并关闭中断）
 *
 * 构造时保存中断状态、关中断并加锁；离开作用域时解锁并恢复中断状态。
 */
class SpinlockIrqGuard {
public:
    explicit SpinlockIrqGuard(spinlock_t &lock) : lock_(lock) {
        spinlock_lock_irqsave(&lock_, &irq_state_);
    }
    ~SpinlockIrqGuard() { spinlock_unlock_irqrestore(&lock_, irq_state_); }

    SpinlockIrqGuard(const SpinlockIrqGuard &) = delete;
    SpinlockIrqGuard &operator=(const SpinlockIrqGuard &) = delete;

private:
    spinlock_t &lock_;
    bool irq_state_;
};

#endif // _KERNEL_SYNC_SPINLOCK_H_

