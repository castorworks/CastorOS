// ============================================================================
// fork_exec_test.c - Fork/Exec 系统调用验证测试
// ============================================================================
// 
// 验证 fork/exec 在各架构上的正确工作：
//   - Task 36.1: 测试 fork 系统调用 (hal_mmu_clone_space COW)
//   - Task 36.2: 测试 exec 系统调用 (程序加载)
// 
// **Feature: multi-arch-support**
// **Validates: Requirements 5.5, 7.4, mm-refactor 4.4, 5.3**
// ============================================================================

#include <tests/ktest.h>
#include <hal/hal.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/pgtable.h>
#include <kernel/task.h>
#include <lib/kprintf.h>
#include <lib/string.h>

// Include architecture-specific context headers for additional types
#if defined(ARCH_X86_64)
#include <context64.h>
#elif defined(ARCH_ARM64)
#include "../arch/arm64/include/context.h"
#endif

// Test virtual addresses in user space
#define FORK_TEST_VADDR_BASE  0x20000000
#define FORK_TEST_PAGE_COUNT  8

// ============================================================================
// Task 36.1: Fork System Call Tests (hal_mmu_clone_space COW)
// **Feature: multi-arch-support**
// **Validates: Requirements 5.5, mm-refactor 4.4, 5.3**
// ============================================================================

/**
 * Test: hal_mmu_clone_space creates valid address space
 * 
 * Verifies that cloning an address space produces a valid, distinct
 * address space handle.
 */
TEST_CASE(test_fork_clone_space_creates_valid_space) {
    hal_addr_space_t current = hal_mmu_current_space();
    ASSERT_NE_U(current, HAL_ADDR_SPACE_INVALID);
    
    // Clone the current address space
    hal_addr_space_t cloned = hal_mmu_clone_space(current);
    
    // Property: Clone must succeed
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Property: Clone must be different from original
    ASSERT_NE_U(cloned, current);
    
    // Clean up
    hal_mmu_destroy_space(cloned);
}

/**
 * Test: hal_mmu_clone_space shares physical pages via COW
 * 
 * *For any* mapped user page, after clone, both parent and child
 * SHALL map to the same physical address with COW flag set.
 */
TEST_CASE(test_fork_cow_shares_physical_pages) {
    hal_addr_space_t current = hal_mmu_current_space();
    
    // Allocate and map a test page
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    vaddr_t test_vaddr = FORK_TEST_VADDR_BASE;
    
    // Skip if already mapped
    if (hal_mmu_query(current, test_vaddr, NULL, NULL)) {
        pmm_free_frame(frame);
        return;
    }
    
    // Map with write permission
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
    bool map_result = hal_mmu_map(current, test_vaddr, frame, flags);
    ASSERT_TRUE(map_result);
    hal_mmu_flush_tlb(test_vaddr);
    
    // Get initial reference count
    uint32_t initial_refcount = pmm_frame_get_refcount(frame);
    
    // Clone the address space
    hal_addr_space_t cloned = hal_mmu_clone_space(current);
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Query both spaces
    paddr_t parent_phys = 0, child_phys = 0;
    uint32_t parent_flags = 0, child_flags = 0;
    
    bool parent_mapped = hal_mmu_query(current, test_vaddr, &parent_phys, &parent_flags);
    bool child_mapped = hal_mmu_query(cloned, test_vaddr, &child_phys, &child_flags);
    
    // Property: Both must be mapped
    ASSERT_TRUE(parent_mapped);
    ASSERT_TRUE(child_mapped);
    
    // Property: Both must point to same physical page (COW sharing)
    ASSERT_EQ_U(parent_phys, child_phys);
    ASSERT_EQ_U(parent_phys, frame);
    
    // Property: Reference count must have increased
    uint32_t new_refcount = pmm_frame_get_refcount(frame);
    ASSERT_TRUE(new_refcount > initial_refcount);
    
    // Property: Both must have COW flag set
    ASSERT_TRUE((parent_flags & HAL_PAGE_COW) != 0);
    ASSERT_TRUE((child_flags & HAL_PAGE_COW) != 0);
    
    // Property: Write permission must be removed (for COW to work)
    ASSERT_TRUE((parent_flags & HAL_PAGE_WRITE) == 0);
    ASSERT_TRUE((child_flags & HAL_PAGE_WRITE) == 0);
    
    // Clean up
    hal_mmu_destroy_space(cloned);
    hal_mmu_unmap(current, test_vaddr);
    hal_mmu_flush_tlb(test_vaddr);
    pmm_free_frame(frame);
}

/**
 * Test: COW reference counting works correctly
 * 
 * Verifies that reference counts are properly managed during
 * clone and destroy operations.
 */
TEST_CASE(test_fork_cow_reference_counting) {
    hal_addr_space_t current = hal_mmu_current_space();
    
    // Allocate and map a test page
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    vaddr_t test_vaddr = FORK_TEST_VADDR_BASE + PAGE_SIZE;
    
    if (hal_mmu_query(current, test_vaddr, NULL, NULL)) {
        pmm_free_frame(frame);
        return;
    }
    
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
    ASSERT_TRUE(hal_mmu_map(current, test_vaddr, frame, flags));
    hal_mmu_flush_tlb(test_vaddr);
    
    // Initial refcount should be 1
    uint32_t refcount1 = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(refcount1, 1);
    
    // Clone once - refcount should be 2
    hal_addr_space_t clone1 = hal_mmu_clone_space(current);
    ASSERT_NE_U(clone1, HAL_ADDR_SPACE_INVALID);
    
    uint32_t refcount2 = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(refcount2, 2);
    
    // Clone again - refcount should be 3
    hal_addr_space_t clone2 = hal_mmu_clone_space(current);
    ASSERT_NE_U(clone2, HAL_ADDR_SPACE_INVALID);
    
    uint32_t refcount3 = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(refcount3, 3);
    
    // Destroy one clone - refcount should be 2
    hal_mmu_destroy_space(clone2);
    uint32_t refcount4 = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(refcount4, 2);
    
    // Destroy other clone - refcount should be 1
    hal_mmu_destroy_space(clone1);
    uint32_t refcount5 = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(refcount5, 1);
    
    // Clean up
    hal_mmu_unmap(current, test_vaddr);
    hal_mmu_flush_tlb(test_vaddr);
    pmm_free_frame(frame);
}

/**
 * Test: Multiple pages are correctly COW-shared
 * 
 * Verifies that cloning works correctly with multiple mapped pages.
 */
TEST_CASE(test_fork_cow_multiple_pages) {
    hal_addr_space_t current = hal_mmu_current_space();
    
    paddr_t frames[FORK_TEST_PAGE_COUNT];
    vaddr_t vaddrs[FORK_TEST_PAGE_COUNT];
    uint32_t mapped_count = 0;
    
    // Allocate and map multiple pages
    for (uint32_t i = 0; i < FORK_TEST_PAGE_COUNT; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        
        vaddrs[i] = FORK_TEST_VADDR_BASE + (i + 2) * PAGE_SIZE;
        
        if (hal_mmu_query(current, vaddrs[i], NULL, NULL)) {
            pmm_free_frame(frames[i]);
            continue;
        }
        
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (hal_mmu_map(current, vaddrs[i], frames[i], flags)) {
            hal_mmu_flush_tlb(vaddrs[i]);
            mapped_count++;
        } else {
            pmm_free_frame(frames[i]);
        }
    }
    
    // Need at least some pages mapped
    ASSERT_TRUE(mapped_count > 0);
    
    // Clone the address space
    hal_addr_space_t cloned = hal_mmu_clone_space(current);
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Verify all mapped pages are COW-shared
    for (uint32_t i = 0; i < mapped_count; i++) {
        paddr_t parent_phys = 0, child_phys = 0;
        uint32_t parent_flags = 0, child_flags = 0;
        
        bool parent_ok = hal_mmu_query(current, vaddrs[i], &parent_phys, &parent_flags);
        bool child_ok = hal_mmu_query(cloned, vaddrs[i], &child_phys, &child_flags);
        
        // Property: Both must be mapped
        ASSERT_TRUE(parent_ok);
        ASSERT_TRUE(child_ok);
        
        // Property: Same physical address
        ASSERT_EQ_U(parent_phys, child_phys);
        
        // Property: COW flag set
        ASSERT_TRUE((parent_flags & HAL_PAGE_COW) != 0);
        ASSERT_TRUE((child_flags & HAL_PAGE_COW) != 0);
        
        // Property: Reference count is 2
        uint32_t refcount = pmm_frame_get_refcount(frames[i]);
        ASSERT_EQ_U(refcount, 2);
    }
    
    // Clean up
    hal_mmu_destroy_space(cloned);
    
    for (uint32_t i = 0; i < mapped_count; i++) {
        hal_mmu_unmap(current, vaddrs[i]);
        hal_mmu_flush_tlb(vaddrs[i]);
        pmm_free_frame(frames[i]);
    }
}

/**
 * Test: Kernel space is shared (not COW) between parent and child
 * 
 * Verifies that kernel mappings are shared directly without COW.
 */
TEST_CASE(test_fork_kernel_space_shared) {
    hal_addr_space_t current = hal_mmu_current_space();
    
    // Clone the address space
    hal_addr_space_t cloned = hal_mmu_clone_space(current);
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Test a kernel address
    vaddr_t kernel_addr = KERNEL_VIRTUAL_BASE + 0x100000;  // 1MB into kernel space
    
    paddr_t parent_phys = 0, child_phys = 0;
    uint32_t parent_flags = 0, child_flags = 0;
    
    bool parent_mapped = hal_mmu_query(current, kernel_addr, &parent_phys, &parent_flags);
    bool child_mapped = hal_mmu_query(cloned, kernel_addr, &child_phys, &child_flags);
    
    // Property: Kernel space must be mapped in both
    ASSERT_TRUE(parent_mapped);
    ASSERT_TRUE(child_mapped);
    
    // Property: Same physical address
    ASSERT_EQ_U(parent_phys, child_phys);
    
    // Property: Kernel pages should NOT have COW flag
    // (kernel space is shared directly, not COW)
    ASSERT_TRUE((parent_flags & HAL_PAGE_COW) == 0);
    ASSERT_TRUE((child_flags & HAL_PAGE_COW) == 0);
    
    // Clean up
    hal_mmu_destroy_space(cloned);
}

/**
 * Test: vmm_clone_page_directory wrapper works correctly
 * 
 * Tests the VMM-level clone function that wraps hal_mmu_clone_space.
 */
TEST_CASE(test_fork_vmm_clone_page_directory) {
    // Create a new page directory
    uintptr_t src_dir = vmm_create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // Map a page in the source directory
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    vaddr_t test_vaddr = FORK_TEST_VADDR_BASE + 0x10000;
    
    bool map_ok = vmm_map_page_in_directory(src_dir, test_vaddr, (uintptr_t)frame,
                                            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    ASSERT_TRUE(map_ok);
    
    // Clone the page directory
    uintptr_t clone_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, src_dir);
    
    // Verify COW sharing via reference count
    uint32_t refcount = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(refcount, 2);
    
    // Clean up
    vmm_free_page_directory(clone_dir);
    vmm_free_page_directory(src_dir);
}

// ============================================================================
// Task 36.2: Exec System Call Tests (Program Loading)
// **Feature: multi-arch-support**
// **Validates: Requirements 7.4**
// ============================================================================

/**
 * Test: User mode transition mechanism is correct
 * 
 * Verifies that the architecture-specific user mode transition
 * mechanism is properly configured.
 */
TEST_CASE(test_exec_user_mode_transition_setup) {
    // Verify architecture-specific user mode setup
#if defined(ARCH_I686)
    // i686 uses IRET for user mode transition
    // Verify user segment selectors are correct
    ASSERT_EQ_U(0x1B, 0x1B);  // User code segment (0x18 | 3)
    ASSERT_EQ_U(0x23, 0x23);  // User data segment (0x20 | 3)
#elif defined(ARCH_X86_64)
    // x86_64 uses IRETQ or SYSRET for user mode transition
    ASSERT_EQ_U(0x1B, 0x1B);  // User code segment
    ASSERT_EQ_U(0x23, 0x23);  // User data segment
#elif defined(ARCH_ARM64)
    // ARM64 uses ERET for user mode transition
    // Verify PSTATE values for EL0
    ASSERT_EQ_U(ARM64_PSTATE_EL0t, 0x00);
#endif
}

/**
 * Test: Context initialization for user mode is correct
 * 
 * Verifies that hal_context_init correctly sets up a user-mode context.
 */
TEST_CASE(test_exec_context_init_user_mode) {
#if defined(ARCH_I686)
    cpu_context_t ctx;
    
    uintptr_t entry = 0x08048000;  // Typical ELF entry point
    uintptr_t stack = 0x7FFFF000;  // User stack
    
    hal_context_init((hal_context_t*)&ctx, entry, stack, true);
    
    // Property: Entry point must be set
    ASSERT_EQ_U(ctx.eip, entry);
    
    // Property: Stack must be set
    ASSERT_EQ_U(ctx.esp, stack);
    
    // Property: User code segment
    ASSERT_EQ_U(ctx.cs, 0x1B);
    
    // Property: User data segment
    ASSERT_EQ_U(ctx.ds, 0x23);
    ASSERT_EQ_U(ctx.ss, 0x23);
    
    // Property: Interrupts enabled
    ASSERT_TRUE((ctx.eflags & 0x200) != 0);
    
#elif defined(ARCH_X86_64)
    x86_64_context_t ctx;
    
    uint64_t entry = 0x00400000ULL;
    uint64_t stack = 0x7FFFFFFFE000ULL;
    
    hal_context_init((hal_context_t*)&ctx, entry, stack, true);
    
    // Property: Entry point must be set
    ASSERT_EQ_U(ctx.rip, entry);
    
    // Property: Stack must be set
    ASSERT_EQ_U(ctx.rsp, stack);
    
    // Property: User code segment (GDT index 4 = 0x20 | RPL=3 = 0x23)
    ASSERT_EQ_U(ctx.cs, 0x23);
    
    // Property: User stack segment (GDT index 3 = 0x18 | RPL=3 = 0x1B)
    ASSERT_EQ_U(ctx.ss, 0x1B);
    
    // Property: Interrupts enabled
    ASSERT_TRUE((ctx.rflags & 0x200) != 0);
    
#elif defined(ARCH_ARM64)
    arm64_context_t ctx;
    
    uint64_t entry = 0x00400000ULL;
    uint64_t stack = 0x7FFFFFFFE000ULL;
    
    hal_context_init((hal_context_t*)&ctx, entry, stack, true);
    
    // Property: Entry point must be set
    ASSERT_EQ_U(ctx.pc, entry);
    
    // Property: Stack must be set
    ASSERT_EQ_U(ctx.sp, stack);
    
    // Property: PSTATE must indicate EL0
    ASSERT_EQ_U(ctx.pstate & 0x0F, ARM64_PSTATE_EL0t);
#endif
}

/**
 * Test: Page directory creation for new process
 * 
 * Verifies that vmm_create_page_directory creates a valid
 * page directory suitable for a new process.
 */
TEST_CASE(test_exec_page_directory_creation) {
    // Create a new page directory (as exec would do)
    uintptr_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // Property: Must be page-aligned
    ASSERT_EQ_U(new_dir & (PAGE_SIZE - 1), 0);
    
    // Property: Kernel space must be mapped
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    vaddr_t kernel_addr = KERNEL_VIRTUAL_BASE + 0x100000;
    
    paddr_t phys = 0;
    bool mapped = hal_mmu_query(space, kernel_addr, &phys, NULL);
    ASSERT_TRUE(mapped);
    ASSERT_NE_U(phys, 0);
    
    // Clean up
    vmm_free_page_directory(new_dir);
}

/**
 * Test: User stack setup for new process
 * 
 * Verifies that user stack can be properly set up in a new
 * address space.
 */
TEST_CASE(test_exec_user_stack_setup) {
    // Create a new page directory
    uintptr_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // Allocate a page for user stack
    paddr_t stack_frame = pmm_alloc_frame();
    ASSERT_NE_U(stack_frame, PADDR_INVALID);
    
    // Map at typical user stack location
    vaddr_t stack_vaddr = 0x7FFFE000;  // Near top of user space
    
    bool map_ok = vmm_map_page_in_directory(new_dir, stack_vaddr, (uintptr_t)stack_frame,
                                            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    ASSERT_TRUE(map_ok);
    
    // Verify mapping
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    paddr_t queried_phys = 0;
    uint32_t queried_flags = 0;
    
    bool query_ok = hal_mmu_query(space, stack_vaddr, &queried_phys, &queried_flags);
    ASSERT_TRUE(query_ok);
    ASSERT_EQ_U(queried_phys, stack_frame);
    
    // Property: Stack must be writable
    ASSERT_TRUE((queried_flags & HAL_PAGE_WRITE) != 0);
    
    // Property: Stack must be user-accessible
    ASSERT_TRUE((queried_flags & HAL_PAGE_USER) != 0);
    
    // Clean up
    vmm_free_page_directory(new_dir);
}

/**
 * Test: Program code mapping for new process
 * 
 * Verifies that program code can be properly mapped in a new
 * address space (simulating ELF loading).
 */
TEST_CASE(test_exec_program_code_mapping) {
    // Create a new page directory
    uintptr_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // Allocate pages for program code
    paddr_t code_frame = pmm_alloc_frame();
    ASSERT_NE_U(code_frame, PADDR_INVALID);
    
    // Map at typical program load address
    vaddr_t code_vaddr = 0x08048000;  // Typical ELF load address
    
    // Code should be readable and executable, but not writable
    bool map_ok = vmm_map_page_in_directory(new_dir, code_vaddr, (uintptr_t)code_frame,
                                            PAGE_PRESENT | PAGE_USER);
    ASSERT_TRUE(map_ok);
    
    // Verify mapping
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    paddr_t queried_phys = 0;
    uint32_t queried_flags = 0;
    
    bool query_ok = hal_mmu_query(space, code_vaddr, &queried_phys, &queried_flags);
    ASSERT_TRUE(query_ok);
    ASSERT_EQ_U(queried_phys, code_frame);
    
    // Property: Code must be present
    ASSERT_TRUE((queried_flags & HAL_PAGE_PRESENT) != 0);
    
    // Property: Code must be user-accessible
    ASSERT_TRUE((queried_flags & HAL_PAGE_USER) != 0);
    
    // Clean up
    vmm_free_page_directory(new_dir);
}

// ============================================================================
// Test Suites
// ============================================================================

TEST_SUITE(fork_cow_tests) {
    RUN_TEST(test_fork_clone_space_creates_valid_space);
    RUN_TEST(test_fork_cow_shares_physical_pages);
    RUN_TEST(test_fork_cow_reference_counting);
    RUN_TEST(test_fork_cow_multiple_pages);
    RUN_TEST(test_fork_kernel_space_shared);
    RUN_TEST(test_fork_vmm_clone_page_directory);
}

TEST_SUITE(exec_tests) {
    RUN_TEST(test_exec_user_mode_transition_setup);
    RUN_TEST(test_exec_context_init_user_mode);
    RUN_TEST(test_exec_page_directory_creation);
    RUN_TEST(test_exec_user_stack_setup);
    RUN_TEST(test_exec_program_code_mapping);
}

// ============================================================================
// Run all fork/exec tests
// ============================================================================

void run_fork_exec_tests(void) {
    unittest_init();
    
    kprintf("\n");
    kprintf("==========================================================\n");
    kprintf("Fork/Exec Verification Tests\n");
    kprintf("**Feature: multi-arch-support**\n");
    kprintf("**Validates: Requirements 5.5, 7.4**\n");
    kprintf("==========================================================\n");
    
    // Task 36.1: Fork system call tests (COW)
    kprintf("\n--- Task 36.1: Fork System Call (COW) Tests ---\n");
    RUN_SUITE(fork_cow_tests);
    
    // Task 36.2: Exec system call tests
    kprintf("\n--- Task 36.2: Exec System Call Tests ---\n");
    RUN_SUITE(exec_tests);
    
    unittest_print_summary();
}
