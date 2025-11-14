#include <tests/sync_test.h>
#include <tests/ktest.h>
#include <kernel/sync/spinlock.h>
#include <kernel/sync/mutex.h>
#include <kernel/sync/semaphore.h>

static void test_spinlock_basic(void) {
    spinlock_t lock;
    spinlock_init(&lock);

    ASSERT_FALSE(spinlock_is_locked(&lock));
    spinlock_lock(&lock);
    ASSERT_TRUE(spinlock_is_locked(&lock));
    spinlock_unlock(&lock);
    ASSERT_FALSE(spinlock_is_locked(&lock));
}

static void test_mutex_recursive(void) {
    mutex_t mutex;
    mutex_init(&mutex);

    ASSERT_FALSE(mutex_is_locked(&mutex));
    mutex_lock(&mutex);
    ASSERT_TRUE(mutex_is_locked(&mutex));

    /* 同一任务递归加锁 */
    mutex_lock(&mutex);
    ASSERT_TRUE(mutex_is_locked(&mutex));

    /* 释放两次 */
    mutex_unlock(&mutex);
    ASSERT_TRUE(mutex_is_locked(&mutex));

    mutex_unlock(&mutex);
    ASSERT_FALSE(mutex_is_locked(&mutex));
}

static void test_semaphore_basic(void) {
    semaphore_t sem;
    semaphore_init(&sem, 2);

    ASSERT_EQ(2, semaphore_get_value(&sem));

    semaphore_wait(&sem);
    ASSERT_EQ(1, semaphore_get_value(&sem));

    ASSERT_TRUE(semaphore_try_wait(&sem));
    ASSERT_EQ(0, semaphore_get_value(&sem));

    ASSERT_FALSE(semaphore_try_wait(&sem));

    semaphore_signal(&sem);
    ASSERT_EQ(1, semaphore_get_value(&sem));
}

void run_sync_tests(void) {
    unittest_begin_suite("Synchronization Primitive Tests");
    unittest_run_test("spinlock basic operations", test_spinlock_basic);
    unittest_run_test("mutex recursive locking", test_mutex_recursive);
    unittest_run_test("semaphore basic operations", test_semaphore_basic);
    unittest_end_suite();
}

