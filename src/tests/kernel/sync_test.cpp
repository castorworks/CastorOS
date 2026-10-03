#include <tests/kernel/sync_test.h>
#include <tests/ktest.h>
#include <kernel/sync/spinlock.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/semaphore.h>

static void test_spinlock_basic(void) {
    sync::Spinlock lock;
    lock.init();

    ASSERT_FALSE(lock.is_locked());
    lock.lock();
    ASSERT_TRUE(lock.is_locked());
    lock.unlock();
    ASSERT_FALSE(lock.is_locked());
}

static void test_mutex_recursive(void) {
    sync::Mutex mutex;
    mutex.init();

    ASSERT_FALSE(mutex.is_locked());
    mutex.lock();
    ASSERT_TRUE(mutex.is_locked());

    /* 同一任务递归加锁 */
    mutex.lock();
    ASSERT_TRUE(mutex.is_locked());

    /* 释放两次 */
    mutex.unlock();
    ASSERT_TRUE(mutex.is_locked());

    mutex.unlock();
    ASSERT_FALSE(mutex.is_locked());
}

static void test_semaphore_basic(void) {
    sync::Semaphore sem;
    sem.init(2);

    ASSERT_EQ(2, sem.value());

    sem.wait();
    ASSERT_EQ(1, sem.value());

    ASSERT_TRUE(sem.try_wait());
    ASSERT_EQ(0, sem.value());

    ASSERT_FALSE(sem.try_wait());

    sem.signal();
    ASSERT_EQ(1, sem.value());
}

void run_sync_tests(void) {
    unittest_begin_suite("Synchronization Primitive Tests");
    unittest_run_test("spinlock basic operations", test_spinlock_basic);
    unittest_run_test("mutex recursive locking", test_mutex_recursive);
    unittest_run_test("semaphore basic operations", test_semaphore_basic);
    unittest_end_suite();
}

