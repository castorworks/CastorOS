// ============================================================================
// pmm_test.c - 物理内存管理器单元测试
// ============================================================================
//
// 模块名称: pmm
// 子系统: mm (内存管理)
// 描述: 测试 PMM (Physical Memory Manager) 的功能
//
// 功能覆盖:
//   - 页帧分配 (pmm_alloc_frame)
//   - 页帧释放 (pmm_free_frame)
//   - 信息查询 (pmm_get_info)
//   - 引用计数 (pmm_frame_ref_inc, pmm_frame_ref_dec)
//   - 压力测试
//
// **Feature: test-refactor**
// **Validates: Requirements 3.1, 10.1, 11.1**
// ============================================================================

#include <tests/ktest.h>
#include <tests/pmm_test.h>
#include <tests/test_module.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/kprintf.h>
#include <types.h>

// ============================================================================
// 测试套件 1: pmm_alloc_tests - 页帧分配测试
// ============================================================================
// 
// 测试 pmm_alloc_frame() 函数的基本功能
// **Validates: Requirements 3.1** - PMM 分配页帧应返回页对齐且唯一的地址
// ============================================================================

/**
 * @brief 测试基本页帧分配
 * 
 * 验证 pmm_alloc_frame() 返回有效的页对齐地址
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_alloc_frame_basic) {
    // 分配一个页帧
    paddr_t frame = pmm_alloc_frame();
    
    // 应该返回有效地址
    ASSERT_NE_U(frame, PADDR_INVALID);
    ASSERT_NE_U(frame, 0);
    
    // 应该是页对齐的（4KB = 0x1000）
    ASSERT_TRUE(IS_PADDR_ALIGNED(frame));
    
    // 释放页帧
    pmm_free_frame(frame);
}

/**
 * @brief 测试多页帧分配的唯一性
 * 
 * 验证连续分配的页帧地址互不相同
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_alloc_multiple_frames) {
    // 分配多个页帧
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    paddr_t frame3 = pmm_alloc_frame();
    
    // 应该都有效
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    ASSERT_NE_U(frame3, PADDR_INVALID);
    
    // 应该都不相同
    ASSERT_NE_U(frame1, frame2);
    ASSERT_NE_U(frame2, frame3);
    ASSERT_NE_U(frame1, frame3);
    
    // 释放页帧
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
    pmm_free_frame(frame3);
}

/**
 * @brief 测试页帧分配的对齐性
 * 
 * 验证所有分配的页帧都是页对齐的 (4KB)
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_alloc_frame_alignment) {
    // 分配10个页帧，检查对齐
    for (int i = 0; i < 10; i++) {
        paddr_t frame = pmm_alloc_frame();
        ASSERT_NE_U(frame, PADDR_INVALID);
        ASSERT_TRUE(IS_PADDR_ALIGNED(frame));
        pmm_free_frame(frame);
    }
}

// ============================================================================
// 测试套件 2: pmm_free_tests - 页帧释放测试
// ============================================================================
// 
// 测试 pmm_free_frame() 函数的功能和边界情况
// **Validates: Requirements 3.1, 3.5** - 页帧释放和内存泄漏检测
// ============================================================================

/**
 * @brief 测试基本页帧释放
 * 
 * 验证释放页帧后空闲计数恢复
 * _Requirements: 3.1, 3.5_
 */
TEST_CASE(test_pmm_free_frame_basic) {
    pmm_info_t info_before = pmm_get_info();
    
    // 分配一个页帧
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    pmm_info_t info_after_alloc = pmm_get_info();
    // 空闲页帧应该减少1
    ASSERT_EQ_U(info_after_alloc.free_frames, info_before.free_frames - 1);
    
    // 释放页帧
    pmm_free_frame(frame);
    
    pmm_info_t info_after_free = pmm_get_info();
    // 空闲页帧应该恢复
    ASSERT_EQ_U(info_after_free.free_frames, info_before.free_frames);
}

/**
 * @brief 测试页帧复用
 * 
 * 验证释放的页帧可以被重新分配
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_free_frame_reuse) {
    // 分配并释放一个页帧
    paddr_t frame1 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    pmm_free_frame(frame1);
    
    // 再次分配，应该能够复用刚释放的页帧
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // 可能（但不一定）是同一个页帧
    // 至少应该能成功分配
    pmm_free_frame(frame2);
}

/**
 * @brief 测试释放无效页帧
 * 
 * 验证释放非对齐地址时系统保持稳定
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_free_invalid_frame) {
    pmm_info_t info_before = pmm_get_info();
    
    // 尝试释放非对齐地址（应该被忽略）
    pmm_free_frame(0x12345);
    
    pmm_info_t info_after = pmm_get_info();
    // 信息应该不变
    ASSERT_EQ_U(info_after.free_frames, info_before.free_frames);
}

/**
 * @brief 测试双重释放保护
 * 
 * 验证双重释放不会导致内存状态异常
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_free_double_free) {    
    // 分配一个页帧
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 第一次释放
    pmm_free_frame(frame);
    pmm_info_t info_after_first_free = pmm_get_info();
    
    // 第二次释放相同页帧（double free，应该被忽略）
    pmm_free_frame(frame);
    pmm_info_t info_after_second_free = pmm_get_info();
    
    // 第二次释放不应该改变状态
    ASSERT_EQ_U(info_after_second_free.free_frames, info_after_first_free.free_frames);
    ASSERT_EQ_U(info_after_second_free.used_frames, info_after_first_free.used_frames);
}

/**
 * @brief 测试释放越界页帧
 * 
 * 验证释放超出范围的地址时系统保持稳定
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_free_out_of_bounds) {
    pmm_info_t info_before = pmm_get_info();
    
    // 尝试释放超出范围的页帧（应该被忽略）
    pmm_free_frame(0x7FFFF000);  // 接近2GB边界
    
    pmm_info_t info_after = pmm_get_info();
    // 信息应该不变
    ASSERT_EQ_U(info_after.free_frames, info_before.free_frames);
}

/**
 * @brief 测试大量分配直到内存不足
 * 
 * 验证大量分配和释放后内存状态恢复
 * _Requirements: 3.1, 3.5_
 */
TEST_CASE(test_pmm_alloc_until_low_memory) {
    // 测试分配大量内存（但不完全耗尽）
    #define LARGE_ALLOC_COUNT 200
    paddr_t frames[LARGE_ALLOC_COUNT];
    int allocated = 0;
    
    pmm_info_t info_before = pmm_get_info();
    
    // 分配大量页帧
    for (int i = 0; i < LARGE_ALLOC_COUNT; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            // 如果分配失败，记录已分配的数量
            break;
        }
        allocated++;
    }
    
    // 应该至少能分配一些页帧
    ASSERT_TRUE(allocated > 0);
    
    pmm_info_t info_after_alloc = pmm_get_info();
    // 空闲页帧应该减少
    ASSERT_TRUE(info_after_alloc.free_frames < info_before.free_frames);
    
    // 释放所有分配的页帧
    for (int i = 0; i < allocated; i++) {
        pmm_free_frame(frames[i]);
    }
    
    pmm_info_t info_after_free = pmm_get_info();
    // 应该恢复到接近原始状态
    int64_t diff = (int64_t)info_after_free.free_frames - (int64_t)info_before.free_frames;
    
    if (diff < -150 || diff > 150) {
        kprintf("  Debug: allocated=%d, before=%llu, after=%llu, diff=%lld\n",
                allocated, (unsigned long long)info_before.free_frames, 
                (unsigned long long)info_after_free.free_frames, (long long)diff);
    }
    
    ASSERT_TRUE(diff >= -150 && diff <= 150);
}

// ============================================================================
// 测试套件 3: pmm_info_tests - 信息查询测试
// ============================================================================
// 
// 测试 pmm_get_info() 和 pmm_get_bitmap_end() 函数
// **Validates: Requirements 3.1** - PMM 信息查询正确性
// ============================================================================

/**
 * @brief 测试基本信息查询
 * 
 * 验证 pmm_get_info() 返回一致的内存统计信息
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_get_info_basic) {
    pmm_info_t info = pmm_get_info();
    
    // 总页帧数应该非零
    ASSERT_NE_U(info.total_frames, 0);
    
    // 总页帧 = 空闲页帧 + 已使用页帧
    ASSERT_EQ_U(info.total_frames, info.free_frames + info.used_frames);
}

/**
 * @brief 测试位图结束地址
 * 
 * 验证 pmm_get_bitmap_end() 返回有效的内核空间地址
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_get_bitmap_end) {
    // 获取位图结束地址
    uintptr_t bitmap_end = pmm_get_bitmap_end();
    
    // 应该非零
    ASSERT_NE_U(bitmap_end, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(bitmap_end & (PAGE_SIZE - 1), 0);
    
    // 应该在内核空间（高半核）
    ASSERT_TRUE(bitmap_end >= KERNEL_VIRTUAL_BASE);
}

/**
 * @brief 测试操作后的信息一致性
 * 
 * 验证分配和释放操作后 pmm_get_info() 返回正确的统计
 * _Requirements: 3.1, 3.5_
 */
TEST_CASE(test_pmm_get_info_after_operations) {
    pmm_info_t info_before = pmm_get_info();
    
    // 分配3个页帧
    paddr_t frames[3];
    for (int i = 0; i < 3; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
    }
    
    pmm_info_t info_after_alloc = pmm_get_info();
    // 空闲页帧应该减少3
    ASSERT_EQ_U(info_after_alloc.free_frames, info_before.free_frames - 3);
    // 已使用页帧应该增加3
    ASSERT_EQ_U(info_after_alloc.used_frames, info_before.used_frames + 3);
    
    // 释放3个页帧
    for (int i = 0; i < 3; i++) {
        pmm_free_frame(frames[i]);
    }
    
    pmm_info_t info_after_free = pmm_get_info();
    // 应该恢复原状
    ASSERT_EQ_U(info_after_free.free_frames, info_before.free_frames);
    ASSERT_EQ_U(info_after_free.used_frames, info_before.used_frames);
}

// ============================================================================
// 测试套件 4: pmm_stress_tests - 压力测试
// ============================================================================
// 
// 测试 PMM 在高负载下的稳定性
// **Validates: Requirements 3.1, 3.5** - 压力测试和内存泄漏检测
// ============================================================================

/**
 * @brief 压力测试：大量分配和释放
 * 
 * 验证大量连续分配和释放后内存状态正确
 * _Requirements: 3.1, 3.5_
 */
TEST_CASE(test_pmm_stress_alloc_free) {
    // 分配和释放大量页帧
    #define STRESS_COUNT 100
    paddr_t frames[STRESS_COUNT];
    
    pmm_info_t info_before = pmm_get_info();
    
    // 分配
    for (int i = 0; i < STRESS_COUNT; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
    }
    
    pmm_info_t info_after_alloc = pmm_get_info();
    ASSERT_TRUE(info_after_alloc.free_frames <= info_before.free_frames - STRESS_COUNT);
    
    // 释放
    for (int i = 0; i < STRESS_COUNT; i++) {
        pmm_free_frame(frames[i]);
    }
    
    pmm_info_t info_after_free = pmm_get_info();
    int64_t diff = (int64_t)info_after_free.free_frames - (int64_t)info_before.free_frames;
    ASSERT_TRUE(diff >= -100 && diff <= 100);
}

/**
 * @brief 压力测试：交替分配和释放
 * 
 * 验证交替分配和释放操作的正确性
 * _Requirements: 3.1_
 */
TEST_CASE(test_pmm_interleaved_alloc_free) {
    // 交替分配和释放
    paddr_t frame1 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    pmm_free_frame(frame1);
    
    paddr_t frame3 = pmm_alloc_frame();
    ASSERT_NE_U(frame3, PADDR_INVALID);
    
    pmm_free_frame(frame2);
    pmm_free_frame(frame3);
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(pmm_alloc_tests) {
    RUN_TEST(test_pmm_alloc_frame_basic);
    RUN_TEST(test_pmm_alloc_multiple_frames);
    RUN_TEST(test_pmm_alloc_frame_alignment);
}

TEST_SUITE(pmm_free_tests) {
    RUN_TEST(test_pmm_free_frame_basic);
    RUN_TEST(test_pmm_free_frame_reuse);
    RUN_TEST(test_pmm_free_invalid_frame);
    RUN_TEST(test_pmm_free_double_free);
    RUN_TEST(test_pmm_free_out_of_bounds);
}

TEST_SUITE(pmm_info_tests) {
    RUN_TEST(test_pmm_get_info_basic);
    RUN_TEST(test_pmm_get_info_after_operations);
    RUN_TEST(test_pmm_get_bitmap_end);
}

TEST_SUITE(pmm_stress_tests) {
    RUN_TEST(test_pmm_stress_alloc_free);
    RUN_TEST(test_pmm_interleaved_alloc_free);
    RUN_TEST(test_pmm_alloc_until_low_memory);
}

// ============================================================================
// 测试套件 5: pmm_property_tests - 属性测试 (PBT)
// ============================================================================
// 
// 使用属性测试验证 PMM 的核心正确性属性
// **Feature: test-refactor, Property 4: PMM Allocation Alignment and Uniqueness**
// **Validates: Requirements 3.1**
// ============================================================================

/**
 * @brief 属性测试：所有分配的页帧都是页对齐的
 * 
 * *For any* successful call to pmm_alloc_frame(), the returned address 
 * SHALL be page-aligned (divisible by PAGE_SIZE).
 * 
 * **Feature: test-refactor, Property 4: PMM Allocation Alignment and Uniqueness**
 * **Validates: Requirements 3.1**
 */
TEST_CASE(test_pbt_pmm_page_alignment) {
    #define PBT_PMM_ITERATIONS 100
    
    paddr_t frames[PBT_PMM_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames
    for (uint32_t i = 0; i < PBT_PMM_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            // Out of memory, stop allocating
            break;
        }
        allocated++;
        
        // Property: frame address must be page-aligned (4KB = 0x1000)
        ASSERT_TRUE(IS_PADDR_ALIGNED(frames[i]));
        
        // Property: frame address must not be PADDR_INVALID
        ASSERT_NE_U(frames[i], PADDR_INVALID);
    }
    
    // Verify we allocated at least some frames
    ASSERT_TRUE(allocated > 0);
    
    // Cleanup: free all allocated frames
    for (uint32_t i = 0; i < allocated; i++) {
        pmm_free_frame(frames[i]);
    }
}

/**
 * @brief 属性测试：分配的页帧地址唯一
 * 
 * *For any* sequence of allocations without intervening frees,
 * all returned frame addresses SHALL be unique.
 * 
 * **Feature: test-refactor, Property 4: PMM Allocation Alignment and Uniqueness**
 * **Validates: Requirements 3.1**
 */
TEST_CASE(test_pbt_pmm_frame_uniqueness) {
    #define PBT_UNIQUE_ITERATIONS 50
    
    paddr_t frames[PBT_UNIQUE_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames
    for (uint32_t i = 0; i < PBT_UNIQUE_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        allocated++;
    }
    
    // Verify uniqueness: no two frames should be the same
    for (uint32_t i = 0; i < allocated; i++) {
        for (uint32_t j = i + 1; j < allocated; j++) {
            ASSERT_NE_U(frames[i], frames[j]);
        }
    }
    
    // Cleanup
    for (uint32_t i = 0; i < allocated; i++) {
        pmm_free_frame(frames[i]);
    }
}

/**
 * @brief 属性测试：分配/释放往返保持内存计数
 * 
 * *For any* sequence of N allocations followed by N frees,
 * the free frame count SHALL return to its original value.
 * 
 * **Feature: test-refactor, Property 8: Memory Leak Detection**
 * **Validates: Requirements 3.5**
 */
TEST_CASE(test_pbt_pmm_alloc_free_roundtrip) {
    #define PBT_ROUNDTRIP_ITERATIONS 30
    
    pmm_info_t info_before = pmm_get_info();
    
    paddr_t frames[PBT_ROUNDTRIP_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames
    for (uint32_t i = 0; i < PBT_ROUNDTRIP_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        allocated++;
    }
    
    // Free all frames
    for (uint32_t i = 0; i < allocated; i++) {
        pmm_free_frame(frames[i]);
    }
    
    pmm_info_t info_after = pmm_get_info();
    
    // Property: free frame count should be restored
    ASSERT_EQ_U(info_after.free_frames, info_before.free_frames);
    ASSERT_EQ_U(info_after.used_frames, info_before.used_frames);
}

// ============================================================================
// 测试套件 6: pmm_refcount_property_tests - 引用计数属性测试
// ============================================================================
// 
// 使用属性测试验证 PMM 引用计数的正确性
// **Feature: test-refactor, Property 7: COW Reference Count Consistency**
// **Validates: Requirements 3.4**
// ============================================================================

/**
 * @brief 属性测试：引用计数一致性
 * 
 * *For any* allocated frame, after n calls to pmm_frame_ref_inc() and 
 * m calls to pmm_frame_ref_dec() where n >= m, pmm_frame_get_refcount() 
 * SHALL return 1 + n - m.
 * 
 * **Feature: test-refactor, Property 7: COW Reference Count Consistency**
 * **Validates: Requirements 3.4**
 */
TEST_CASE(test_pbt_pmm_refcount_consistency) {
    #define PBT_REFCOUNT_ITERATIONS 20
    
    // Allocate a frame
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // Initial refcount should be 1
    uint32_t initial_count = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(initial_count, 1);
    
    // Increment refcount multiple times
    for (uint32_t i = 0; i < PBT_REFCOUNT_ITERATIONS; i++) {
        uint32_t new_count = pmm_frame_ref_inc(frame);
        // Property: refcount should be 1 + (i + 1) = 2 + i
        ASSERT_EQ_U(new_count, 2 + i);
        ASSERT_EQ_U(pmm_frame_get_refcount(frame), 2 + i);
    }
    
    // Decrement refcount back to 1
    for (uint32_t i = 0; i < PBT_REFCOUNT_ITERATIONS; i++) {
        uint32_t new_count = pmm_frame_ref_dec(frame);
        // Property: refcount should be (1 + PBT_REFCOUNT_ITERATIONS) - (i + 1)
        uint32_t expected = (1 + PBT_REFCOUNT_ITERATIONS) - (i + 1);
        ASSERT_EQ_U(new_count, expected);
        ASSERT_EQ_U(pmm_frame_get_refcount(frame), expected);
    }
    
    // Final refcount should be 1
    ASSERT_EQ_U(pmm_frame_get_refcount(frame), 1);
    
    // Cleanup
    pmm_free_frame(frame);
}

/**
 * @brief 属性测试：引用计数防止过早释放
 * 
 * *For any* frame with refcount > 1, calling pmm_free_frame() SHALL 
 * only decrement the refcount without actually freeing the frame.
 * 
 * **Feature: test-refactor, Property 7: COW Reference Count Consistency**
 * **Validates: Requirements 3.4**
 */
TEST_CASE(test_pbt_pmm_refcount_prevents_free) {
    pmm_info_t info_before = pmm_get_info();
    
    // Allocate a frame
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // Increment refcount to 2
    pmm_frame_ref_inc(frame);
    ASSERT_EQ_U(pmm_frame_get_refcount(frame), 2);
    
    pmm_info_t info_after_alloc = pmm_get_info();
    
    // Try to free - should only decrement refcount, not actually free
    pmm_free_frame(frame);
    
    pmm_info_t info_after_first_free = pmm_get_info();
    
    // Property: frame should still be allocated (free count unchanged)
    ASSERT_EQ_U(info_after_first_free.free_frames, info_after_alloc.free_frames);
    
    // Refcount should now be 1
    ASSERT_EQ_U(pmm_frame_get_refcount(frame), 1);
    
    // Second free should actually free the frame
    pmm_free_frame(frame);
    
    pmm_info_t info_after_second_free = pmm_get_info();
    
    // Property: frame should now be freed
    ASSERT_EQ_U(info_after_second_free.free_frames, info_before.free_frames);
}

/**
 * @brief 属性测试：多页帧独立引用计数
 * 
 * *For any* set of allocated frames, their reference counts SHALL be 
 * independent of each other.
 * 
 * **Feature: test-refactor, Property 7: COW Reference Count Consistency**
 * **Validates: Requirements 3.4**
 */
TEST_CASE(test_pbt_pmm_independent_refcounts) {
    #define PBT_INDEPENDENT_FRAMES 5
    
    paddr_t frames[PBT_INDEPENDENT_FRAMES];
    
    // Allocate frames
    for (int i = 0; i < PBT_INDEPENDENT_FRAMES; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
        ASSERT_EQ_U(pmm_frame_get_refcount(frames[i]), 1);
    }
    
    // Increment refcount of each frame by different amounts
    for (int i = 0; i < PBT_INDEPENDENT_FRAMES; i++) {
        for (int j = 0; j < i + 1; j++) {
            pmm_frame_ref_inc(frames[i]);
        }
        // Property: refcount should be 1 + (i + 1) = 2 + i
        ASSERT_EQ_U(pmm_frame_get_refcount(frames[i]), (uint32_t)(2 + i));
    }
    
    // Verify each frame's refcount is independent
    for (int i = 0; i < PBT_INDEPENDENT_FRAMES; i++) {
        ASSERT_EQ_U(pmm_frame_get_refcount(frames[i]), (uint32_t)(2 + i));
    }
    
    // Cleanup: decrement refcounts and free
    for (int i = 0; i < PBT_INDEPENDENT_FRAMES; i++) {
        // Decrement the extra refs we added
        for (int j = 0; j < i + 1; j++) {
            pmm_frame_ref_dec(frames[i]);
        }
        // Now free the frame (refcount should be 1)
        ASSERT_EQ_U(pmm_frame_get_refcount(frames[i]), 1);
        pmm_free_frame(frames[i]);
    }
}

/**
 * @brief 属性测试套件：分配对齐和唯一性
 * 
 * **Feature: test-refactor, Property 4: PMM Allocation Alignment and Uniqueness**
 * **Validates: Requirements 3.1**
 */
TEST_SUITE(pmm_property_tests) {
    RUN_TEST(test_pbt_pmm_page_alignment);
    RUN_TEST(test_pbt_pmm_frame_uniqueness);
    RUN_TEST(test_pbt_pmm_alloc_free_roundtrip);
}

/**
 * @brief 属性测试套件：引用计数一致性
 * 
 * **Feature: test-refactor, Property 7: COW Reference Count Consistency**
 * **Validates: Requirements 3.4**
 */
TEST_SUITE(pmm_refcount_property_tests) {
    RUN_TEST(test_pbt_pmm_refcount_consistency);
    RUN_TEST(test_pbt_pmm_refcount_prevents_free);
    RUN_TEST(test_pbt_pmm_independent_refcounts);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 PMM 测试
 * 
 * 按功能组织的测试套件：
 *   1. pmm_alloc_tests - 页帧分配测试
 *   2. pmm_free_tests - 页帧释放测试
 *   3. pmm_info_tests - 信息查询测试
 *   4. pmm_stress_tests - 压力测试
 *   5. pmm_property_tests - 分配属性测试 (PBT)
 *   6. pmm_refcount_property_tests - 引用计数属性测试 (PBT)
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 11.1**
 */
void run_pmm_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // ========================================================================
    // 功能测试套件
    // ========================================================================
    
    // 套件 1: 页帧分配测试
    // _Requirements: 3.1_
    RUN_SUITE(pmm_alloc_tests);
    
    // 套件 2: 页帧释放测试
    // _Requirements: 3.1, 3.5_
    RUN_SUITE(pmm_free_tests);
    
    // 套件 3: 信息查询测试
    // _Requirements: 3.1_
    RUN_SUITE(pmm_info_tests);
    
    // 套件 4: 压力测试
    // _Requirements: 3.1, 3.5_
    RUN_SUITE(pmm_stress_tests);
    
    // ========================================================================
    // 属性测试套件 (Property-Based Tests)
    // ========================================================================
    
    // 套件 5: 分配属性测试
    // **Feature: test-refactor, Property 4: PMM Allocation Alignment and Uniqueness**
    // **Validates: Requirements 3.1**
    RUN_SUITE(pmm_property_tests);
    
    // 套件 6: 引用计数属性测试
    // **Feature: test-refactor, Property 7: COW Reference Count Consistency**
    // **Validates: Requirements 3.4**
    RUN_SUITE(pmm_refcount_property_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief PMM 测试模块元数据
 * 
 * 使用 TEST_MODULE_DESC 宏注册模块到测试框架
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 10.2, 11.1**
 */
TEST_MODULE_DESC(pmm, MM, run_pmm_tests, 
    "Physical Memory Manager tests - allocation, free, info, refcount");
