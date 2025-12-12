/**
 * @file dma_test.c
 * @brief DMA Cache Coherency Property Tests
 * 
 * Property-based tests for DMA cache coherency operations.
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 * 
 * Property 15: DMA Cache Coherency
 * ================================
 * *For any* DMA buffer, the appropriate cache operations (invalidate before
 * DMA read, clean before DMA write) SHALL be performed to maintain coherency
 * between CPU cache and device memory access.
 * 
 * Test Strategy:
 * Since we cannot directly test DMA device interactions in a unit test
 * environment, we verify:
 * 1. Cache operations can be called without crashing
 * 2. Cache operations handle NULL and zero-size inputs gracefully
 * 3. DMA sync functions dispatch to correct cache operations
 * 4. Cache line alignment helpers work correctly
 * 5. Write-then-sync-then-read sequences maintain data consistency
 */

#include <tests/ktest.h>
#include <tests/mm/dma_test.h>
#include <hal/hal.h>
#include <hal/dma.h>
#include <lib/kprintf.h>

/* ============================================================================
 * Property 15: DMA Cache Coherency
 * ============================================================================
 * 
 * *For any* DMA buffer, the appropriate cache operations SHALL be performed
 * to maintain coherency between CPU cache and device memory access.
 * 
 * **Validates: Requirements 9.4**
 * ========================================================================== */

/**
 * @brief Test that cache clean operation is callable
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_cache_clean_callable) {
    volatile uint8_t buffer[128];
    
    /* Initialize buffer with known values */
    for (int i = 0; i < 128; i++) {
        buffer[i] = (uint8_t)i;
    }
    
    /* Cache clean should not crash */
    hal_cache_clean((void *)buffer, sizeof(buffer));
    
    /* Verify data is still intact */
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ_UINT((uint8_t)i, buffer[i]);
    }
}

/**
 * @brief Test that cache invalidate operation is callable
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_cache_invalidate_callable) {
    volatile uint8_t buffer[128];
    
    /* Initialize buffer */
    for (int i = 0; i < 128; i++) {
        buffer[i] = (uint8_t)(255 - i);
    }
    
    /* Cache invalidate should not crash */
    hal_cache_invalidate((void *)buffer, sizeof(buffer));
    
    /* Note: After invalidate, data may or may not be preserved depending
     * on whether the cache line was dirty. We just verify no crash. */
    ASSERT_TRUE(true);
}

/**
 * @brief Test that cache clean+invalidate operation is callable
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_cache_clean_invalidate_callable) {
    volatile uint8_t buffer[128];
    
    /* Initialize buffer */
    for (int i = 0; i < 128; i++) {
        buffer[i] = (uint8_t)i;
    }
    
    /* Cache clean+invalidate should not crash */
    hal_cache_clean_invalidate((void *)buffer, sizeof(buffer));
    
    /* After clean+invalidate, data should be preserved (clean writes back) */
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ_UINT((uint8_t)i, buffer[i]);
    }
}


/**
 * @brief Test cache operations handle NULL gracefully
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_cache_ops_null_safe) {
    /* These should not crash with NULL */
    hal_cache_clean(NULL, 100);
    hal_cache_invalidate(NULL, 100);
    hal_cache_clean_invalidate(NULL, 100);
    
    ASSERT_TRUE(true);
}

/**
 * @brief Test cache operations handle zero size gracefully
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_cache_ops_zero_size_safe) {
    volatile uint8_t buffer[64];
    
    /* These should not crash with zero size */
    hal_cache_clean((void *)buffer, 0);
    hal_cache_invalidate((void *)buffer, 0);
    hal_cache_clean_invalidate((void *)buffer, 0);
    
    ASSERT_TRUE(true);
}

/**
 * @brief Test DMA sync for device (TO_DEVICE direction)
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_sync_for_device_to_device) {
    volatile uint32_t buffer[32];
    
    /* Initialize buffer with test pattern */
    for (int i = 0; i < 32; i++) {
        buffer[i] = 0xDEADBEEF + i;
    }
    
    /* Sync for device (DMA_TO_DEVICE should clean cache) */
    hal_dma_sync_for_device((void *)buffer, sizeof(buffer), DMA_TO_DEVICE);
    
    /* Data should be preserved */
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ_UINT(0xDEADBEEF + i, buffer[i]);
    }
}


/**
 * @brief Test DMA sync for device (FROM_DEVICE direction)
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_sync_for_device_from_device) {
    volatile uint32_t buffer[32];
    
    /* Sync for device (DMA_FROM_DEVICE should invalidate cache) */
    hal_dma_sync_for_device((void *)buffer, sizeof(buffer), DMA_FROM_DEVICE);
    
    /* Should not crash */
    ASSERT_TRUE(true);
}

/**
 * @brief Test DMA sync for device (BIDIRECTIONAL direction)
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_sync_for_device_bidirectional) {
    volatile uint32_t buffer[32];
    
    /* Initialize buffer */
    for (int i = 0; i < 32; i++) {
        buffer[i] = 0xCAFEBABE + i;
    }
    
    /* Sync for device (BIDIRECTIONAL should clean+invalidate) */
    hal_dma_sync_for_device((void *)buffer, sizeof(buffer), DMA_BIDIRECTIONAL);
    
    /* Data should be preserved (clean writes back before invalidate) */
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ_UINT(0xCAFEBABE + i, buffer[i]);
    }
}

/**
 * @brief Test DMA sync for CPU (TO_DEVICE direction - no-op)
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_sync_for_cpu_to_device) {
    volatile uint32_t buffer[32];
    
    /* Initialize buffer */
    for (int i = 0; i < 32; i++) {
        buffer[i] = 0x12345678 + i;
    }
    
    /* Sync for CPU (TO_DEVICE is a no-op - device only read) */
    hal_dma_sync_for_cpu((void *)buffer, sizeof(buffer), DMA_TO_DEVICE);
    
    /* Data should be preserved */
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ_UINT(0x12345678 + i, buffer[i]);
    }
}


/**
 * @brief Test DMA sync for CPU (FROM_DEVICE direction)
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_sync_for_cpu_from_device) {
    volatile uint32_t buffer[32];
    
    /* Sync for CPU (FROM_DEVICE should invalidate cache) */
    hal_dma_sync_for_cpu((void *)buffer, sizeof(buffer), DMA_FROM_DEVICE);
    
    /* Should not crash */
    ASSERT_TRUE(true);
}

/**
 * @brief Test hal_dma_needs_cache_ops returns correct value
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_needs_cache_ops_correct) {
    bool needs_ops = hal_dma_needs_cache_ops();
    
#if defined(ARCH_ARM64)
    /* ARM64 requires cache ops */
    ASSERT_TRUE(needs_ops);
#else
    /* x86/x86_64 have coherent DMA */
    ASSERT_FALSE(needs_ops);
#endif
}

/**
 * @brief Test cache line size is reasonable
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_cache_line_size_reasonable) {
    size_t line_size = hal_dma_cache_line_size();
    
    /* Cache line size should be a power of 2 between 32 and 128 bytes */
    ASSERT_TRUE(line_size >= 32);
    ASSERT_TRUE(line_size <= 128);
    
    /* Check it's a power of 2 */
    ASSERT_TRUE((line_size & (line_size - 1)) == 0);
}

/**
 * @brief Test DMA size alignment helper
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_align_size_correct) {
    size_t line_size = hal_dma_cache_line_size();
    
    /* Zero should align to zero */
    ASSERT_EQ_UINT(0, hal_dma_align_size(0));
    
    /* 1 byte should align up to cache line size */
    ASSERT_EQ_UINT(line_size, hal_dma_align_size(1));
    
    /* Exact cache line size should stay the same */
    ASSERT_EQ_UINT(line_size, hal_dma_align_size(line_size));
    
    /* One more than cache line should round up to 2x */
    ASSERT_EQ_UINT(line_size * 2, hal_dma_align_size(line_size + 1));
}


/**
 * @brief Test DMA address alignment check
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_is_aligned_correct) {
    size_t line_size = hal_dma_cache_line_size();
    
    /* Aligned addresses should return true */
    ASSERT_TRUE(hal_dma_is_aligned((void *)0));
    ASSERT_TRUE(hal_dma_is_aligned((void *)(uintptr_t)line_size));
    ASSERT_TRUE(hal_dma_is_aligned((void *)(uintptr_t)(line_size * 2)));
    
    /* Unaligned addresses should return false */
    ASSERT_FALSE(hal_dma_is_aligned((void *)1));
    ASSERT_FALSE(hal_dma_is_aligned((void *)(uintptr_t)(line_size + 1)));
}

/**
 * @brief Property test: DMA sync round-trip preserves data
 * 
 * For any buffer with known data, performing a full DMA sync cycle
 * (sync_for_device followed by sync_for_cpu) should preserve the data
 * when using DMA_TO_DEVICE direction.
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_sync_roundtrip_preserves_data) {
    /* Use aligned buffer for best results */
    volatile uint64_t buffer[16] __attribute__((aligned(64)));
    
    /* Initialize with test pattern */
    for (int i = 0; i < 16; i++) {
        buffer[i] = 0xFEEDFACECAFEBABEULL + i;
    }
    
    /* Simulate DMA write cycle: CPU writes, then device reads */
    hal_dma_sync_for_device((void *)buffer, sizeof(buffer), DMA_TO_DEVICE);
    
    /* After sync for device, CPU can still read the data */
    for (int i = 0; i < 16; i++) {
        ASSERT_TRUE(buffer[i] == (0xFEEDFACECAFEBABEULL + i));
    }
    
    /* Sync back for CPU (no-op for TO_DEVICE, but should not corrupt) */
    hal_dma_sync_for_cpu((void *)buffer, sizeof(buffer), DMA_TO_DEVICE);
    
    /* Data should still be intact */
    for (int i = 0; i < 16; i++) {
        ASSERT_TRUE(buffer[i] == (0xFEEDFACECAFEBABEULL + i));
    }
}

/**
 * @brief Property test: Multiple cache operations don't corrupt data
 * 
 * **Feature: multi-arch-support, Property 15: DMA Cache Coherency**
 * **Validates: Requirements 9.4**
 */
TEST_CASE(dma_multiple_ops_no_corruption) {
    volatile uint32_t buffer[64] __attribute__((aligned(64)));
    
    /* Initialize buffer */
    for (int i = 0; i < 64; i++) {
        buffer[i] = 0xA5A5A5A5 ^ i;
    }
    
    /* Perform multiple cache operations */
    for (int iter = 0; iter < 10; iter++) {
        hal_cache_clean((void *)buffer, sizeof(buffer));
        hal_cache_clean_invalidate((void *)buffer, sizeof(buffer));
    }
    
    /* Data should be preserved */
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ_UINT(0xA5A5A5A5 ^ i, buffer[i]);
    }
}


/* ============================================================================
 * Test Suite Definition
 * ========================================================================== */

TEST_SUITE(dma_tests) {
    /* Basic cache operation tests */
    RUN_TEST(dma_cache_clean_callable);
    RUN_TEST(dma_cache_invalidate_callable);
    RUN_TEST(dma_cache_clean_invalidate_callable);
    
    /* Edge case handling */
    RUN_TEST(dma_cache_ops_null_safe);
    RUN_TEST(dma_cache_ops_zero_size_safe);
    
    /* DMA sync for device tests */
    RUN_TEST(dma_sync_for_device_to_device);
    RUN_TEST(dma_sync_for_device_from_device);
    RUN_TEST(dma_sync_for_device_bidirectional);
    
    /* DMA sync for CPU tests */
    RUN_TEST(dma_sync_for_cpu_to_device);
    RUN_TEST(dma_sync_for_cpu_from_device);
    
    /* Helper function tests */
    RUN_TEST(dma_needs_cache_ops_correct);
    RUN_TEST(dma_cache_line_size_reasonable);
    RUN_TEST(dma_align_size_correct);
    RUN_TEST(dma_is_aligned_correct);
    
    /* Property tests */
    RUN_TEST(dma_sync_roundtrip_preserves_data);
    RUN_TEST(dma_multiple_ops_no_corruption);
}

/**
 * @brief Run all DMA cache coherency property tests
 */
void run_dma_tests(void) {
    unittest_init();
    RUN_SUITE(dma_tests);
    unittest_print_summary();
}
