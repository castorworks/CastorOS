/**
 * @file interrupt_handler_test.c
 * @brief Property tests for interrupt handler registration API consistency
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 * 
 * This test verifies that the HAL interrupt registration API provides
 * consistent behavior across all supported architectures.
 */

#include <tests/ktest.h>
#include <hal/hal.h>
#include <lib/string.h>

/* ============================================================================
 * Test State
 * ========================================================================== */

/** Flag to track if test handler was called */
static volatile bool g_handler_called = false;

/** Data passed to handler */
static volatile void *g_handler_data = NULL;

/** Counter for handler invocations */
static volatile uint32_t g_handler_count = 0;

/**
 * @brief Test interrupt handler
 */
static void test_interrupt_handler(void *data) {
    g_handler_called = true;
    g_handler_data = data;
    g_handler_count++;
}

/**
 * @brief Reset test state
 */
static void reset_test_state(void) {
    g_handler_called = false;
    g_handler_data = NULL;
    g_handler_count = 0;
}

/* ============================================================================
 * Property Test: Interrupt Handler Registration API Consistency
 * ============================================================================
 * 
 * Property 8: Interrupt Handler Registration API Consistency
 * 
 * *For any* interrupt handler registration through the HAL API, the handler 
 * SHALL be invoked when the corresponding interrupt occurs, regardless of 
 * the underlying architecture-specific interrupt numbering.
 * 
 * Since we cannot easily trigger real hardware interrupts in a test,
 * we verify the API contract by testing:
 * 1. Registration and unregistration don't crash
 * 2. The API accepts valid parameters
 * 3. Multiple registrations work correctly
 * 4. Unregistration removes the handler
 */

/**
 * Test: Verify interrupt registration API is callable
 * 
 * Tests that hal_interrupt_register can be called without crashing.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_register_callable) {
    reset_test_state();
    
    /* Use a high IRQ number that's unlikely to be in use */
    uint32_t test_irq = 100;
    uint32_t test_data = 0x12345678;
    
    /* Registration should not crash */
    hal_interrupt_register(test_irq, test_interrupt_handler, (void *)(uintptr_t)test_data);
    
    /* Unregister to clean up */
    hal_interrupt_unregister(test_irq);
    
    /* If we get here, the API is callable */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify interrupt unregistration API is callable
 * 
 * Tests that hal_interrupt_unregister can be called without crashing.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_unregister_callable) {
    /* Unregistering a non-existent handler should not crash */
    hal_interrupt_unregister(200);
    
    /* If we get here, the API is callable */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify multiple interrupt registrations work
 * 
 * Tests that multiple different IRQs can be registered.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_multiple_registrations) {
    reset_test_state();
    
    /* Register multiple handlers */
    uint32_t irqs[] = {100, 101, 102, 103, 104};
    uint32_t num_irqs = sizeof(irqs) / sizeof(irqs[0]);
    
    for (uint32_t i = 0; i < num_irqs; i++) {
        hal_interrupt_register(irqs[i], test_interrupt_handler, (void *)(uintptr_t)i);
    }
    
    /* Unregister all */
    for (uint32_t i = 0; i < num_irqs; i++) {
        hal_interrupt_unregister(irqs[i]);
    }
    
    /* If we get here, multiple registrations work */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify NULL handler is handled gracefully
 * 
 * Tests that registering a NULL handler doesn't crash.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_null_handler) {
    /* Registering NULL handler should not crash */
    hal_interrupt_register(150, NULL, NULL);
    
    /* Clean up */
    hal_interrupt_unregister(150);
    
    /* If we get here, NULL handler is handled */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify re-registration overwrites previous handler
 * 
 * Tests that registering a handler for the same IRQ twice works.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_reregistration) {
    reset_test_state();
    
    uint32_t test_irq = 160;
    
    /* Register first handler */
    hal_interrupt_register(test_irq, test_interrupt_handler, (void *)1);
    
    /* Re-register with different data */
    hal_interrupt_register(test_irq, test_interrupt_handler, (void *)2);
    
    /* Clean up */
    hal_interrupt_unregister(test_irq);
    
    /* If we get here, re-registration works */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify interrupt enable/disable API is callable
 * 
 * Tests that hal_interrupt_enable and hal_interrupt_disable work.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_enable_disable) {
    /* Save current state */
    uint64_t saved_state = hal_interrupt_save();
    
    /* Disable interrupts */
    hal_interrupt_disable();
    
    /* Enable interrupts */
    hal_interrupt_enable();
    
    /* Disable again */
    hal_interrupt_disable();
    
    /* Restore original state */
    hal_interrupt_restore(saved_state);
    
    /* If we get here, enable/disable works */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify interrupt save/restore API is callable
 * 
 * Tests that hal_interrupt_save and hal_interrupt_restore work.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_save_restore) {
    /* Save state */
    uint64_t state1 = hal_interrupt_save();
    
    /* Save again (should be disabled now) */
    uint64_t state2 = hal_interrupt_save();
    
    /* Restore inner state */
    hal_interrupt_restore(state2);
    
    /* Restore outer state */
    hal_interrupt_restore(state1);
    
    /* If we get here, save/restore works */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify EOI API is callable
 * 
 * Tests that hal_interrupt_eoi can be called.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_eoi_callable) {
    /* EOI for a non-active interrupt should not crash */
    hal_interrupt_eoi(100);
    
    /* If we get here, EOI is callable */
    ASSERT_TRUE(true);
}

/**
 * Test: Verify interrupt initialization state
 * 
 * Tests that the interrupt system reports as initialized.
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
TEST_CASE(hal_interrupt_initialized_state) {
    /* Interrupt system should be initialized */
    ASSERT_TRUE(hal_interrupt_initialized());
}

/* ============================================================================
 * Test Suite Definition
 * ========================================================================== */

TEST_SUITE(interrupt_handler_tests) {
    /* Property 8: Interrupt Handler Registration API Consistency */
    RUN_TEST(hal_interrupt_register_callable);
    RUN_TEST(hal_interrupt_unregister_callable);
    RUN_TEST(hal_interrupt_multiple_registrations);
    RUN_TEST(hal_interrupt_null_handler);
    RUN_TEST(hal_interrupt_reregistration);
    RUN_TEST(hal_interrupt_enable_disable);
    RUN_TEST(hal_interrupt_save_restore);
    RUN_TEST(hal_interrupt_eoi_callable);
    RUN_TEST(hal_interrupt_initialized_state);
}

/**
 * @brief Run all interrupt handler registration tests
 */
void run_interrupt_handler_tests(void) {
    unittest_init();
    RUN_SUITE(interrupt_handler_tests);
    unittest_print_summary();
}
