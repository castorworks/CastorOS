#include <tests/task_test.h>
#include <tests/ktest.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <kernel/task.h>
#include <lib/string.h>

#define TEST_PDE_IDX(v) ((v) >> 22)
#define ENTRY_PRESENT(e) ((e) & PAGE_PRESENT)

static uint32_t g_task_stack_fail_index = UINT32_MAX;

bool task_should_fail_stack_page(uint32_t page_index) {
    return page_index == g_task_stack_fail_index;
}

static void init_dummy_task(task_t *task) {
    memset(task, 0, sizeof(task_t));
    task->is_user_process = true;
    task->page_dir_phys = vmm_create_page_directory();
    task->page_dir = (page_directory_t*)PHYS_TO_VIRT(task->page_dir_phys);
}

static void cleanup_dummy_task(task_t *task) {
    if (task->page_dir_phys) {
        vmm_free_page_directory(task->page_dir_phys);
        task->page_dir_phys = 0;
    }
}

TEST_CASE(test_user_stack_cleanup_on_partial_failure) {
    task_t task;
    init_dummy_task(&task);
    ASSERT_NE_U(0, task.page_dir_phys);

    g_task_stack_fail_index = 3;

    bool ok = task_setup_user_stack(&task);
    ASSERT_FALSE(ok);

    ASSERT_EQ_UINT(0, task.user_stack_base);

    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(task.page_dir_phys);
    uint32_t start_pd = TEST_PDE_IDX(USER_SPACE_END - USER_STACK_SIZE);
    uint32_t end_pd = TEST_PDE_IDX(USER_SPACE_END - PAGE_SIZE);
    for (uint32_t pd = start_pd; pd <= end_pd; pd++) {
        ASSERT_FALSE(ENTRY_PRESENT(dir->entries[pd]));
    }

    cleanup_dummy_task(&task);
    ASSERT_EQ_U(0, task.page_dir_phys);

    g_task_stack_fail_index = UINT32_MAX;
}

TEST_CASE(test_user_stack_full_allocation_and_release) {
    task_t task;
    init_dummy_task(&task);
    ASSERT_NE_U(0, task.page_dir_phys);

    bool ok = task_setup_user_stack(&task);
    ASSERT_TRUE(ok);

    ASSERT_NE_U(0, task.user_stack_base);
    ASSERT_EQ_UINT(USER_SPACE_END - 4, task.user_stack);

    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(task.page_dir_phys);
    uint32_t start_pd = TEST_PDE_IDX(USER_SPACE_END - USER_STACK_SIZE);
    uint32_t end_pd = TEST_PDE_IDX(USER_SPACE_END - PAGE_SIZE);
    for (uint32_t pd = start_pd; pd <= end_pd; pd++) {
        ASSERT_TRUE(ENTRY_PRESENT(dir->entries[pd]));
    }

    cleanup_dummy_task(&task);
    ASSERT_EQ_U(0, task.page_dir_phys);
}

void run_task_tests(void) {
    unittest_begin_suite("Task Manager Tests");
    RUN_TEST(test_user_stack_cleanup_on_partial_failure);
    RUN_TEST(test_user_stack_full_allocation_and_release);
    unittest_end_suite();
}

