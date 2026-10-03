#ifndef _KERNEL_SYNC_SPINLOCK_H_
#define _KERNEL_SYNC_SPINLOCK_H_

#include <types.h>
#include <kernel/interrupt.h>

namespace sync {

/**
 * @brief 自旋锁
 *
 * 平凡类型（无构造函数）：可以安全地放在全局/静态存储区（零初始化即为未锁定），
 * 也可以嵌入通过 kmalloc + memset 创建的结构体中；后一种情况请显式调用 init()。
 */
class Spinlock {
public:
    /** @brief 重置为未锁定状态 */
    void init();

    void lock();
    bool try_lock();
    void unlock();
    bool is_locked() const;

    /** @brief 保存中断状态、关中断并加锁 */
    void lock_irqsave(bool &irq_state);
    /** @brief 解锁并恢复 lock_irqsave 保存的中断状态 */
    void unlock_irqrestore(bool irq_state);

private:
    volatile uint32_t value_;
};

/**
 * @brief 通用 RAII 锁守卫
 *
 * 适用于任何提供 lock()/unlock() 的锁类型（Spinlock、Mutex）。
 * 构造时加锁，离开作用域时自动解锁，避免在多个 return 路径上遗漏解锁。
 * 可以省略模板参数：`sync::LockGuard guard(some_lock);`
 */
template <typename Lock>
class LockGuard {
public:
    explicit LockGuard(Lock &lock) : lock_(lock) { lock_.lock(); }
    ~LockGuard() { lock_.unlock(); }

    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    Lock &lock_;
};

/** @brief RAII 自旋锁守卫 */
using SpinlockGuard = LockGuard<Spinlock>;

/**
 * @brief RAII 自旋锁守卫（同时保存并关闭中断）
 *
 * 构造时保存中断状态、关中断并加锁；离开作用域时解锁并恢复中断状态。
 */
class SpinlockIrqGuard {
public:
    explicit SpinlockIrqGuard(Spinlock &lock) : lock_(lock) { lock_.lock_irqsave(irq_state_); }
    ~SpinlockIrqGuard() { lock_.unlock_irqrestore(irq_state_); }

    SpinlockIrqGuard(const SpinlockIrqGuard &) = delete;
    SpinlockIrqGuard &operator=(const SpinlockIrqGuard &) = delete;

private:
    Spinlock &lock_;
    bool irq_state_;
};

} // namespace sync

#endif // _KERNEL_SYNC_SPINLOCK_H_
