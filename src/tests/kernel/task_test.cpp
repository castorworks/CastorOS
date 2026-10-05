#include <tests/kernel/task_test.h>
#include <tests/ktest.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <lib/string.h>
#include <lib/kprintf.h>


/** 栈区（含最顶上的参数页）里有多少页是映射着的 */
static uint32_t mapped_stack_pages(const task_t *task) {
    uint32_t mapped = 0;
    for (uintptr_t virt = USER_STACK_TOP - USER_STACK_SIZE; virt < USER_STACK_TOP; virt += PAGE_SIZE) {
        mapped += hal::Mmu::query((hal_addr_space_t)task->page_dir_phys, virt, NULL, NULL);
    }
    return mapped;
}

static uint32_t g_task_stack_fail_index = UINT32_MAX;

bool kernel::Scheduler::should_fail_stack_page(uint32_t page_index) {
    return page_index == g_task_stack_fail_index;
}

static void init_dummy_task(task_t *task) {
    memset(task, 0, sizeof(task_t));
    task->is_user_process = true;
    task->page_dir_phys = mm::Vmm::create_page_directory();
}

static void cleanup_dummy_task(task_t *task) {
    if (task->page_dir_phys) {
        mm::Vmm::free_page_directory(task->page_dir_phys);
        task->page_dir_phys = 0;
    }
}

TEST_CASE(test_user_stack_cleanup_on_partial_failure) {
    task_t task;
    init_dummy_task(&task);
    ASSERT_NE_U(0, task.page_dir_phys);

    g_task_stack_fail_index = 3;

    bool ok = kernel::Scheduler::setup_user_stack(&task);
    ASSERT_FALSE(ok);

    ASSERT_EQ_UINT(0, task.user_stack_base);

    // 已经映射的那几页都撤掉了（空出来的页表留到地址空间销毁时回收）
    ASSERT_EQ_UINT(0, mapped_stack_pages(&task));

    cleanup_dummy_task(&task);
    ASSERT_EQ_U(0, task.page_dir_phys);

    g_task_stack_fail_index = UINT32_MAX;
}

TEST_CASE(test_user_stack_full_allocation_and_release) {
    task_t task;
    init_dummy_task(&task);
    ASSERT_NE_U(0, task.page_dir_phys);

    bool ok = kernel::Scheduler::setup_user_stack(&task);
    ASSERT_TRUE(ok);

    ASSERT_NE_U(0, task.user_stack_base);
    ASSERT_TRUE(task.user_stack < USER_ARGS_ADDR && task.user_stack >= USER_ARGS_ADDR - 16);

    ASSERT_EQ_UINT(USER_STACK_SIZE / PAGE_SIZE, mapped_stack_pages(&task));

    cleanup_dummy_task(&task);
    ASSERT_EQ_U(0, task.page_dir_phys);
}

/**
 * create_user_process 失败时不能动调用者传入的地址空间：
 * 调用者（loader）会自己销毁它，
 * 这里再释放一次就是二次销毁。
 */
TEST_CASE(test_create_user_process_failure_keeps_address_space) {
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(0, dir);

    g_task_stack_fail_index = 2;
    uint32_t pid = kernel::Scheduler::create_user_process(
        "fail-test", KTEST_USER_CODE_VADDR, dir, KTEST_USER_CODE_VADDR + PAGE_SIZE);
    g_task_stack_fail_index = UINT32_MAX;

    ASSERT_EQ_U(0, pid);

    // 地址空间仍然有效：还能在里面建立并查询映射
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, KTEST_USER_CODE_VADDR, (uintptr_t)frame,
                                               PAGE_PRESENT | PAGE_USER));
    paddr_t queried = 0;
    ASSERT_TRUE(hal::Mmu::query((hal_addr_space_t)dir, KTEST_USER_CODE_VADDR, &queried, NULL));
    ASSERT_EQ_U(queried, frame);

    // 由调用者销毁（连同上面映射的页）
    mm::Vmm::free_page_directory(dir);
}

// ============================================================================
// Property-Based Tests: Context Switch Register Preservation
// ============================================================================

/**
 * Property Test: Architecture name is correct
 * 
 * *For any* architecture, hal_arch_name() SHALL return the correct
 * architecture identifier string.
 */
TEST_CASE(test_pbt_arch_name) {
    const char *arch_name = hal_arch_name();
    
    ASSERT_NOT_NULL(arch_name);
    
#if defined(ARCH_I686)
    ASSERT_STR_EQ(arch_name, "i686");
#elif defined(ARCH_X86_64)
    ASSERT_STR_EQ(arch_name, "x86_64");
#elif defined(ARCH_ARM64)
    ASSERT_STR_EQ(arch_name, "arm64");
#endif
}

/**
 * Property Test: Pointer size matches architecture
 * 
 * *For any* architecture, hal_pointer_size() SHALL return the correct
 * pointer size (4 for 32-bit, 8 for 64-bit).
 */
TEST_CASE(test_pbt_pointer_size) {
    size_t ptr_size = hal_pointer_size();
    
#if defined(ARCH_I686)
    ASSERT_EQ_U(ptr_size, 4);
    ASSERT_FALSE(hal_is_64bit());
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_EQ_U(ptr_size, 8);
    ASSERT_TRUE(hal_is_64bit());
#endif
}

// ============================================================================
// Property-Based Tests: Address Space Switch Correctness (x86_64)
// ============================================================================

#if defined(ARCH_X86_64)
#endif /* ARCH_X86_64 */

// ============================================================================
// Property-Based Tests: Context Switch Register Preservation (ARM64)
// ============================================================================

#if defined(ARCH_ARM64)
// ============================================================================
// Property-Based Tests: Address Space Switch Correctness (ARM64)
// ============================================================================

#endif /* ARCH_ARM64 */

TEST_SUITE(task_context_property_tests) {
    RUN_TEST(test_pbt_arch_name);
    RUN_TEST(test_pbt_pointer_size);
#if defined(ARCH_X86_64)
#elif defined(ARCH_ARM64)
#endif
}

// ============================================================================
// Run all task tests
// ============================================================================

void run_task_tests(void) {
    unittest_begin_suite("Task Manager Tests");
    RUN_TEST(test_user_stack_cleanup_on_partial_failure);
    RUN_TEST(test_user_stack_full_allocation_and_release);
    RUN_TEST(test_create_user_process_failure_keeps_address_space);
    unittest_end_suite();
    
    // Property-based tests
    RUN_SUITE(task_context_property_tests);
}

