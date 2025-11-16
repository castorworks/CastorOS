// ============================================================================
// heap_test.c - 堆内存管理器单元测试
// ============================================================================
// 
// 测试 Heap (Dynamic Memory Allocator) 的功能
// 包括：malloc、free、realloc、calloc 等
// ============================================================================

#include <tests/ktest.h>
#include <tests/heap_test.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// 测试用例：kmalloc - 内存分配
// ============================================================================

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

TEST_CASE(test_kmalloc_zero) {
    // 分配0字节应该返回NULL
    void *ptr = kmalloc(0);
    ASSERT_NULL(ptr);
}

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

TEST_CASE(test_kmalloc_alignment) {
    // kmalloc 应该返回4字节对齐的地址
    for (int i = 1; i <= 100; i++) {
        void *ptr = kmalloc(i);
        ASSERT_NOT_NULL(ptr);
        ASSERT_EQ_U((uint32_t)ptr & 0x3, 0);
        kfree(ptr);
    }
}

// ============================================================================
// 测试用例：kfree - 内存释放
// ============================================================================

TEST_CASE(test_kfree_basic) {
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);
    
    // 释放内存（不应该崩溃）
    kfree(ptr);
}

TEST_CASE(test_kfree_null) {
    // 释放NULL指针（应该无害）
    kfree(NULL);
}

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
// 测试用例：krealloc - 重新分配
// ============================================================================

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

TEST_CASE(test_krealloc_null) {
    // krealloc(NULL, size) 应该等同于 kmalloc(size)
    void *ptr = krealloc(NULL, 64);
    ASSERT_NOT_NULL(ptr);
    kfree(ptr);
}

TEST_CASE(test_krealloc_zero) {
    // krealloc(ptr, 0) 应该等同于 kfree(ptr)
    void *ptr = kmalloc(64);
    ASSERT_NOT_NULL(ptr);
    
    void *new_ptr = krealloc(ptr, 0);
    ASSERT_NULL(new_ptr);
}

// ============================================================================
// 测试用例：kcalloc - 分配并清零
// ============================================================================

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

TEST_CASE(test_kcalloc_zero_elements) {
    // kcalloc(0, size) 应该返回NULL
    void *ptr = kcalloc(0, 10);
    ASSERT_NULL(ptr);
}

TEST_CASE(test_kcalloc_zero_size) {
    // kcalloc(num, 0) 应该返回NULL
    void *ptr = kcalloc(10, 0);
    ASSERT_NULL(ptr);
}

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

TEST_CASE(test_kcalloc_overflow_protection) {
    // 测试整数溢出保护
    // SIZE_MAX / 2 * 3 会导致溢出
    size_t large_num = (size_t)-1 / 2 + 1;
    void *ptr = kcalloc(large_num, 2);
    
    // 应该返回NULL（溢出检测）
    ASSERT_NULL(ptr);
}

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
// 测试用例：边界条件和错误处理
// ============================================================================

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
    
    heap_block_t *block = (heap_block_t*)((uint32_t)ptr - sizeof(heap_block_t));
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

TEST_CASE(test_heap_alignment_various_sizes) {
    // 测试各种大小的分配都是4字节对齐的
    size_t test_sizes[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 127, 128};
    
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        void *ptr = kmalloc(test_sizes[i]);
        ASSERT_NOT_NULL(ptr);
        ASSERT_EQ_U((uint32_t)ptr & 0x3, 0);
        kfree(ptr);
    }
}

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
// 测试用例：综合测试
// ============================================================================

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

TEST_SUITE(heap_alloc_tests) {
    RUN_TEST(test_kmalloc_basic);
    RUN_TEST(test_kmalloc_zero);
    RUN_TEST(test_kmalloc_large);
    RUN_TEST(test_kmalloc_multiple);
    RUN_TEST(test_kmalloc_alignment);
}

TEST_SUITE(heap_free_tests) {
    RUN_TEST(test_kfree_basic);
    RUN_TEST(test_kfree_null);
    RUN_TEST(test_kfree_reuse);
    RUN_TEST(test_kfree_multiple);
    RUN_TEST(test_kfree_reverse_order);
}

TEST_SUITE(heap_realloc_tests) {
    RUN_TEST(test_krealloc_basic);
    RUN_TEST(test_krealloc_shrink);
    RUN_TEST(test_krealloc_null);
    RUN_TEST(test_krealloc_zero);
}

TEST_SUITE(heap_calloc_tests) {
    RUN_TEST(test_kcalloc_basic);
    RUN_TEST(test_kcalloc_zero_elements);
    RUN_TEST(test_kcalloc_zero_size);
    RUN_TEST(test_kcalloc_large);
    RUN_TEST(test_kcalloc_overflow_protection);
    RUN_TEST(test_kcalloc_boundary);
}

TEST_SUITE(heap_boundary_tests) {
    RUN_TEST(test_heap_magic_corruption);
    RUN_TEST(test_heap_alignment_various_sizes);
    RUN_TEST(test_heap_realloc_edge_cases);
    RUN_TEST(test_heap_double_free_protection);
    RUN_TEST(test_heap_large_allocation);
}

TEST_SUITE(heap_coalesce_tests) {
    RUN_TEST(test_heap_coalesce_forward);
    RUN_TEST(test_heap_coalesce_backward);
    RUN_TEST(test_heap_split_blocks);
}

TEST_SUITE(heap_comprehensive_tests) {
    RUN_TEST(test_heap_fragmentation);
    RUN_TEST(test_heap_stress);
    RUN_TEST(test_heap_interleaved);
    RUN_TEST(test_heap_data_integrity);
    RUN_TEST(test_heap_mixed_operations);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_heap_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(heap_alloc_tests);
    RUN_SUITE(heap_free_tests);
    RUN_SUITE(heap_realloc_tests);
    RUN_SUITE(heap_calloc_tests);
    RUN_SUITE(heap_boundary_tests);
    RUN_SUITE(heap_coalesce_tests);
    RUN_SUITE(heap_comprehensive_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
