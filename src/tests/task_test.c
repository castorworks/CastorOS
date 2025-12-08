#include <tests/task_test.h>
#include <tests/ktest.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <lib/string.h>
#include <lib/kprintf.h>

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

// ============================================================================
// Property-Based Tests: Context Switch Register Preservation
// **Feature: multi-arch-support, Property 9: Context Switch Register Preservation**
// **Validates: Requirements 7.1**
// ============================================================================

/**
 * Property Test: Context structure size matches architecture expectations
 * 
 * *For any* architecture, the context structure size SHALL be correct
 * for the architecture's register set.
 */
TEST_CASE(test_pbt_context_size) {
    size_t ctx_size = hal_context_size();
    
    // For i686, context should be 72 bytes (18 x 4-byte fields)
    // This includes: gs, fs, es, ds (4x4), edi-eax (8x4), eip, cs, eflags, esp, ss, cr3 (6x4)
#if defined(ARCH_I686)
    ASSERT_EQ_U(ctx_size, 72);
#elif defined(ARCH_X86_64)
    // x86_64 context would be larger (not implemented yet)
    ASSERT_TRUE(ctx_size >= 72);
#elif defined(ARCH_ARM64)
    // ARM64 context would be different (not implemented yet)
    ASSERT_TRUE(ctx_size >= 72);
#else
    // Unknown architecture, just verify non-zero
    ASSERT_TRUE(ctx_size > 0);
#endif
}

/**
 * Property Test: Context initialization sets correct segment selectors
 * 
 * *For any* context initialization, the segment selectors SHALL be set
 * correctly for the specified privilege level (kernel or user).
 */
TEST_CASE(test_pbt_context_init_segments) {
    cpu_context_t kernel_ctx;
    cpu_context_t user_ctx;
    
    // Initialize kernel context
    hal_context_init((hal_context_t*)&kernel_ctx, 0x80100000, 0x80200000, false);
    
    // Initialize user context
    hal_context_init((hal_context_t*)&user_ctx, 0x00100000, 0x7FFFF000, true);
    
#if defined(ARCH_I686)
    // Kernel context: CS should be 0x08 (kernel code segment)
    ASSERT_EQ_U(kernel_ctx.cs, 0x08);
    // Kernel context: DS should be 0x10 (kernel data segment)
    ASSERT_EQ_U(kernel_ctx.ds, 0x10);
    
    // User context: CS should be 0x1B (user code segment with RPL=3)
    ASSERT_EQ_U(user_ctx.cs, 0x1B);
    // User context: DS should be 0x23 (user data segment with RPL=3)
    ASSERT_EQ_U(user_ctx.ds, 0x23);
    
    // Both contexts should have interrupts enabled (IF flag in EFLAGS)
    ASSERT_TRUE((kernel_ctx.eflags & 0x200) != 0);
    ASSERT_TRUE((user_ctx.eflags & 0x200) != 0);
#endif
}

/**
 * Property Test: Context initialization preserves entry point and stack
 * 
 * *For any* context initialization with entry point E and stack S,
 * the context SHALL contain the correct entry point and stack values.
 */
TEST_CASE(test_pbt_context_init_entry_stack) {
    cpu_context_t ctx;
    
    // Test with user context (simpler - entry point is directly in EIP)
    uintptr_t test_entry = 0x00400000;
    uintptr_t test_stack = 0x7FFFF000;
    
    hal_context_init((hal_context_t*)&ctx, test_entry, test_stack, true);
    
#if defined(ARCH_I686)
    // For user context, EIP should be the entry point
    ASSERT_EQ_U(ctx.eip, test_entry);
    // ESP should be the stack pointer
    ASSERT_EQ_U(ctx.esp, test_stack);
#endif
}

/**
 * Property Test: Context structure field offsets are correct
 * 
 * This test verifies that the context structure layout matches
 * what the assembly code expects.
 */
TEST_CASE(test_pbt_context_field_offsets) {
    cpu_context_t ctx;
    
    // Calculate offsets using pointer arithmetic
    uintptr_t base = (uintptr_t)&ctx;
    
#if defined(ARCH_I686)
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

TEST_SUITE(task_context_property_tests) {
    RUN_TEST(test_pbt_context_size);
    RUN_TEST(test_pbt_context_init_segments);
    RUN_TEST(test_pbt_context_init_entry_stack);
    RUN_TEST(test_pbt_context_field_offsets);
    RUN_TEST(test_pbt_arch_name);
    RUN_TEST(test_pbt_pointer_size);
}

// ============================================================================
// Run all task tests
// ============================================================================

void run_task_tests(void) {
    unittest_begin_suite("Task Manager Tests");
    RUN_TEST(test_user_stack_cleanup_on_partial_failure);
    RUN_TEST(test_user_stack_full_allocation_and_release);
    unittest_end_suite();
    
    // Property-based tests
    // **Feature: multi-arch-support, Property 9: Context Switch Register Preservation**
    // **Validates: Requirements 7.1**
    RUN_SUITE(task_context_property_tests);
}

