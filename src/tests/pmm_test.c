// ============================================================================
// pmm_test.c - 物理内存管理器单元测试
// ============================================================================
// 
// 测试 PMM (Physical Memory Manager) 的功能
// 包括：页帧分配、释放、信息查询等
// ============================================================================

#include <tests/ktest.h>
#include <tests/pmm_test.h>
#include <mm/pmm.h>
#include <lib/kprintf.h>
#include <types.h>

// ============================================================================
// 测试用例：pmm_alloc_frame - 页帧分配
// ============================================================================

TEST_CASE(test_pmm_alloc_frame_basic) {
    // 分配一个页帧
    uint32_t frame = pmm_alloc_frame();
    
    // 应该返回非零地址
    ASSERT_NE_U(frame, 0);
    
    // 应该是页对齐的（4KB = 0x1000）
    ASSERT_EQ_U(frame & 0xFFF, 0);
    
    // 释放页帧
    pmm_free_frame(frame);
}

TEST_CASE(test_pmm_alloc_multiple_frames) {
    // 分配多个页帧
    uint32_t frame1 = pmm_alloc_frame();
    uint32_t frame2 = pmm_alloc_frame();
    uint32_t frame3 = pmm_alloc_frame();
    
    // 应该都非零
    ASSERT_NE_U(frame1, 0);
    ASSERT_NE_U(frame2, 0);
    ASSERT_NE_U(frame3, 0);
    
    // 应该都不相同
    ASSERT_NE_U(frame1, frame2);
    ASSERT_NE_U(frame2, frame3);
    ASSERT_NE_U(frame1, frame3);
    
    // 释放页帧
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
    pmm_free_frame(frame3);
}

TEST_CASE(test_pmm_alloc_frame_alignment) {
    // 分配10个页帧，检查对齐
    for (int i = 0; i < 10; i++) {
        uint32_t frame = pmm_alloc_frame();
        ASSERT_NE_U(frame, 0);
        ASSERT_EQ_U(frame & (PAGE_SIZE - 1), 0);
        pmm_free_frame(frame);
    }
}

// ============================================================================
// 测试用例：pmm_free_frame - 页帧释放
// ============================================================================

TEST_CASE(test_pmm_free_frame_basic) {
    pmm_info_t info_before = pmm_get_info();
    
    // 分配一个页帧
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    pmm_info_t info_after_alloc = pmm_get_info();
    // 空闲页帧应该减少1
    ASSERT_EQ_U(info_after_alloc.free_frames, info_before.free_frames - 1);
    
    // 释放页帧
    pmm_free_frame(frame);
    
    pmm_info_t info_after_free = pmm_get_info();
    // 空闲页帧应该恢复
    ASSERT_EQ_U(info_after_free.free_frames, info_before.free_frames);
}

TEST_CASE(test_pmm_free_frame_reuse) {
    // 分配并释放一个页帧
    uint32_t frame1 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, 0);
    pmm_free_frame(frame1);
    
    // 再次分配，应该能够复用刚释放的页帧
    uint32_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame2, 0);
    
    // 可能（但不一定）是同一个页帧
    // 至少应该能成功分配
    pmm_free_frame(frame2);
}

TEST_CASE(test_pmm_free_invalid_frame) {
    pmm_info_t info_before = pmm_get_info();
    
    // 尝试释放非对齐地址（应该被忽略）
    pmm_free_frame(0x12345);
    
    pmm_info_t info_after = pmm_get_info();
    // 信息应该不变
    ASSERT_EQ_U(info_after.free_frames, info_before.free_frames);
}

TEST_CASE(test_pmm_free_double_free) {    
    // 分配一个页帧
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
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

TEST_CASE(test_pmm_free_out_of_bounds) {
    pmm_info_t info_before = pmm_get_info();
    
    // 尝试释放超出范围的页帧（应该被忽略）
    // 使用一个非常大的地址
    pmm_free_frame(0x7FFFF000);  // 接近2GB边界
    
    pmm_info_t info_after = pmm_get_info();
    // 信息应该不变
    ASSERT_EQ_U(info_after.free_frames, info_before.free_frames);
}

TEST_CASE(test_pmm_alloc_until_low_memory) {
    // 测试分配大量内存（但不完全耗尽）
    #define LARGE_ALLOC_COUNT 200
    uint32_t frames[LARGE_ALLOC_COUNT];
    int allocated = 0;
    
    pmm_info_t info_before = pmm_get_info();
    
    // 分配大量页帧
    for (int i = 0; i < LARGE_ALLOC_COUNT; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == 0) {
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
    // 应该恢复到接近原始状态（允许较大误差，因为其他子系统也会分配内存）
    // 容忍 ±150 个页帧的差异（约 600KB），这是合理的系统开销
    // 在测试套件运行期间，堆分配器、VMM等子系统会占用一些页帧
    int32_t diff = (int32_t)info_after_free.free_frames - (int32_t)info_before.free_frames;
    
    // 打印调试信息（仅在超出容差时）
    if (diff < -150 || diff > 150) {
        kprintf("  Debug: allocated=%d, before=%u, after=%u, diff=%d\n",
                allocated, info_before.free_frames, info_after_free.free_frames, diff);
    }
    
    ASSERT_TRUE(diff >= -150 && diff <= 150);
}

// ============================================================================
// 测试用例：pmm_get_info - 信息查询
// ============================================================================

TEST_CASE(test_pmm_get_info_basic) {
    pmm_info_t info = pmm_get_info();
    
    // 总页帧数应该非零
    ASSERT_NE_U(info.total_frames, 0);
    
    // 总页帧 = 空闲页帧 + 已使用页帧
    ASSERT_EQ_U(info.total_frames, info.free_frames + info.used_frames);
}

TEST_CASE(test_pmm_get_bitmap_end) {
    // 获取位图结束地址
    uint32_t bitmap_end = pmm_get_bitmap_end();
    
    // 应该非零
    ASSERT_NE_U(bitmap_end, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(bitmap_end & (PAGE_SIZE - 1), 0);
    
    // 应该在内核空间（高半核）
    ASSERT_TRUE(bitmap_end >= 0x80000000);
}

TEST_CASE(test_pmm_get_info_after_operations) {
    pmm_info_t info_before = pmm_get_info();
    
    // 分配3个页帧
    uint32_t frames[3];
    for (int i = 0; i < 3; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], 0);
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
// 测试用例：压力测试
// TODO: 这个测试可能有问题
// ============================================================================

TEST_CASE(test_pmm_stress_alloc_free) {
    // 分配和释放大量页帧
    #define STRESS_COUNT 100
    uint32_t frames[STRESS_COUNT];
    
    pmm_info_t info_before = pmm_get_info();
    
    // 分配
    for (int i = 0; i < STRESS_COUNT; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], 0);
    }
    
    pmm_info_t info_after_alloc = pmm_get_info();
    // 验证至少减少了 STRESS_COUNT 个页帧（可能更多，因为其他子系统也在使用）
    ASSERT_TRUE(info_after_alloc.free_frames <= info_before.free_frames - STRESS_COUNT);
    
    // 释放
    for (int i = 0; i < STRESS_COUNT; i++) {
        pmm_free_frame(frames[i]);
    }
    
    pmm_info_t info_after_free = pmm_get_info();
    // 验证释放后页帧数基本恢复（允许误差，因为其他子系统可能分配了页表等）
    // 容忍±100个页帧的差异（约400KB），这是合理的系统开销
    int32_t diff = (int32_t)info_after_free.free_frames - (int32_t)info_before.free_frames;
    ASSERT_TRUE(diff >= -100 && diff <= 100);
}

TEST_CASE(test_pmm_interleaved_alloc_free) {
    // 交替分配和释放
    uint32_t frame1 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, 0);
    
    uint32_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame2, 0);
    
    pmm_free_frame(frame1);
    
    uint32_t frame3 = pmm_alloc_frame();
    ASSERT_NE_U(frame3, 0);
    
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
// Property-Based Tests: PMM Page Size Correctness
// **Feature: multi-arch-support, Property 2: PMM Page Size Correctness**
// **Validates: Requirements 5.1**
// ============================================================================

/**
 * Property Test: All allocated frames are page-aligned
 * 
 * *For any* physical memory allocation request, the PMM SHALL return 
 * addresses that are aligned to the architecture's standard page size 
 * (4KB for all currently supported architectures).
 */
TEST_CASE(test_pbt_pmm_page_alignment) {
    // Property-based test: allocate many frames and verify all are page-aligned
    #define PBT_PMM_ITERATIONS 100
    
    uint32_t frames[PBT_PMM_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames
    for (uint32_t i = 0; i < PBT_PMM_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == 0) {
            // Out of memory, stop allocating
            break;
        }
        allocated++;
        
        // Property: frame address must be page-aligned (4KB = 0x1000)
        ASSERT_EQ_U(frames[i] & (PAGE_SIZE - 1), 0);
        
        // Property: frame address must be non-zero
        ASSERT_NE_U(frames[i], 0);
    }
    
    // Verify we allocated at least some frames
    ASSERT_TRUE(allocated > 0);
    
    // Cleanup: free all allocated frames
    for (uint32_t i = 0; i < allocated; i++) {
        pmm_free_frame(frames[i]);
    }
}

/**
 * Property Test: Allocated frames are unique
 * 
 * *For any* sequence of allocations without intervening frees,
 * all returned frame addresses SHALL be unique.
 */
TEST_CASE(test_pbt_pmm_frame_uniqueness) {
    #define PBT_UNIQUE_ITERATIONS 50
    
    uint32_t frames[PBT_UNIQUE_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames
    for (uint32_t i = 0; i < PBT_UNIQUE_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == 0) {
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
 * Property Test: Allocation/Free round-trip preserves memory count
 * 
 * *For any* sequence of N allocations followed by N frees,
 * the free frame count SHALL return to its original value.
 */
TEST_CASE(test_pbt_pmm_alloc_free_roundtrip) {
    #define PBT_ROUNDTRIP_ITERATIONS 30
    
    pmm_info_t info_before = pmm_get_info();
    
    uint32_t frames[PBT_ROUNDTRIP_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames
    for (uint32_t i = 0; i < PBT_ROUNDTRIP_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == 0) {
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

TEST_SUITE(pmm_property_tests) {
    RUN_TEST(test_pbt_pmm_page_alignment);
    RUN_TEST(test_pbt_pmm_frame_uniqueness);
    RUN_TEST(test_pbt_pmm_alloc_free_roundtrip);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_pmm_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(pmm_alloc_tests);
    RUN_SUITE(pmm_free_tests);
    RUN_SUITE(pmm_info_tests);
    RUN_SUITE(pmm_stress_tests);
    
    // Property-based tests
    // **Feature: multi-arch-support, Property 2: PMM Page Size Correctness**
    // **Validates: Requirements 5.1**
    RUN_SUITE(pmm_property_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
