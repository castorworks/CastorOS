#include <tests/kernel/sync_test.h>
#include <tests/ktest.h>
#include <kernel/sync/spinlock.h>

static void test_spinlock_basic(void) {
    sync::Spinlock lock;
    lock.init();

    ASSERT_FALSE(lock.is_locked());
    lock.lock();
    ASSERT_TRUE(lock.is_locked());
    lock.unlock();
    ASSERT_FALSE(lock.is_locked());
}

void run_sync_tests(void) {
    unittest_begin_suite("Synchronization Primitive Tests");
    unittest_run_test("spinlock basic operations", test_spinlock_basic);
    unittest_end_suite();
}

