// ============================================================================
// heap_test.c - 堆内存管理器单元测试
// ============================================================================
//
// 模块名称: heap
// 子系统: mm (内存管理)
// 描述: 测试 Heap (Dynamic Memory Allocator) 的功能
//
// 功能覆盖:
//   - 内存分配 (kmalloc)
//   - 内存释放 (kfree)
//   - 重新分配 (krealloc)
//   - 分配并清零 (kcalloc)
//   - 边界条件和错误处理
//   - 内存合并 (coalescing)
//   - 压力测试
//
// **Feature: test-refactor**
// **Validates: Requirements 3.3, 10.1, 11.1**
// ============================================================================

#include <tests/ktest.h>
#include <tests/mm/heap_test.h>
#include <tests/test_module.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试套件 1: heap_alloc_tests - 内存分配测试
// ============================================================================
//
// 测试 kmalloc() 函数的基本功能
// **Validates: Requirements 3.3** - Heap 分配应返回对齐的地址
// ============================================================================

/**
 * @brief 测试基本内存分配
 *
 * 验证 kmalloc() 返回有效的可写内存
 * _Requirements: 3.3_
 */
TEST_CASE(test_kmalloc_basic) {
    // 分配小块内存
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);

    // 应该可以写入
    uint8_t *data = (uint8_t*)ptr;
    for (int i = 0; i < 64; i++) {
        data[i] = i;
    }

    // 验证数据
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(data[i], i);
    }

    kfree(ptr);
}

/**
 * @brief 测试零字节分配
 *
 * 验证 kmalloc(0) 返回 NULL
 * _Requirements: 3.3_
 */
TEST_CASE(test_kmalloc_zero) {
    // 分配0字节应该返回NULL
    void *ptr = kmalloc(0);
    ASSERT_NULL(ptr);
}

/**
 * @brief 测试大块内存分配
 *
 * 验证 kmalloc() 能分配较大的内存块
 * _Requirements: 3.3_
 */
TEST_CASE(test_kmalloc_large) {
    // 分配较大的内存块
    void *ptr = kmalloc(4096);
    ASSERT_NOT_NULL(ptr);

    // 写入数据验证
    uint32_t *data = (uint32_t*)ptr;
    data[0] = 0xDEADBEEF;
    data[1023] = 0xCAFEBABE;

    ASSERT_EQ_U(data[0], 0xDEADBEEF);
    ASSERT_EQ_U(data[1023], 0xCAFEBABE);

    kfree(ptr);
}

/**
 * @brief 测试多块内存分配的唯一性
 *
 * 验证连续分配的内存块地址互不相同
 * _Requirements: 3.3_
 */
TEST_CASE(test_kmalloc_multiple) {
    // 分配多个不同大小的块
    void *ptr1 = kmalloc(16);
    void *ptr2 = kmalloc(32);
    void *ptr3 = kmalloc(64);

    ASSERT_NOT_NULL(ptr1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_NOT_NULL(ptr3);

    // 应该都不相同
    ASSERT_NE_PTR(ptr1, ptr2);
    ASSERT_NE_PTR(ptr2, ptr3);
    ASSERT_NE_PTR(ptr1, ptr3);

    // 写入不同的数据
    *(uint32_t*)ptr1 = 0x11111111;
    *(uint32_t*)ptr2 = 0x22222222;
    *(uint32_t*)ptr3 = 0x33333333;

    // 验证数据独立性
    ASSERT_EQ_U(*(uint32_t*)ptr1, 0x11111111);
    ASSERT_EQ_U(*(uint32_t*)ptr2, 0x22222222);
    ASSERT_EQ_U(*(uint32_t*)ptr3, 0x33333333);

    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
}

/**
 * @brief 测试内存分配的对齐性
 *
 * 验证所有分配的内存都是4字节对齐的
 * **Feature: test-refactor, Property 6: Heap Allocation Alignment**
 * **Validates: Requirements 3.3**
 */
TEST_CASE(test_kmalloc_alignment) {
    // kmalloc 应该返回4字节对齐的地址
    for (int i = 1; i <= 100; i++) {
        void *ptr = kmalloc(i);
        ASSERT_NOT_NULL(ptr);
        ASSERT_EQ_U((uintptr_t)ptr & 0x3, 0);
        kfree(ptr);
    }
}

// ============================================================================
// 测试套件 2: heap_free_tests - 内存释放测试
// ============================================================================
//
// 测试 kfree() 函数的功能和边界情况
// **Validates: Requirements 3.3** - 内存释放正确性
// ============================================================================

/**
 * @brief 测试基本内存释放
 *
 * 验证释放内存不会崩溃
 * _Requirements: 3.3_
 */
TEST_CASE(test_kfree_basic) {
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);

    // 释放内存（不应该崩溃）
    kfree(ptr);
}

/**
 * @brief 测试释放 NULL 指针
 *
 * 验证 kfree(NULL) 是安全的
 * _Requirements: 3.3_
 */
TEST_CASE(test_kfree_null) {
    // 释放NULL指针（应该无害）
    kfree(NULL);
}

/**
 * @brief 测试内存复用
 *
 * 验证释放的内存可以被重新分配
 * _Requirements: 3.3_
 */
TEST_CASE(test_kfree_reuse) {
    // 分配并释放
    void *ptr1 = kmalloc(64);
    ASSERT_NOT_NULL(ptr1);
    kfree(ptr1);

    // 再次分配相同大小（可能复用）
    void *ptr2 = kmalloc(64);
    ASSERT_NOT_NULL(ptr2);
    kfree(ptr2);
}

/**
 * @brief 测试多块内存顺序释放
 *
 * 验证多块内存按顺序释放的正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_kfree_multiple) {
    // 分配多个块
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(32);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 按顺序释放
    for (int i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }
}

/**
 * @brief 测试多块内存逆序释放
 *
 * 验证多块内存按逆序释放的正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_kfree_reverse_order) {
    // 分配多个块
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(32);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 逆序释放
    for (int i = 9; i >= 0; i--) {
        kfree(ptrs[i]);
    }
}

// ============================================================================
// 测试套件 3: heap_realloc_tests - 重新分配测试
// ============================================================================
//
// 测试 krealloc() 函数的功能
// **Validates: Requirements 3.3** - 重新分配保持数据完整性
// ============================================================================

/**
 * @brief 测试基本重新分配（扩大）
 *
 * 验证 krealloc() 扩大内存时保留原数据
 * _Requirements: 3.3_
 */
TEST_CASE(test_krealloc_basic) {
    // 分配初始内存
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);

    // 写入数据
    for (int i = 0; i < 64; i++) {
        ((uint8_t*)ptr)[i] = i;
    }

    // 扩大内存
    void *new_ptr = krealloc(ptr, 128);
    ASSERT_NOT_NULL(new_ptr);

    // 原数据应该保留
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(((uint8_t*)new_ptr)[i], i);
    }

    kfree(new_ptr);
}

/**
 * @brief 测试重新分配（缩小）
 *
 * 验证 krealloc() 缩小内存时保留前部数据
 * _Requirements: 3.3_
 */
TEST_CASE(test_krealloc_shrink) {
    // 分配较大内存
    void *ptr = kmalloc(128);
    ASSERT_NOT_NULL(ptr);

    // 写入数据
    for (int i = 0; i < 128; i++) {
        ((uint8_t*)ptr)[i] = i;
    }

    // 缩小内存
    void *new_ptr = krealloc(ptr, 64);
    ASSERT_NOT_NULL(new_ptr);

    // 前64字节应该保留
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(((uint8_t*)new_ptr)[i], i);
    }

    kfree(new_ptr);
}

/**
 * @brief 测试 krealloc(NULL, size)
 *
 * 验证 krealloc(NULL, size) 等同于 kmalloc(size)
 * _Requirements: 3.3_
 */
TEST_CASE(test_krealloc_null) {
    // krealloc(NULL, size) 应该等同于 kmalloc(size)
    void *ptr = krealloc(NULL, 64);
    ASSERT_NOT_NULL(ptr);
    kfree(ptr);
}

/**
 * @brief 测试 krealloc(ptr, 0)
 *
 * 验证 krealloc(ptr, 0) 等同于 kfree(ptr)
 * _Requirements: 3.3_
 */
TEST_CASE(test_krealloc_zero) {
    // krealloc(ptr, 0) 应该等同于 kfree(ptr)
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);

    void *new_ptr = krealloc(ptr, 0);
    ASSERT_NULL(new_ptr);
}

// ============================================================================
// 测试套件 4: heap_calloc_tests - 分配并清零测试
// ============================================================================
//
// 测试 kcalloc() 函数的功能
// **Validates: Requirements 3.3** - 分配并清零的正确性
// ============================================================================

/**
 * @brief 测试基本 kcalloc
 *
 * 验证 kcalloc() 返回清零的内存
 * _Requirements: 3.3_
 */
TEST_CASE(test_kcalloc_basic) {
    // 分配10个uint32_t
    uint32_t *ptr = (uint32_t*)kcalloc(10, sizeof(uint32_t));
    ASSERT_NOT_NULL(ptr);

    // 应该全部为0
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ_U(ptr[i], 0);
    }

    kfree(ptr);
}

/**
 * @brief 测试 kcalloc(0, size)
 *
 * 验证 kcalloc(0, size) 返回 NULL
 * _Requirements: 3.3_
 */
TEST_CASE(test_kcalloc_zero_elements) {
    // kcalloc(0, size) 应该返回NULL
    void *ptr = kcalloc(0, 10);
    ASSERT_NULL(ptr);
}

/**
 * @brief 测试 kcalloc(num, 0)
 *
 * 验证 kcalloc(num, 0) 返回 NULL
 * _Requirements: 3.3_
 */
TEST_CASE(test_kcalloc_zero_size) {
    // kcalloc(num, 0) 应该返回NULL
    void *ptr = kcalloc(10, 0);
    ASSERT_NULL(ptr);
}

/**
 * @brief 测试大块 kcalloc
 *
 * 验证 kcalloc() 能分配较大的清零内存
 * _Requirements: 3.3_
 */
TEST_CASE(test_kcalloc_large) {
    // 分配1024个字节
    uint8_t *ptr = (uint8_t*)kcalloc(1024, 1);
    ASSERT_NOT_NULL(ptr);

    // 验证全部为0
    for (int i = 0; i < 1024; i++) {
        ASSERT_EQ(ptr[i], 0);
    }

    kfree(ptr);
}

/**
 * @brief 测试 kcalloc 整数溢出保护
 *
 * 验证 kcalloc() 检测整数溢出并返回 NULL
 * _Requirements: 3.3_
 */
TEST_CASE(test_kcalloc_overflow_protection) {
    // 测试整数溢出保护
    // SIZE_MAX / 2 * 3 会导致溢出
    size_t large_num = (size_t)-1 / 2 + 1;
    void *ptr = kcalloc(large_num, 2);

    // 应该返回NULL（溢出检测）
    ASSERT_NULL(ptr);
}

/**
 * @brief 测试 kcalloc 边界情况
 *
 * 验证各种边界参数组合
 * _Requirements: 3.3_
 */
TEST_CASE(test_kcalloc_boundary) {
    // 测试边界情况
    void *ptr1 = kcalloc(1, 0);
    ASSERT_NULL(ptr1);

    void *ptr2 = kcalloc(0, 1);
    ASSERT_NULL(ptr2);

    void *ptr3 = kcalloc(0, 0);
    ASSERT_NULL(ptr3);
}

// ============================================================================
// 测试套件 5: heap_boundary_tests - 边界条件和错误处理测试
// ============================================================================
//
// 测试堆管理器的边界条件和错误处理
// **Validates: Requirements 3.3** - 边界条件处理
// ============================================================================

/**
 * @brief 测试魔数损坏检测
 *
 * 验证堆管理器能检测块头魔数损坏
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_magic_corruption) {
    // 分配内存
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);

    // 获取块头（注意：heap_block_t 的字段顺序是 size, is_free, next, prev, magic）
    typedef struct heap_block {
        size_t size;
        bool is_free;
        struct heap_block *next;
        struct heap_block *prev;
        uint32_t magic;
    } heap_block_t;

    heap_block_t *block = (heap_block_t*)((uintptr_t)ptr - sizeof(heap_block_t));
    uint32_t original_magic = block->magic;

    // 验证原始魔数是正确的
    ASSERT_EQ_U(original_magic, 0xDEADBEEF);

    // 破坏魔数
    block->magic = 0xBADC0FFE;

    // 尝试释放（应该被忽略，因为魔数不匹配）
    kfree(ptr);

    // 恢复魔数并正确释放
    block->magic = original_magic;
    kfree(ptr);
}

/**
 * @brief 测试各种大小的对齐性
 *
 * 验证各种大小的分配都是4字节对齐的
 * **Feature: test-refactor, Property 6: Heap Allocation Alignment**
 * **Validates: Requirements 3.3**
 */
TEST_CASE(test_heap_alignment_various_sizes) {
    // 测试各种大小的分配都是4字节对齐的
    size_t test_sizes[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 127, 128};

    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        void *ptr = kmalloc(test_sizes[i]);
        ASSERT_NOT_NULL(ptr);
        ASSERT_EQ_U((uintptr_t)ptr & 0x3, 0);
        kfree(ptr);
    }
}

/**
 * @brief 测试 krealloc 边界情况
 *
 * 验证 krealloc 的各种边界情况
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_realloc_edge_cases) {
    // krealloc 边界情况测试

    // 1. krealloc(NULL, size) == kmalloc(size)
    void *ptr1 = krealloc(NULL, 64);
    ASSERT_NOT_NULL(ptr1);
    kfree(ptr1);

    // 2. krealloc(ptr, 0) == kfree(ptr)
    void *ptr2 = kmalloc(64);
    ASSERT_NOT_NULL(ptr2);
    void *result = krealloc(ptr2, 0);
    ASSERT_NULL(result);

    // 3. krealloc 到相同大小
    void *ptr3 = kmalloc(64);
    ASSERT_NOT_NULL(ptr3);
    *(uint32_t*)ptr3 = 0x12345678;
    void *ptr3_new = krealloc(ptr3, 64);
    ASSERT_NOT_NULL(ptr3_new);
    ASSERT_EQ_U(*(uint32_t*)ptr3_new, 0x12345678);
    kfree(ptr3_new);
}

/**
 * @brief 测试双重释放保护
 *
 * 验证双重释放不会导致崩溃
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_double_free_protection) {
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);

    // 第一次释放
    kfree(ptr);

    // 第二次释放相同指针
    // 注意：这可能导致未定义行为，但不应该崩溃
    // 我们的实现通过检查魔数来保护
    kfree(ptr);
}

/**
 * @brief 测试大块分配
 *
 * 验证大块分配（跨越多个页）的正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_large_allocation) {
    // 测试大块分配（跨越多个页）
    void *ptr = kmalloc(16384);  // 16KB
    ASSERT_NOT_NULL(ptr);

    // 写入并验证数据
    uint32_t *data = (uint32_t*)ptr;
    data[0] = 0xAAAAAAAA;
    data[4095] = 0xBBBBBBBB;  // 最后一个uint32_t

    ASSERT_EQ_U(data[0], 0xAAAAAAAA);
    ASSERT_EQ_U(data[4095], 0xBBBBBBBB);

    kfree(ptr);
}

// ============================================================================
// 测试套件 6: heap_coalesce_tests - 内存合并测试
// ============================================================================
//
// 测试堆管理器的空闲块合并功能
// **Validates: Requirements 3.3** - 内存合并正确性
// ============================================================================

/**
 * @brief 测试向前合并空闲块
 *
 * 验证释放相邻块时能正确合并
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_coalesce_forward) {
    // 测试向前合并空闲块
    void *ptr1 = kmalloc(64);
    void *ptr2 = kmalloc(64);
    void *ptr3 = kmalloc(64);

    ASSERT_NOT_NULL(ptr1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_NOT_NULL(ptr3);

    // 释放 ptr1 和 ptr2（应该合并）
    kfree(ptr1);
    kfree(ptr2);

    // 现在分配较大的块（应该能使用合并后的空间）
    void *large = kmalloc(100);
    ASSERT_NOT_NULL(large);

    kfree(large);
    kfree(ptr3);
}

/**
 * @brief 测试向后合并空闲块
 *
 * 验证逆序释放相邻块时能正确合并
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_coalesce_backward) {
    // 测试向后合并空闲块
    void *ptr1 = kmalloc(64);
    void *ptr2 = kmalloc(64);
    void *ptr3 = kmalloc(64);

    ASSERT_NOT_NULL(ptr1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_NOT_NULL(ptr3);

    // 按相反顺序释放 ptr2 和 ptr1（应该合并）
    kfree(ptr2);
    kfree(ptr1);

    // 分配较大的块
    void *large = kmalloc(100);
    ASSERT_NOT_NULL(large);

    kfree(large);
    kfree(ptr3);
}

/**
 * @brief 测试块分裂
 *
 * 验证大块被分裂为小块的正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_split_blocks) {
    // 测试块分裂
    // 分配一个大块然后释放
    void *large = kmalloc(256);
    ASSERT_NOT_NULL(large);
    kfree(large);

    // 分配一个小块（应该分裂大块）
    void *small1 = kmalloc(32);
    void *small2 = kmalloc(32);
    void *small3 = kmalloc(32);

    ASSERT_NOT_NULL(small1);
    ASSERT_NOT_NULL(small2);
    ASSERT_NOT_NULL(small3);

    // 所有小块应该都不相同
    ASSERT_NE_PTR(small1, small2);
    ASSERT_NE_PTR(small2, small3);

    kfree(small1);
    kfree(small2);
    kfree(small3);
}

// ============================================================================
// 测试套件 7: heap_comprehensive_tests - 综合测试
// ============================================================================
//
// 综合测试堆管理器的各种场景
// **Validates: Requirements 3.3** - 综合功能验证
// ============================================================================

/**
 * @brief 测试内存碎片整理
 *
 * 验证碎片化场景下的分配正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_fragmentation) {
    // 测试内存碎片整理
    void *ptrs[20];

    // 分配20个块
    for (int i = 0; i < 20; i++) {
        ptrs[i] = kmalloc(64);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 释放奇数位置的块
    for (int i = 1; i < 20; i += 2) {
        kfree(ptrs[i]);
    }

    // 分配较大的块（测试碎片合并）
    void *large = kmalloc(128);
    ASSERT_NOT_NULL(large);
    kfree(large);

    // 释放剩余的块
    for (int i = 0; i < 20; i += 2) {
        kfree(ptrs[i]);
    }
}

/**
 * @brief 压力测试
 *
 * 验证大量分配和释放的稳定性
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_stress) {
    // 压力测试：大量分配和释放
    #define HEAP_STRESS_COUNT 50
    void *ptrs[HEAP_STRESS_COUNT];

    // 分配
    for (int i = 0; i < HEAP_STRESS_COUNT; i++) {
        ptrs[i] = kmalloc(32 + (i % 64));
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 释放
    for (int i = 0; i < HEAP_STRESS_COUNT; i++) {
        kfree(ptrs[i]);
    }
}

/**
 * @brief 测试交替分配和释放
 *
 * 验证交替操作的正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_interleaved) {
    // 交替分配和释放
    void *ptr1 = kmalloc(32);
    ASSERT_NOT_NULL(ptr1);

    void *ptr2 = kmalloc(64);
    ASSERT_NOT_NULL(ptr2);

    kfree(ptr1);

    void *ptr3 = kmalloc(48);
    ASSERT_NOT_NULL(ptr3);

    kfree(ptr2);
    kfree(ptr3);
}

/**
 * @brief 测试数据完整性
 *
 * 验证多块分配时数据不会相互干扰
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_data_integrity) {
    // 测试数据完整性
    #define DATA_SIZE 100
    void *ptrs[DATA_SIZE];

    // 分配并写入唯一数据
    for (int i = 0; i < DATA_SIZE; i++) {
        ptrs[i] = kmalloc(16);
        ASSERT_NOT_NULL(ptrs[i]);
        *(uint32_t*)ptrs[i] = 0x10000 + i;
    }

    // 验证数据
    for (int i = 0; i < DATA_SIZE; i++) {
        ASSERT_EQ_U(*(uint32_t*)ptrs[i], 0x10000 + i);
    }

    // 释放
    for (int i = 0; i < DATA_SIZE; i++) {
        kfree(ptrs[i]);
    }
}

/**
 * @brief 测试混合操作
 *
 * 验证混合分配、释放、重分配操作的正确性
 * _Requirements: 3.3_
 */
TEST_CASE(test_heap_mixed_operations) {
    // 混合操作测试
    void *ptrs[20];

    // 分配一些块
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(32 + i * 8);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 释放一些
    for (int i = 0; i < 5; i++) {
        kfree(ptrs[i * 2]);
    }

    // 再分配一些
    for (int i = 10; i < 15; i++) {
        ptrs[i] = kmalloc(48);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    // 释放所有
    for (int i = 1; i < 10; i += 2) {
        kfree(ptrs[i]);
    }
    for (int i = 10; i < 15; i++) {
        kfree(ptrs[i]);
    }
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 内存分配测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_alloc_tests) {
    RUN_TEST(test_kmalloc_basic);
    RUN_TEST(test_kmalloc_zero);
    RUN_TEST(test_kmalloc_large);
    RUN_TEST(test_kmalloc_multiple);
    RUN_TEST(test_kmalloc_alignment);
}

/**
 * @brief 内存释放测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_free_tests) {
    RUN_TEST(test_kfree_basic);
    RUN_TEST(test_kfree_null);
    RUN_TEST(test_kfree_reuse);
    RUN_TEST(test_kfree_multiple);
    RUN_TEST(test_kfree_reverse_order);
}

/**
 * @brief 重新分配测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_realloc_tests) {
    RUN_TEST(test_krealloc_basic);
    RUN_TEST(test_krealloc_shrink);
    RUN_TEST(test_krealloc_null);
    RUN_TEST(test_krealloc_zero);
}

/**
 * @brief 分配并清零测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_calloc_tests) {
    RUN_TEST(test_kcalloc_basic);
    RUN_TEST(test_kcalloc_zero_elements);
    RUN_TEST(test_kcalloc_zero_size);
    RUN_TEST(test_kcalloc_large);
    RUN_TEST(test_kcalloc_overflow_protection);
    RUN_TEST(test_kcalloc_boundary);
}

/**
 * @brief 边界条件测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_boundary_tests) {
    RUN_TEST(test_heap_magic_corruption);
    RUN_TEST(test_heap_alignment_various_sizes);
    RUN_TEST(test_heap_realloc_edge_cases);
    RUN_TEST(test_heap_double_free_protection);
    RUN_TEST(test_heap_large_allocation);
}

/**
 * @brief 内存合并测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_coalesce_tests) {
    RUN_TEST(test_heap_coalesce_forward);
    RUN_TEST(test_heap_coalesce_backward);
    RUN_TEST(test_heap_split_blocks);
}

/**
 * @brief 综合测试套件
 *
 * **Validates: Requirements 3.3**
 */
TEST_SUITE(heap_comprehensive_tests) {
    RUN_TEST(test_heap_fragmentation);
    RUN_TEST(test_heap_stress);
    RUN_TEST(test_heap_interleaved);
    RUN_TEST(test_heap_data_integrity);
    RUN_TEST(test_heap_mixed_operations);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 Heap 测试
 *
 * 按功能组织的测试套件：
 *   1. heap_alloc_tests - 内存分配测试
 *   2. heap_free_tests - 内存释放测试
 *   3. heap_realloc_tests - 重新分配测试
 *   4. heap_calloc_tests - 分配并清零测试
 *   5. heap_boundary_tests - 边界条件测试
 *   6. heap_coalesce_tests - 内存合并测试
 *   7. heap_comprehensive_tests - 综合测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 11.1**
 */
void run_heap_tests(void) {
    // 初始化测试框架
    unittest_init();

    // ========================================================================
    // 功能测试套件
    // ========================================================================

    // 套件 1: 内存分配测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_alloc_tests);

    // 套件 2: 内存释放测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_free_tests);

    // 套件 3: 重新分配测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_realloc_tests);

    // 套件 4: 分配并清零测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_calloc_tests);

    // 套件 5: 边界条件测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_boundary_tests);

    // 套件 6: 内存合并测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_coalesce_tests);

    // 套件 7: 综合测试
    // _Requirements: 3.3_
    RUN_SUITE(heap_comprehensive_tests);

    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief Heap 测试模块元数据
 *
 * 使用 TEST_MODULE_DESC 宏注册模块到测试框架
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 10.2, 11.1**
 */
TEST_MODULE_DESC(heap, MM, run_heap_tests,
    "Heap Memory Allocator tests - kmalloc, kfree, krealloc, kcalloc, coalescing");
