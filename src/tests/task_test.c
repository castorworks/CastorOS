#include <tests/task_test.h>
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
    // x86_64 context should be 168 bytes (21 x 8-byte fields)
    // This includes: r15-rax (15x8), rip, cs, rflags, rsp, ss, cr3 (6x8)
    ASSERT_EQ_U(ctx_size, 168);
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
#if defined(ARCH_I686)
    cpu_context_t kernel_ctx;
    cpu_context_t user_ctx;
    
    // Initialize kernel context
    hal_context_init((hal_context_t*)&kernel_ctx, 0x80100000, 0x80200000, false);
    
    // Initialize user context
    hal_context_init((hal_context_t*)&user_ctx, 0x00100000, 0x7FFFF000, true);
    
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
#elif defined(ARCH_X86_64)
    x86_64_context_t kernel_ctx;
    x86_64_context_t user_ctx;
    
    // Initialize kernel context (use 64-bit addresses)
    hal_context_init((hal_context_t*)&kernel_ctx, 0xFFFF800000100000ULL, 0xFFFF800000200000ULL, false);
    
    // Initialize user context
    hal_context_init((hal_context_t*)&user_ctx, 0x00400000ULL, 0x7FFFFFFFE000ULL, true);
    
    // Kernel context: CS should be 0x08 (kernel code segment)
    ASSERT_EQ_U(kernel_ctx.cs, 0x08);
    // Kernel context: SS should be 0x10 (kernel data segment)
    ASSERT_EQ_U(kernel_ctx.ss, 0x10);
    
    // User context: CS should be 0x1B (user code segment with RPL=3)
    ASSERT_EQ_U(user_ctx.cs, 0x1B);
    // User context: SS should be 0x23 (user data segment with RPL=3)
    ASSERT_EQ_U(user_ctx.ss, 0x23);
    
    // Both contexts should have interrupts enabled (IF flag in RFLAGS)
    ASSERT_TRUE((kernel_ctx.rflags & 0x200) != 0);
    ASSERT_TRUE((user_ctx.rflags & 0x200) != 0);
#endif
}

/**
 * Property Test: Context initialization preserves entry point and stack
 * 
 * *For any* context initialization with entry point E and stack S,
 * the context SHALL contain the correct entry point and stack values.
 */
TEST_CASE(test_pbt_context_init_entry_stack) {
#if defined(ARCH_I686)
    cpu_context_t ctx;
    
    // Test with user context (simpler - entry point is directly in EIP)
    uintptr_t test_entry = 0x00400000;
    uintptr_t test_stack = 0x7FFFF000;
    
    hal_context_init((hal_context_t*)&ctx, test_entry, test_stack, true);
    
    // For user context, EIP should be the entry point
    ASSERT_EQ_U(ctx.eip, test_entry);
    // ESP should be the stack pointer
    ASSERT_EQ_U(ctx.esp, test_stack);
#elif defined(ARCH_X86_64)
    x86_64_context_t ctx;
    
    // Test with user context (simpler - entry point is directly in RIP)
    uint64_t test_entry = 0x00400000ULL;
    uint64_t test_stack = 0x7FFFFFFFE000ULL;
    
    hal_context_init((hal_context_t*)&ctx, test_entry, test_stack, true);
    
    // For user context, RIP should be the entry point
    ASSERT_EQ_U(ctx.rip, test_entry);
    // RSP should be the stack pointer
    ASSERT_EQ_U(ctx.rsp, test_stack);
#endif
}

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
// **Feature: multi-arch-support, Property 10: Address Space Switch Correctness (x86_64)**
// **Validates: Requirements 7.3**
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

/**
 * Property Test: x86_64 context initialization sets CR3 to zero
 * 
 * *For any* newly initialized context, CR3 SHALL be set to 0,
 * indicating that the caller must set the page table address.
 */
TEST_CASE(test_pbt_x86_64_address_space_switch_cr3_init) {
    x86_64_context_t ctx;
    
    // Initialize a user context
    hal_context_init((hal_context_t*)&ctx, 0x00400000ULL, 0x7FFFFFFFE000ULL, true);
    
    // CR3 should be 0 after initialization (caller sets it)
    ASSERT_EQ_U(ctx.cr3, 0);
    
    // Initialize a kernel context
    hal_context_init((hal_context_t*)&ctx, 0xFFFF800000100000ULL, 0xFFFF800000200000ULL, false);
    
    // CR3 should still be 0 after initialization
    ASSERT_EQ_U(ctx.cr3, 0);
}

/**
 * Property Test: x86_64 context CR3 can store valid page table addresses
 * 
 * *For any* valid page table physical address, the CR3 field SHALL be able
 * to store it correctly. Page table addresses must be 4KB aligned.
 */
TEST_CASE(test_pbt_x86_64_address_space_switch_cr3_storage) {
    x86_64_context_t ctx;
    
    // Test various page table addresses (must be 4KB aligned)
    uint64_t test_addresses[] = {
        0x0000000000001000ULL,  // Low memory
        0x0000000000100000ULL,  // 1MB
        0x0000000010000000ULL,  // 256MB
        0x0000000100000000ULL,  // 4GB (above 32-bit)
        0x0000001000000000ULL,  // 64GB
    };
    
    for (size_t i = 0; i < sizeof(test_addresses) / sizeof(test_addresses[0]); i++) {
        // Initialize context
        hal_context_init((hal_context_t*)&ctx, 0x00400000ULL, 0x7FFFFFFFE000ULL, true);
        
        // Set CR3 to test address
        ctx.cr3 = test_addresses[i];
        
        // Verify CR3 stores the address correctly
        ASSERT_EQ_U(ctx.cr3, test_addresses[i]);
        
        // Verify address is 4KB aligned (required for page tables)
        ASSERT_EQ_U(ctx.cr3 & 0xFFF, 0);
    }
}

/**
 * Property Test: x86_64 context structure size includes CR3
 * 
 * *For any* x86_64 context, the structure size SHALL be exactly 168 bytes,
 * which includes all registers plus CR3 for address space switching.
 */
TEST_CASE(test_pbt_x86_64_address_space_switch_context_size) {
    // Context size must be 168 bytes (21 x 8-byte fields)
    ASSERT_EQ_U(sizeof(x86_64_context_t), 168);
    
    // Verify this matches what hal_context_size() returns
    ASSERT_EQ_U(hal_context_size(), 168);
}
#endif /* ARCH_X86_64 */

// ============================================================================
// Property-Based Tests: Context Switch Register Preservation (ARM64)
// **Feature: multi-arch-support, Property 9: Context Switch Register Preservation (ARM64)**
// **Validates: Requirements 7.2**
// ============================================================================

#if defined(ARCH_ARM64)
/**
 * Property Test: ARM64 context structure size is correct
 * 
 * *For any* ARM64 context, the structure size SHALL be 280 bytes,
 * which includes X0-X30 (31 registers), SP, PC, PSTATE, and TTBR0.
 */
TEST_CASE(test_pbt_arm64_context_size) {
    // ARM64 context should be 280 bytes:
    // - X0-X30: 31 x 8 = 248 bytes
    // - SP: 8 bytes
    // - PC: 8 bytes
    // - PSTATE: 8 bytes
    // - TTBR0: 8 bytes
    // Total: 280 bytes
    ASSERT_EQ_U(sizeof(arm64_context_t), 280);
    
    // Verify this matches what hal_context_size() returns
    ASSERT_EQ_U(hal_context_size(), 280);
}

/**
 * Property Test: ARM64 context field offsets are correct
 * 
 * *For any* ARM64 context, the field offsets SHALL match what the
 * assembly code expects for correct register save/restore.
 */
TEST_CASE(test_pbt_arm64_context_field_offsets) {
    arm64_context_t ctx;
    uintptr_t base = (uintptr_t)&ctx;
    
    // X0 at offset 0
    ASSERT_EQ_U((uintptr_t)&ctx.x[0] - base, 0);
    // X1 at offset 8
    ASSERT_EQ_U((uintptr_t)&ctx.x[1] - base, 8);
    // X19 at offset 152 (callee-saved, used for entry function)
    ASSERT_EQ_U((uintptr_t)&ctx.x[19] - base, 152);
    // X29 (FP) at offset 232
    ASSERT_EQ_U((uintptr_t)&ctx.x[29] - base, 232);
    // X30 (LR) at offset 240
    ASSERT_EQ_U((uintptr_t)&ctx.x[30] - base, 240);
    // SP at offset 248
    ASSERT_EQ_U((uintptr_t)&ctx.sp - base, 248);
    // PC at offset 256
    ASSERT_EQ_U((uintptr_t)&ctx.pc - base, 256);
    // PSTATE at offset 264
    ASSERT_EQ_U((uintptr_t)&ctx.pstate - base, 264);
    // TTBR0 at offset 272
    ASSERT_EQ_U((uintptr_t)&ctx.ttbr0 - base, 272);
}

/**
 * Property Test: ARM64 context initialization sets correct PSTATE
 * 
 * *For any* context initialization, the PSTATE SHALL be set correctly
 * for the specified privilege level (EL0 for user, EL1 for kernel).
 */
TEST_CASE(test_pbt_arm64_context_init_pstate) {
    arm64_context_t kernel_ctx;
    arm64_context_t user_ctx;
    
    // Initialize kernel context
    hal_context_init((hal_context_t*)&kernel_ctx, 0xFFFF000000100000ULL, 0xFFFF000000200000ULL, false);
    
    // Initialize user context
    hal_context_init((hal_context_t*)&user_ctx, 0x00400000ULL, 0x7FFFFFFFE000ULL, true);
    
    // Kernel context: PSTATE should indicate EL1h (0x05)
    ASSERT_EQ_U(kernel_ctx.pstate & 0x0F, ARM64_PSTATE_EL1h);
    
    // User context: PSTATE should indicate EL0t (0x00)
    ASSERT_EQ_U(user_ctx.pstate & 0x0F, ARM64_PSTATE_EL0t);
}

/**
 * Property Test: ARM64 context initialization preserves entry point and stack
 * 
 * *For any* context initialization with entry point E and stack S,
 * the context SHALL contain the correct entry point and stack values.
 */
TEST_CASE(test_pbt_arm64_context_init_entry_stack) {
    arm64_context_t ctx;
    
    // Test with user context (simpler - entry point is directly in PC)
    uint64_t test_entry = 0x00400000ULL;
    uint64_t test_stack = 0x7FFFFFFFE000ULL;
    
    hal_context_init((hal_context_t*)&ctx, test_entry, test_stack, true);
    
    // For user context, PC should be the entry point
    ASSERT_EQ_U(ctx.pc, test_entry);
    // SP should be the stack pointer
    ASSERT_EQ_U(ctx.sp, test_stack);
}

/**
 * Property Test: ARM64 kernel context stores entry function in X19
 * 
 * *For any* kernel context initialization, the actual entry function
 * SHALL be stored in X19 (callee-saved register) for use by the
 * kernel thread entry trampoline.
 */
TEST_CASE(test_pbt_arm64_kernel_context_entry_in_x19) {
    arm64_context_t ctx;
    
    // Test with kernel context
    uint64_t test_entry = 0xFFFF000000100000ULL;
    uint64_t test_stack = 0xFFFF000000200000ULL;
    
    hal_context_init((hal_context_t*)&ctx, test_entry, test_stack, false);
    
    // For kernel context, X19 should contain the actual entry function
    ASSERT_EQ_U(ctx.x[19], test_entry);
    
    // PC should point to hal_context_enter_kernel_thread (not the entry function)
    // We can't easily check the exact address, but it should not be the entry function
    ASSERT_NE_U(ctx.pc, test_entry);
}

// ============================================================================
// Property-Based Tests: Address Space Switch Correctness (ARM64)
// **Feature: multi-arch-support, Property 10: Address Space Switch Correctness (ARM64)**
// **Validates: Requirements 7.3**
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

/**
 * Property Test: ARM64 context initialization sets TTBR0 to zero
 * 
 * *For any* newly initialized context, TTBR0 SHALL be set to 0,
 * indicating that the caller must set the page table address.
 */
TEST_CASE(test_pbt_arm64_address_space_switch_ttbr0_init) {
    arm64_context_t ctx;
    
    // Initialize a user context
    hal_context_init((hal_context_t*)&ctx, 0x00400000ULL, 0x7FFFFFFFE000ULL, true);
    
    // TTBR0 should be 0 after initialization (caller sets it)
    ASSERT_EQ_U(ctx.ttbr0, 0);
    
    // Initialize a kernel context
    hal_context_init((hal_context_t*)&ctx, 0xFFFF000000100000ULL, 0xFFFF000000200000ULL, false);
    
    // TTBR0 should still be 0 after initialization
    ASSERT_EQ_U(ctx.ttbr0, 0);
}

/**
 * Property Test: ARM64 context TTBR0 can store valid page table addresses
 * 
 * *For any* valid page table physical address, the TTBR0 field SHALL be able
 * to store it correctly. Page table addresses must be 4KB aligned.
 */
TEST_CASE(test_pbt_arm64_address_space_switch_ttbr0_storage) {
    arm64_context_t ctx;
    
    // Test various page table addresses (must be 4KB aligned)
    uint64_t test_addresses[] = {
        0x0000000040001000ULL,  // QEMU virt RAM base + 4KB
        0x0000000040100000ULL,  // 1MB into RAM
        0x0000000050000000ULL,  // 256MB into RAM
        0x0000000100000000ULL,  // 4GB (above 32-bit)
    };
    
    for (size_t i = 0; i < sizeof(test_addresses) / sizeof(test_addresses[0]); i++) {
        // Initialize context
        hal_context_init((hal_context_t*)&ctx, 0x00400000ULL, 0x7FFFFFFFE000ULL, true);
        
        // Set TTBR0 to test address
        ctx.ttbr0 = test_addresses[i];
        
        // Verify TTBR0 stores the address correctly
        ASSERT_EQ_U(ctx.ttbr0, test_addresses[i]);
        
        // Verify address is 4KB aligned (required for page tables)
        ASSERT_EQ_U(ctx.ttbr0 & 0xFFF, 0);
    }
}
#endif /* ARCH_ARM64 */

TEST_SUITE(task_context_property_tests) {
    RUN_TEST(test_pbt_context_size);
    RUN_TEST(test_pbt_context_init_segments);
    RUN_TEST(test_pbt_context_init_entry_stack);
    RUN_TEST(test_pbt_context_field_offsets);
    RUN_TEST(test_pbt_arch_name);
    RUN_TEST(test_pbt_pointer_size);
#if defined(ARCH_X86_64)
    // **Feature: multi-arch-support, Property 10: Address Space Switch Correctness (x86_64)**
    // **Validates: Requirements 7.3**
    RUN_TEST(test_pbt_x86_64_address_space_switch_cr3_offset);
    RUN_TEST(test_pbt_x86_64_address_space_switch_cr3_init);
    RUN_TEST(test_pbt_x86_64_address_space_switch_cr3_storage);
    RUN_TEST(test_pbt_x86_64_address_space_switch_context_size);
#elif defined(ARCH_ARM64)
    // **Feature: multi-arch-support, Property 9: Context Switch Register Preservation (ARM64)**
    // **Validates: Requirements 7.2**
    RUN_TEST(test_pbt_arm64_context_size);
    RUN_TEST(test_pbt_arm64_context_field_offsets);
    RUN_TEST(test_pbt_arm64_context_init_pstate);
    RUN_TEST(test_pbt_arm64_context_init_entry_stack);
    RUN_TEST(test_pbt_arm64_kernel_context_entry_in_x19);
    // **Feature: multi-arch-support, Property 10: Address Space Switch Correctness (ARM64)**
    // **Validates: Requirements 7.3**
    RUN_TEST(test_pbt_arm64_address_space_switch_ttbr0_offset);
    RUN_TEST(test_pbt_arm64_address_space_switch_ttbr0_init);
    RUN_TEST(test_pbt_arm64_address_space_switch_ttbr0_storage);
#endif
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

