#ifndef _KERNEL_SYNC_SEMAPHORE_H_
#define _KERNEL_SYNC_SEMAPHORE_H_

#include <types.h>
#include <kernel/sync/spinlock.h>

namespace sync {

/**
 * @brief 计数信号量
 *
 * 平凡类型：使用前必须调用 init()。
 */
class Semaphore {
public:
    void init(int32_t initial_count);

    /** @brief P 操作：计数为 0 时阻塞当前任务 */
    void wait();
    /** @brief 非阻塞的 P 操作，成功返回 true */
    bool try_wait();
    /** @brief V 操作：计数加一并唤醒等待者 */
    void signal();
    /** @brief 当前计数值 */
    int32_t value();

private:
    bool try_consume();

    Spinlock lock_;
    int32_t count_;
};

} // namespace sync

#endif // _KERNEL_SYNC_SEMAPHORE_H_
