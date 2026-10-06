#include <tests/kernel/hw_access_test.h>
#include <tests/ktest.h>
#include <kernel/hw_access.h>
#include <kernel/task.h>
#include <lib/string.h>

using kernel::HwAccess;

// 一个没有特权的用户进程的 PCB：只用到里面的许可表
static task_t task;

static void reset_task(void) {
    memset(&task, 0, sizeof(task));
    task.is_user_process = true;
}

static void test_empty_table_covers_nothing(void) {
    reset_task();
    ASSERT_FALSE(HwAccess::covers(&task, HW_PORTS, 0x3F8, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x09000000ULL, 0x1000));
    ASSERT_FALSE(HwAccess::covers(&task, HW_IRQ, 4, 1));
    ASSERT_FALSE(HwAccess::covers(NULL, HW_IRQ, 4, 1));
}

static void test_ports_are_exact(void) {
    reset_task();
    ASSERT_TRUE(HwAccess::allow(&task, HW_PORTS, 0x3F8, 8));
    ASSERT_EQ_UINT(1, task.hw_allowed_count);

    // 范围里面的每一种宽度都行
    ASSERT_TRUE(HwAccess::covers(&task, HW_PORTS, 0x3F8, 1));
    ASSERT_TRUE(HwAccess::covers(&task, HW_PORTS, 0x3FF, 1));
    ASSERT_TRUE(HwAccess::covers(&task, HW_PORTS, 0x3FC, 4));
    ASSERT_TRUE(HwAccess::covers(&task, HW_PORTS, 0x3F8, 8));

    // 前一个、后一个、跨出边界的、空的都不行
    ASSERT_FALSE(HwAccess::covers(&task, HW_PORTS, 0x3F7, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_PORTS, 0x400, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_PORTS, 0x3FE, 4));
    ASSERT_FALSE(HwAccess::covers(&task, HW_PORTS, 0x3F7, 2));
    ASSERT_FALSE(HwAccess::covers(&task, HW_PORTS, 0x3F8, 0));

    // 许可是分种类的：同样的数字换一种资源不算
    ASSERT_FALSE(HwAccess::covers(&task, HW_IRQ, 0x3F8, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x3F8, 1));
}

static void test_memory_is_per_page(void) {
    reset_task();
    // 一个只占半页的设备（QEMU virt 上 virtio-mmio 的一个槽位是 0x200 字节）
    ASSERT_TRUE(HwAccess::allow(&task, HW_MEMORY, 0x0A003E00ULL, 0x200));

    // 映射的最小单位是页：它所在的整页都可以映射
    ASSERT_TRUE(HwAccess::covers(&task, HW_MEMORY, 0x0A003000ULL, 0x1000));
    ASSERT_TRUE(HwAccess::covers(&task, HW_MEMORY, 0x0A003E00ULL, 0x200));

    // 相邻的页不行，跨到相邻页的也不行
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x0A002000ULL, 0x1000));
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x0A004000ULL, 0x1000));
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x0A003000ULL, 0x2000));

    // 跨页的设备：每一页都算
    ASSERT_TRUE(HwAccess::allow(&task, HW_MEMORY, 0x10000800ULL, 0x1000));
    ASSERT_TRUE(HwAccess::covers(&task, HW_MEMORY, 0x10000000ULL, 0x2000));
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x10000000ULL, 0x3000));
}

static void test_irq_lines(void) {
    reset_task();
    ASSERT_TRUE(HwAccess::allow(&task, HW_IRQ, 11, 1));
    ASSERT_TRUE(HwAccess::covers(&task, HW_IRQ, 11, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_IRQ, 10, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_IRQ, 12, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_IRQ, 11, 2));
}

static void test_bad_ranges_are_refused(void) {
    reset_task();
    ASSERT_FALSE(HwAccess::allow(&task, 99, 0, 1));                         // 不认识的种类
    ASSERT_FALSE(HwAccess::allow(&task, HW_PORTS, 0x3F8, 0));               // 空的
    ASSERT_FALSE(HwAccess::allow(&task, HW_PORTS, 0xFFFF, 2));              // 端口号只有 16 位
    ASSERT_FALSE(HwAccess::allow(&task, HW_PORTS, 0x10000, 1));
    ASSERT_FALSE(HwAccess::allow(&task, HW_MEMORY, ~(uint64_t)0, 2));       // 回绕
    ASSERT_FALSE(HwAccess::allow(&task, HW_IRQ, ~(uint64_t)0, 1));
    ASSERT_FALSE(HwAccess::allow(NULL, HW_IRQ, 4, 1));
    ASSERT_EQ_UINT(0, task.hw_allowed_count);

    // 端口号的最后一个是可以的
    ASSERT_TRUE(HwAccess::allow(&task, HW_PORTS, 0xFFFF, 1));
    // 查询时回绕的范围同样不算被盖住
    ASSERT_TRUE(HwAccess::allow(&task, HW_MEMORY, 0x1000, 0x1000));
    ASSERT_FALSE(HwAccess::covers(&task, HW_MEMORY, 0x1000, ~(uint64_t)0));
}

static void test_table_is_bounded(void) {
    reset_task();
    for (uint32_t i = 0; i < HW_ALLOW_MAX; i++) {
        ASSERT_TRUE(HwAccess::allow(&task, HW_IRQ, 100 + i * 2, 1));
    }
    ASSERT_EQ_UINT(HW_ALLOW_MAX, task.hw_allowed_count);

    // 表满了：新的加不进去，已经有的不受影响
    ASSERT_FALSE(HwAccess::allow(&task, HW_IRQ, 500, 1));
    ASSERT_FALSE(HwAccess::covers(&task, HW_IRQ, 500, 1));
    ASSERT_TRUE(HwAccess::covers(&task, HW_IRQ, 100, 1));
    ASSERT_TRUE(HwAccess::covers(&task, HW_IRQ, 100 + (HW_ALLOW_MAX - 1) * 2, 1));

    // 已经被盖住的范围不占新的位置，表满了也算成功
    ASSERT_TRUE(HwAccess::allow(&task, HW_IRQ, 100, 1));
    ASSERT_EQ_UINT(HW_ALLOW_MAX, task.hw_allowed_count);
}

static void test_covered_range_is_not_recorded_twice(void) {
    reset_task();
    ASSERT_TRUE(HwAccess::allow(&task, HW_PORTS, 0xC000, 64));
    ASSERT_TRUE(HwAccess::allow(&task, HW_PORTS, 0xC010, 4));
    ASSERT_EQ_UINT(1, task.hw_allowed_count);
    ASSERT_TRUE(task.hw_allowed[0].kind == HW_PORTS);
    ASSERT_TRUE(task.hw_allowed[0].start == 0xC000 && task.hw_allowed[0].count == 64);
}

void run_hw_access_tests(void) {
    unittest_begin_suite("Hardware Access Tests");
    unittest_run_test("an empty table covers nothing", test_empty_table_covers_nothing);
    unittest_run_test("port ranges are exact", test_ports_are_exact);
    unittest_run_test("device memory is allowed per page", test_memory_is_per_page);
    unittest_run_test("interrupt lines", test_irq_lines);
    unittest_run_test("bad ranges are refused", test_bad_ranges_are_refused);
    unittest_run_test("the table is bounded", test_table_is_bounded);
    unittest_run_test("a covered range is not recorded twice", test_covered_range_is_not_recorded_twice);
    unittest_end_suite();
}
