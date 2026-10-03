#ifndef _KERNEL_SYNC_MUTEX_H_
#define _KERNEL_SYNC_MUTEX_H_

#include <types.h>
#include <kernel/sync/spinlock.h>

namespace sync {

/**
 * @brief 可递归的阻塞互斥锁
 *
 * 获取不到锁时当前任务会被阻塞并让出 CPU。同一任务可以重复加锁，
 * 需要对应次数的 unlock() 才会真正释放。
 * 平凡类型：使用前必须调用 init()。
 */
class Mutex {
public:
    void init();

    void lock();
    bool try_lock();
    void unlock();
    bool is_locked() const;

private:
    Spinlock lock_;
    bool locked_;
    uint32_t owner_pid_;
    uint32_t recursion_;
};

/** @brief RAII 互斥锁守卫 */
using MutexGuard = LockGuard<Mutex>;

} // namespace sync

#endif // _KERNEL_SYNC_MUTEX_H_
