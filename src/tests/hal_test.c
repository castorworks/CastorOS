/**
 * @file hal_test.c
 * @brief HAL (Hardware Abstraction Layer) Property Tests
 * 
 * Property-based tests for HAL initialization and I/O operations.
 * 
 * **Feature: multi-arch-support**
 * **Property 1: HAL Initialization Dispatch**
 * **Property 14: MMIO Memory Barrier Correctness**
 * **Validates: Requirements 1.1, 9.1**
 */

#include <tests/ktest.h>
#include <tests/hal_test.h>
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
 * Test Suite Definition
 * ========================================================================== */

TEST_SUITE(hal_tests) {
    /* Property 1: HAL Initialization Dispatch */
    RUN_TEST(hal_cpu_init_dispatch);
    RUN_TEST(hal_interrupt_init_dispatch);
    RUN_TEST(hal_mmu_init_dispatch);
    RUN_TEST(hal_arch_name_correct);
    RUN_TEST(hal_pointer_size_correct);
    RUN_TEST(hal_is_64bit_correct);
    RUN_TEST(hal_all_subsystems_initialized);
    
    /* Property 14: MMIO Memory Barrier Correctness */
    RUN_TEST(hal_mmio_read_write_8bit);
    RUN_TEST(hal_mmio_read_write_16bit);
    RUN_TEST(hal_mmio_read_write_32bit);
    RUN_TEST(hal_mmio_read_write_64bit);
    RUN_TEST(hal_memory_barriers_callable);
    RUN_TEST(hal_mmio_write_ordering);
}

/**
 * @brief Run all HAL property tests
 */
void run_hal_tests(void) {
    unittest_init();
    RUN_SUITE(hal_tests);
    unittest_print_summary();
}
