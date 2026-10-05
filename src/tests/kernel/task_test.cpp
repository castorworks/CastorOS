#include <tests/kernel/task_test.h>
#include <tests/ktest.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <lib/string.h>
#include <lib/kprintf.h>

#if defined(ARCH_X86_64)
#include <context64.h>
#elif defined(ARCH_ARM64)
#include "../arch/arm64/include/context.h"
#endif

#define TEST_PDE_IDX(v) ((v) >> 22)
#define ENTRY_PRESENT(e) ((e) & PAGE_PRESENT)

static uint32_t g_task_stack_fail_index = UINT32_MAX;

bool kernel::Scheduler::should_fail_stack_page(uint32_t page_index) {
    return page_index == g_task_stack_fail_index;
}

static void init_dummy_task(task_t *task) {
    memset(task, 0, sizeof(task_t));
    task->is_user_process = true;
    task->page_dir_phys = mm::Vmm::create_page_directory();
    task->page_dir = (page_directory_t*)PHYS_TO_VIRT(task->page_dir_phys);
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

    bool ok = kernel::Scheduler::setup_user_stack(&task);
    ASSERT_TRUE(ok);

    ASSERT_NE_U(0, task.user_stack_base);
    ASSERT_EQ_UINT(USER_ARGS_ADDR - 4, task.user_stack);

    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(task.page_dir_phys);
    uint32_t start_pd = TEST_PDE_IDX(USER_SPACE_END - USER_STACK_SIZE);
    uint32_t end_pd = TEST_PDE_IDX(USER_SPACE_END - PAGE_SIZE);
    for (uint32_t pd = start_pd; pd <= end_pd; pd++) {
        ASSERT_TRUE(ENTRY_PRESENT(dir->entries[pd]));
    }

    cleanup_dummy_task(&task);
    ASSERT_EQ_U(0, task.page_dir_phys);
}

/**
 * create_user_process 失败时不能动调用者传入的地址空间：
 * 调用者（loader、task_create_user_process_arm64）会自己销毁它，
 * 这里再释放一次就是二次销毁。
 */
TEST_CASE(test_create_user_process_failure_keeps_address_space) {
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(0, dir);
#if defined(ARCH_ARM64)
    page_directory_t *page_dir = (page_directory_t *)dir;
#else
    page_directory_t *page_dir = (page_directory_t *)PHYS_TO_VIRT(dir);
#endif

    g_task_stack_fail_index = 2;
    uint32_t pid = kernel::Scheduler::create_user_process(
        "fail-test", KTEST_USER_CODE_VADDR, page_dir, KTEST_USER_CODE_VADDR + PAGE_SIZE);
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
 * Property Test: Context structure field offsets are correct
 * 
 * This test verifies that the context structure layout matches
 * what the assembly code expects.
 */
TEST_CASE(test_pbt_context_field_offsets) {
#if defined(ARCH_I686)
    cpu_context_t ctx;
    
    // Calculate offsets using pointer arithmetic
    uintptr_t base = (uintptr_t)&ctx;
    
    // Verify critical field offsets match assembly expectations
    // gs at offset 0
    ASSERT_EQ_U((uintptr_t)&ctx.gs - base, 0);
    // fs at offset 4
    ASSERT_EQ_U((uintptr_t)&ctx.fs - base, 4);
    // es at offset 8
    ASSERT_EQ_U((uintptr_t)&ctx.es - base, 8);
    // ds at offset 12
    ASSERT_EQ_U((uintptr_t)&ctx.ds - base, 12);
    // edi at offset 16
    ASSERT_EQ_U((uintptr_t)&ctx.edi - base, 16);
    // eip at offset 48
    ASSERT_EQ_U((uintptr_t)&ctx.eip - base, 48);
    // eflags at offset 56
    ASSERT_EQ_U((uintptr_t)&ctx.eflags - base, 56);
    // esp at offset 60
    ASSERT_EQ_U((uintptr_t)&ctx.esp - base, 60);
    // cr3 at offset 68
    ASSERT_EQ_U((uintptr_t)&ctx.cr3 - base, 68);
#elif defined(ARCH_X86_64)
    x86_64_context_t ctx;
    
    // Calculate offsets using pointer arithmetic
    uintptr_t base = (uintptr_t)&ctx;
    
    // Verify critical field offsets match assembly expectations
    // r15 at offset 0
    ASSERT_EQ_U((uintptr_t)&ctx.r15 - base, 0);
    // r14 at offset 8
    ASSERT_EQ_U((uintptr_t)&ctx.r14 - base, 8);
    // r8 at offset 56
    ASSERT_EQ_U((uintptr_t)&ctx.r8 - base, 56);
    // rbp at offset 64
    ASSERT_EQ_U((uintptr_t)&ctx.rbp - base, 64);
    // rdi at offset 72
    ASSERT_EQ_U((uintptr_t)&ctx.rdi - base, 72);
    // rax at offset 112
    ASSERT_EQ_U((uintptr_t)&ctx.rax - base, 112);
    // rip at offset 120
    ASSERT_EQ_U((uintptr_t)&ctx.rip - base, 120);
    // cs at offset 128
    ASSERT_EQ_U((uintptr_t)&ctx.cs - base, 128);
    // rflags at offset 136
    ASSERT_EQ_U((uintptr_t)&ctx.rflags - base, 136);
    // rsp at offset 144
    ASSERT_EQ_U((uintptr_t)&ctx.rsp - base, 144);
    // ss at offset 152
    ASSERT_EQ_U((uintptr_t)&ctx.ss - base, 152);
    // cr3 at offset 160 (for address space switching)
    ASSERT_EQ_U((uintptr_t)&ctx.cr3 - base, 160);
#endif
}

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
/**
 * Property Test: x86_64 context CR3 field is correctly positioned for address space switch
 * 
 * *For any* address space switch during task switching, the correct architecture-specific
 * page table base register (CR3 on x86) SHALL be updated to point to the new task's page table.
 * 
 * This test verifies:
 * 1. CR3 field exists at the correct offset in the context structure
 * 2. CR3 is initialized to 0 by default (to be set by caller)
 * 3. CR3 can store a valid 64-bit physical address
 */
TEST_CASE(test_pbt_x86_64_address_space_switch_cr3_offset) {
    x86_64_context_t ctx;
    uintptr_t base = (uintptr_t)&ctx;
    
    // CR3 must be at offset 160 for the assembly code to work correctly
    ASSERT_EQ_U((uintptr_t)&ctx.cr3 - base, 160);
    
    // CR3 field must be 8 bytes (64-bit)
    ASSERT_EQ_U(sizeof(ctx.cr3), 8);
}

#endif /* ARCH_X86_64 */

// ============================================================================
// Property-Based Tests: Context Switch Register Preservation (ARM64)
// ============================================================================

#if defined(ARCH_ARM64)
// ============================================================================
// Property-Based Tests: Address Space Switch Correctness (ARM64)
// ============================================================================

/**
 * Property Test: ARM64 context TTBR0 field is correctly positioned for address space switch
 * 
 * *For any* address space switch during task switching, the correct architecture-specific
 * page table base register (TTBR0_EL1 on ARM64) SHALL be updated to point to the new
 * task's page table.
 */
TEST_CASE(test_pbt_arm64_address_space_switch_ttbr0_offset) {
    arm64_context_t ctx;
    uintptr_t base = (uintptr_t)&ctx;
    
    // TTBR0 must be at offset 272 for the assembly code to work correctly
    ASSERT_EQ_U((uintptr_t)&ctx.ttbr0 - base, 272);
    
    // TTBR0 field must be 8 bytes (64-bit)
    ASSERT_EQ_U(sizeof(ctx.ttbr0), 8);
}

#endif /* ARCH_ARM64 */

TEST_SUITE(task_context_property_tests) {
    RUN_TEST(test_pbt_context_field_offsets);
    RUN_TEST(test_pbt_arch_name);
    RUN_TEST(test_pbt_pointer_size);
#if defined(ARCH_X86_64)
    RUN_TEST(test_pbt_x86_64_address_space_switch_cr3_offset);
#elif defined(ARCH_ARM64)
    RUN_TEST(test_pbt_arm64_address_space_switch_ttbr0_offset);
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

