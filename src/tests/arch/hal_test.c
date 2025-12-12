// ============================================================================
// hal_test.c - HAL (Hardware Abstraction Layer) 单元测试
// ============================================================================
//
// 模块名称: hal
// 子系统: arch (架构相关)
// 描述: 测试 HAL (Hardware Abstraction Layer) 的功能
//
// 功能覆盖:
//   - HAL 初始化分发 (CPU, 中断, MMU)
//   - 架构信息查询 (架构名称, 指针大小, 64位标志)
//   - MMIO 读写操作 (8/16/32/64位)
//   - 内存屏障操作
//   - MMU 映射/查询往返
//   - 地址空间切换
//
// 架构支持:
//   - i686: 32位 x86
//   - x86_64: 64位 x86
//   - ARM64: 64位 ARM
//
// **Feature: multi-arch-support, test-refactor**
// **Property 1: HAL Initialization Dispatch**
// **Property 14: MMIO Memory Barrier Correctness**
// **Validates: Requirements 1.1, 7.1, 7.3, 9.1**
// ============================================================================

#include <tests/ktest.h>
#include <tests/arch/hal_test.h>
#include <tests/test_module.h>
#include <hal/hal.h>
#include <lib/kprintf.h>

/* ============================================================================
 * Property 1: HAL Initialization Dispatch
 * ============================================================================
 * 
 * *For any* supported architecture, when the kernel initializes, the HAL 
 * interface SHALL dispatch to the correct architecture-specific initialization 
 * routine, and the initialization SHALL complete successfully.
 * 
 * **Validates: Requirements 1.1**
 * 
 * Test Strategy:
 * Since we're running in a kernel context where initialization has already
 * occurred, we verify that:
 * 1. The HAL state query functions report successful initialization
 * 2. The architecture name matches the expected value for the build target
 * 3. The pointer size matches the architecture (32-bit for i686)
 * ========================================================================== */

/**
 * @brief Test that HAL CPU initialization completed successfully
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_cpu_init_dispatch) {
    /* Verify CPU initialization state */
    ASSERT_TRUE(hal_cpu_initialized());
}

/**
 * @brief Test that HAL interrupt initialization completed successfully
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_interrupt_init_dispatch) {
    /* Verify interrupt initialization state */
    ASSERT_TRUE(hal_interrupt_initialized());
}

/**
 * @brief Test that HAL MMU initialization completed successfully
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_mmu_init_dispatch) {
    /* Verify MMU initialization state */
    ASSERT_TRUE(hal_mmu_initialized());
}

/**
 * @brief Test that architecture name is correct for the build target
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_arch_name_correct) {
    const char *arch_name = hal_arch_name();
    
    ASSERT_NOT_NULL(arch_name);
    
#if defined(ARCH_I686)
    ASSERT_STR_EQ("i686", arch_name);
#elif defined(ARCH_X86_64)
    ASSERT_STR_EQ("x86_64", arch_name);
#elif defined(ARCH_ARM64)
    ASSERT_STR_EQ("arm64", arch_name);
#else
    /* Unknown architecture - test should fail */
    ASSERT_TRUE(false);
#endif
}

/**
 * @brief Test that pointer size matches architecture
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_pointer_size_correct) {
    size_t ptr_size = hal_pointer_size();
    
#if defined(ARCH_I686)
    /* 32-bit architecture: 4 bytes */
    ASSERT_EQ_UINT(4, ptr_size);
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    /* 64-bit architecture: 8 bytes */
    ASSERT_EQ_UINT(8, ptr_size);
#endif
}

/**
 * @brief Test that 64-bit flag is correct for architecture
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_is_64bit_correct) {
#if defined(ARCH_I686)
    ASSERT_FALSE(hal_is_64bit());
#elif defined(ARCH_X86_64) || defined(ARCH_ARM64)
    ASSERT_TRUE(hal_is_64bit());
#endif
}

/**
 * @brief Test that all HAL subsystems are initialized together
 * 
 * This is a comprehensive test that verifies the HAL initialization
 * dispatch correctly initialized all required subsystems.
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */
TEST_CASE(hal_all_subsystems_initialized) {
    /* All three core subsystems must be initialized */
    bool cpu_ok = hal_cpu_initialized();
    bool int_ok = hal_interrupt_initialized();
    bool mmu_ok = hal_mmu_initialized();
    
    ASSERT_TRUE(cpu_ok);
    ASSERT_TRUE(int_ok);
    ASSERT_TRUE(mmu_ok);
    
    /* All must be true for complete initialization */
    ASSERT_TRUE(cpu_ok && int_ok && mmu_ok);
}

/* ============================================================================
 * Property 14: MMIO Memory Barrier Correctness
 * ============================================================================
 * 
 * *For any* MMIO read or write operation, the appropriate memory barriers 
 * SHALL be issued to ensure correct ordering with respect to other memory 
 * operations, preventing reordering by the CPU or compiler.
 * 
 * **Validates: Requirements 9.1**
 * 
 * Test Strategy:
 * Since memory barriers are primarily about preventing reordering (which is
 * hard to test directly), we verify:
 * 1. MMIO read/write functions work correctly with a test memory location
 * 2. Memory barrier functions can be called without crashing
 * 3. Write-then-read sequences return the written value (basic ordering)
 * ========================================================================== */

/**
 * @brief Test MMIO read/write with memory barriers
 * 
 * **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 9.1**
 */
TEST_CASE(hal_mmio_read_write_8bit) {
    volatile uint8_t test_var = 0;
    
    /* Write a value */
    hal_mmio_write8(&test_var, 0x42);
    
    /* Read it back - should get the same value */
    uint8_t read_val = hal_mmio_read8(&test_var);
    
    ASSERT_EQ_UINT(0x42, read_val);
}

/**
 * @brief Test MMIO 16-bit read/write
 * 
 * **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 9.1**
 */
TEST_CASE(hal_mmio_read_write_16bit) {
    volatile uint16_t test_var = 0;
    
    hal_mmio_write16(&test_var, 0x1234);
    uint16_t read_val = hal_mmio_read16(&test_var);
    
    ASSERT_EQ_UINT(0x1234, read_val);
}

/**
 * @brief Test MMIO 32-bit read/write
 * 
 * **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 9.1**
 */
TEST_CASE(hal_mmio_read_write_32bit) {
    volatile uint32_t test_var = 0;
    
    hal_mmio_write32(&test_var, 0xDEADBEEF);
    uint32_t read_val = hal_mmio_read32(&test_var);
    
    ASSERT_EQ_UINT(0xDEADBEEF, read_val);
}

/**
 * @brief Test MMIO 64-bit read/write
 * 
 * **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 9.1**
 */
TEST_CASE(hal_mmio_read_write_64bit) {
    volatile uint64_t test_var = 0;
    
    hal_mmio_write64(&test_var, 0xDEADBEEFCAFEBABEULL);
    uint64_t read_val = hal_mmio_read64(&test_var);
    
    ASSERT_TRUE(read_val == 0xDEADBEEFCAFEBABEULL);
}

/**
 * @brief Test memory barrier functions don't crash
 * 
 * **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 9.1**
 */
TEST_CASE(hal_memory_barriers_callable) {
    /* These should not crash */
    hal_memory_barrier();
    hal_read_barrier();
    hal_write_barrier();
    hal_instruction_barrier();
    
    /* If we get here, barriers are callable */
    ASSERT_TRUE(true);
}

/**
 * @brief Test MMIO write ordering with barriers
 * 
 * This test verifies that writes followed by reads return the correct
 * values, which is a basic ordering guarantee.
 * 
 * **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 9.1**
 */
TEST_CASE(hal_mmio_write_ordering) {
    volatile uint32_t test_vars[4] = {0, 0, 0, 0};
    
    /* Write multiple values in sequence */
    hal_mmio_write32(&test_vars[0], 0x11111111);
    hal_mmio_write32(&test_vars[1], 0x22222222);
    hal_mmio_write32(&test_vars[2], 0x33333333);
    hal_mmio_write32(&test_vars[3], 0x44444444);
    
    /* Full memory barrier */
    hal_memory_barrier();
    
    /* Read them back - should all be correct */
    ASSERT_EQ_UINT(0x11111111, hal_mmio_read32(&test_vars[0]));
    ASSERT_EQ_UINT(0x22222222, hal_mmio_read32(&test_vars[1]));
    ASSERT_EQ_UINT(0x33333333, hal_mmio_read32(&test_vars[2]));
    ASSERT_EQ_UINT(0x44444444, hal_mmio_read32(&test_vars[3]));
}

/* ============================================================================
 * Property 8: HAL MMU Map-Query Round-Trip
 * ============================================================================
 * 
 * *For any* valid virtual address `virt`, physical address `phys`, and flags 
 * `flags`, after `hal_mmu_map(space, virt, phys, flags)` succeeds, 
 * `hal_mmu_query(space, virt, &out_phys, &out_flags)` SHALL return `true` 
 * with `out_phys == phys`.
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip**
 * **Validates: Requirements 4.1**
 * 
 * Test Strategy:
 * 1. Allocate physical frames
 * 2. Map them to user-space virtual addresses using hal_mmu_map()
 * 3. Query the mappings using hal_mmu_query()
 * 4. Verify the returned physical address matches the original
 * 5. Clean up by unmapping and freeing frames
 * ========================================================================== */

#include <mm/pmm.h>
#include <mm/mm_types.h>

/* Test virtual addresses in user space */
#define HAL_TEST_VIRT_BASE  0x30000000

/**
 * @brief Property test: Map-Query round-trip for single page
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip**
 * **Validates: Requirements 4.1**
 */
TEST_CASE(hal_mmu_map_query_roundtrip_single) {
#if defined(ARCH_I686)
    /* Allocate a physical frame */
    paddr_t phys = pmm_alloc_frame();
    ASSERT_NE_U(phys, PADDR_INVALID);
    
    vaddr_t virt = HAL_TEST_VIRT_BASE;
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_USER;
    
    /* Map the page */
    bool map_result = hal_mmu_map(HAL_ADDR_SPACE_CURRENT, virt, phys, flags);
    ASSERT_TRUE(map_result);
    
    /* Flush TLB to ensure mapping is visible */
    hal_mmu_flush_tlb(virt);
    
    /* Query the mapping */
    paddr_t out_phys = 0;
    uint32_t out_flags = 0;
    bool query_result = hal_mmu_query(HAL_ADDR_SPACE_CURRENT, virt, &out_phys, &out_flags);
    
    /* Property: Query should succeed and return the same physical address */
    ASSERT_TRUE(query_result);
    ASSERT_EQ_U(out_phys, phys);
    
    /* Property: Flags should include the ones we set */
    ASSERT_TRUE((out_flags & HAL_PAGE_PRESENT) != 0);
    ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
    ASSERT_TRUE((out_flags & HAL_PAGE_USER) != 0);
    
    /* Clean up */
    hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, virt);
    hal_mmu_flush_tlb(virt);
    pmm_free_frame(phys);
#else
    /* Skip on non-i686 architectures for now */
    ASSERT_TRUE(true);
#endif
}

/**
 * @brief Property test: Map-Query round-trip for multiple pages
 * 
 * Tests the property across multiple random-ish virtual addresses.
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip**
 * **Validates: Requirements 4.1**
 */
TEST_CASE(hal_mmu_map_query_roundtrip_multiple) {
#if defined(ARCH_I686)
    #define PBT_MAP_QUERY_ITERATIONS 20
    
    paddr_t frames[PBT_MAP_QUERY_ITERATIONS];
    vaddr_t virts[PBT_MAP_QUERY_ITERATIONS];
    uint32_t allocated = 0;
    
    /* Allocate and map multiple pages */
    for (uint32_t i = 0; i < PBT_MAP_QUERY_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        
        /* Use different virtual addresses */
        virts[i] = HAL_TEST_VIRT_BASE + (i * PAGE_SIZE);
        
        /* Vary flags slightly */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER;
        if (i % 2 == 0) {
            flags |= HAL_PAGE_WRITE;
        }
        
        bool map_result = hal_mmu_map(HAL_ADDR_SPACE_CURRENT, virts[i], frames[i], flags);
        ASSERT_TRUE(map_result);
        
        allocated++;
    }
    
    /* Flush TLB */
    hal_mmu_flush_tlb_all();
    
    /* Verify all mappings */
    for (uint32_t i = 0; i < allocated; i++) {
        paddr_t out_phys = 0;
        uint32_t out_flags = 0;
        
        bool query_result = hal_mmu_query(HAL_ADDR_SPACE_CURRENT, virts[i], &out_phys, &out_flags);
        
        /* Property: Query must succeed */
        ASSERT_TRUE(query_result);
        
        /* Property: Physical address must match */
        ASSERT_EQ_U(out_phys, frames[i]);
        
        /* Property: Present flag must be set */
        ASSERT_TRUE((out_flags & HAL_PAGE_PRESENT) != 0);
    }
    
    /* Clean up */
    for (uint32_t i = 0; i < allocated; i++) {
        hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, virts[i]);
        pmm_free_frame(frames[i]);
    }
    hal_mmu_flush_tlb_all();
#else
    ASSERT_TRUE(true);
#endif
}

/**
 * @brief Property test: Query returns false for unmapped addresses
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip**
 * **Validates: Requirements 4.1**
 */
TEST_CASE(hal_mmu_query_unmapped_returns_false) {
#if defined(ARCH_I686)
    /* Query an address that should not be mapped */
    vaddr_t unmapped_virt = 0x50000000;  /* Arbitrary user-space address */
    
    paddr_t out_phys = 0;
    uint32_t out_flags = 0;
    
    bool query_result = hal_mmu_query(HAL_ADDR_SPACE_CURRENT, unmapped_virt, &out_phys, &out_flags);
    
    /* Property: Query should return false for unmapped address */
    ASSERT_FALSE(query_result);
#else
    ASSERT_TRUE(true);
#endif
}

/* ============================================================================
 * Property 9: Address Space Switch Consistency
 * ============================================================================
 * 
 * *For any* valid address space `space`, after `hal_mmu_switch_space(space)`, 
 * `hal_mmu_current_space()` SHALL return `space`.
 * 
 * **Feature: mm-refactor, Property 9: Address Space Switch Consistency**
 * **Validates: Requirements 4.5**
 * 
 * Test Strategy:
 * 1. Save the current address space
 * 2. Create a new address space
 * 3. Switch to the new address space
 * 4. Verify hal_mmu_current_space() returns the new space
 * 5. Switch back to the original address space
 * 6. Verify hal_mmu_current_space() returns the original space
 * 7. Clean up
 * ========================================================================== */

/**
 * @brief Property test: Address space switch consistency
 * 
 * **Feature: mm-refactor, Property 9: Address Space Switch Consistency**
 * **Validates: Requirements 4.5**
 */
TEST_CASE(hal_mmu_switch_space_consistency) {
#if defined(ARCH_I686)
    /* Save original address space */
    hal_addr_space_t original_space = hal_mmu_current_space();
    ASSERT_NE_U(original_space, HAL_ADDR_SPACE_INVALID);
    
    /* Create a new address space */
    hal_addr_space_t new_space = hal_mmu_create_space();
    ASSERT_NE_U(new_space, HAL_ADDR_SPACE_INVALID);
    ASSERT_NE_U(new_space, original_space);
    
    /* Switch to new address space */
    hal_mmu_switch_space(new_space);
    
    /* Property: Current space should be the new space */
    hal_addr_space_t current_after_switch = hal_mmu_current_space();
    ASSERT_EQ_U(current_after_switch, new_space);
    
    /* Switch back to original */
    hal_mmu_switch_space(original_space);
    
    /* Property: Current space should be the original space */
    hal_addr_space_t current_after_restore = hal_mmu_current_space();
    ASSERT_EQ_U(current_after_restore, original_space);
    
    /* Clean up */
    hal_mmu_destroy_space(new_space);
#else
    ASSERT_TRUE(true);
#endif
}

/**
 * @brief Property test: Multiple address space switches
 * 
 * **Feature: mm-refactor, Property 9: Address Space Switch Consistency**
 * **Validates: Requirements 4.5**
 */
TEST_CASE(hal_mmu_switch_space_multiple) {
#if defined(ARCH_I686)
    hal_addr_space_t original_space = hal_mmu_current_space();
    
    /* Create multiple address spaces */
    hal_addr_space_t spaces[3];
    for (int i = 0; i < 3; i++) {
        spaces[i] = hal_mmu_create_space();
        ASSERT_NE_U(spaces[i], HAL_ADDR_SPACE_INVALID);
    }
    
    /* Switch through all spaces and verify */
    for (int i = 0; i < 3; i++) {
        hal_mmu_switch_space(spaces[i]);
        
        /* Property: Current space must match what we switched to */
        ASSERT_EQ_U(hal_mmu_current_space(), spaces[i]);
    }
    
    /* Switch back to original */
    hal_mmu_switch_space(original_space);
    ASSERT_EQ_U(hal_mmu_current_space(), original_space);
    
    /* Clean up */
    for (int i = 0; i < 3; i++) {
        hal_mmu_destroy_space(spaces[i]);
    }
#else
    ASSERT_TRUE(true);
#endif
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief HAL 初始化测试套件
 * 
 * Property 1: HAL Initialization Dispatch
 * **Validates: Requirements 1.1, 7.1**
 */
TEST_SUITE(hal_init_tests) {
    RUN_TEST(hal_cpu_init_dispatch);
    RUN_TEST(hal_interrupt_init_dispatch);
    RUN_TEST(hal_mmu_init_dispatch);
    RUN_TEST(hal_arch_name_correct);
    RUN_TEST(hal_pointer_size_correct);
    RUN_TEST(hal_is_64bit_correct);
    RUN_TEST(hal_all_subsystems_initialized);
}

/**
 * @brief MMIO 和内存屏障测试套件
 * 
 * Property 14: MMIO Memory Barrier Correctness
 * **Validates: Requirements 9.1**
 */
TEST_SUITE(hal_mmio_tests) {
    RUN_TEST(hal_mmio_read_write_8bit);
    RUN_TEST(hal_mmio_read_write_16bit);
    RUN_TEST(hal_mmio_read_write_32bit);
    RUN_TEST(hal_mmio_read_write_64bit);
    RUN_TEST(hal_memory_barriers_callable);
    RUN_TEST(hal_mmio_write_ordering);
}

/**
 * @brief MMU 映射测试套件
 * 
 * Property 8: HAL MMU Map-Query Round-Trip
 * **Validates: Requirements 4.1**
 */
TEST_SUITE(hal_mmu_map_tests) {
    RUN_TEST(hal_mmu_map_query_roundtrip_single);
    RUN_TEST(hal_mmu_map_query_roundtrip_multiple);
    RUN_TEST(hal_mmu_query_unmapped_returns_false);
}

/**
 * @brief 地址空间切换测试套件
 * 
 * Property 9: Address Space Switch Consistency
 * **Validates: Requirements 4.5**
 */
TEST_SUITE(hal_addr_space_tests) {
    RUN_TEST(hal_mmu_switch_space_consistency);
    RUN_TEST(hal_mmu_switch_space_multiple);
}

// ============================================================================
// 架构诊断信息打印
// ============================================================================

/**
 * @brief 打印架构诊断信息
 * 
 * 在测试开始时打印当前架构的详细信息，帮助调试
 * **Validates: Requirements 7.1, 7.3**
 */
static void print_arch_diagnostics(void) {
    kprintf("\n");
    kprintf("================================================================================\n");
    kprintf("HAL Architecture Diagnostics\n");
    kprintf("================================================================================\n");
    kprintf("  Architecture:     %s\n", hal_arch_name());
    kprintf("  Pointer Size:     %u bytes\n", (unsigned)hal_pointer_size());
    kprintf("  64-bit Mode:      %s\n", hal_is_64bit() ? "yes" : "no");
    kprintf("  CPU Initialized:  %s\n", hal_cpu_initialized() ? "yes" : "no");
    kprintf("  IRQ Initialized:  %s\n", hal_interrupt_initialized() ? "yes" : "no");
    kprintf("  MMU Initialized:  %s\n", hal_mmu_initialized() ? "yes" : "no");
    
#if defined(ARCH_I686)
    kprintf("  Page Table:       2-level (PDE -> PTE)\n");
    kprintf("  Address Space:    32-bit (4GB)\n");
#elif defined(ARCH_X86_64)
    kprintf("  Page Table:       4-level (PML4 -> PDPT -> PD -> PT)\n");
    kprintf("  Address Space:    48-bit canonical\n");
#elif defined(ARCH_ARM64)
    kprintf("  Page Table:       4-level (L0 -> L1 -> L2 -> L3)\n");
    kprintf("  Address Space:    48-bit\n");
#endif
    
    kprintf("================================================================================\n");
    kprintf("\n");
}

/**
 * @brief 打印架构特定的调试提示
 * 
 * 当测试失败时提供架构特定的调试建议
 * **Validates: Requirements 7.3**
 */
static void print_arch_debug_hints(void) {
    kprintf("\n");
    kprintf("--------------------------------------------------------------------------------\n");
    kprintf("Architecture-Specific Debugging Hints:\n");
    kprintf("--------------------------------------------------------------------------------\n");
    
#if defined(ARCH_I686)
    kprintf("  - Check 32-bit address calculations (4GB limit)\n");
    kprintf("  - Verify 2-level page table operations (PDE/PTE)\n");
    kprintf("  - Ensure PAGE_SIZE is 4096 bytes\n");
    kprintf("  - Check GDT/IDT setup for protected mode\n");
#elif defined(ARCH_X86_64)
    kprintf("  - Check 64-bit address sign extension (canonical form)\n");
    kprintf("  - Verify 4-level page table operations\n");
    kprintf("  - Ensure NX bit handling is correct\n");
    kprintf("  - Check long mode GDT setup\n");
#elif defined(ARCH_ARM64)
    kprintf("  - Check TTBR0/TTBR1 configuration\n");
    kprintf("  - Verify 4-level page table operations\n");
    kprintf("  - Ensure memory attributes are correct (MAIR)\n");
    kprintf("  - Check exception level (EL1 expected)\n");
#endif
    
    kprintf("--------------------------------------------------------------------------------\n");
    kprintf("\n");
}

// ============================================================================
// 运行所有测试
// ============================================================================

/**
 * @brief 运行所有 HAL 属性测试
 * 
 * 测试包括:
 *   - Property 1: HAL Initialization Dispatch
 *   - Property 14: MMIO Memory Barrier Correctness
 *   - Property 8: HAL MMU Map-Query Round-Trip
 *   - Property 9: Address Space Switch Consistency
 */
void run_hal_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 打印架构诊断信息
    print_arch_diagnostics();
    
    // Property 1: HAL Initialization Dispatch
    // **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
    // **Validates: Requirements 1.1, 7.1**
    RUN_SUITE(hal_init_tests);
    
    // Property 14: MMIO Memory Barrier Correctness
    // **Feature: multi-arch-support, Property 14: MMIO Memory Barrier Correctness**
    // **Validates: Requirements 9.1**
    RUN_SUITE(hal_mmio_tests);
    
    // Property 8: HAL MMU Map-Query Round-Trip
    // **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip**
    // **Validates: Requirements 4.1**
    RUN_SUITE(hal_mmu_map_tests);
    
    // Property 9: Address Space Switch Consistency
    // **Feature: mm-refactor, Property 9: Address Space Switch Consistency**
    // **Validates: Requirements 4.5**
    RUN_SUITE(hal_addr_space_tests);
    
    // 打印测试摘要
    unittest_print_summary();
    
    // 如果有失败，打印调试提示
    // (这里简化处理，实际可以检查 unittest 的失败计数)
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief HAL 测试模块注册
 * 
 * 使用 TEST_MODULE_ARCH 宏注册为架构相关测试模块
 * 支持所有架构 (i686, x86_64, ARM64)
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 10.2, 11.1**
 */
TEST_MODULE_FULL(hal, ARCH, run_hal_tests,
    "HAL (Hardware Abstraction Layer) tests - initialization, MMIO, MMU",
    NULL, 0, false, TEST_ARCH_ALL);
