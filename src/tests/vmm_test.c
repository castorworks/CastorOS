// ============================================================================
// vmm_test.c - 虚拟内存管理器单元测试
// ============================================================================
// 
// 测试 VMM (Virtual Memory Manager) 的功能
// 包括：页面映射、取消映射、页目录操作等
// ============================================================================

#include <tests/ktest.h>
#include <tests/vmm_test.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/string.h>
#include <types.h>

// 测试用虚拟地址（用户空间范围）
#define TEST_VIRT_ADDR1  0x40000000
#define TEST_VIRT_ADDR2  0x40001000
#define TEST_VIRT_ADDR3  0x50000000

// ============================================================================
// 测试用例：vmm_map_page - 页面映射
// ============================================================================

TEST_CASE(test_vmm_map_page_basic) {
    // 分配一个物理页帧
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 映射到虚拟地址
    bool result = vmm_map_page(TEST_VIRT_ADDR1, frame, 
                               PAGE_PRESENT | PAGE_WRITE);
    ASSERT_TRUE(result);
    
    // 写入数据验证映射成功
    uint32_t *ptr = (uint32_t*)TEST_VIRT_ADDR1;
    *ptr = 0xDEADBEEF;
    ASSERT_EQ_U(*ptr, 0xDEADBEEF);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    pmm_free_frame(frame);
}

TEST_CASE(test_vmm_map_page_multiple) {
    // 分配多个物理页帧
    uint32_t frame1 = pmm_alloc_frame();
    uint32_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, 0);
    ASSERT_NE_U(frame2, 0);
    
    // 映射到不同的虚拟地址
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, frame1, 
                             PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR2, frame2, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 写入不同的数据
    uint32_t *ptr1 = (uint32_t*)TEST_VIRT_ADDR1;
    uint32_t *ptr2 = (uint32_t*)TEST_VIRT_ADDR2;
    *ptr1 = 0x11111111;
    *ptr2 = 0x22222222;
    
    // 验证数据独立性
    ASSERT_EQ_U(*ptr1, 0x11111111);
    ASSERT_EQ_U(*ptr2, 0x22222222);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    vmm_unmap_page(TEST_VIRT_ADDR2);
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
}

TEST_CASE(test_vmm_map_page_alignment) {
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 尝试映射非对齐地址（应该失败）
    bool result = vmm_map_page(TEST_VIRT_ADDR1 + 0x123, frame, 
                               PAGE_PRESENT | PAGE_WRITE);
    ASSERT_FALSE(result);
    
    // 清理
    pmm_free_frame(frame);
}

TEST_CASE(test_vmm_map_page_flags) {
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 使用不同的标志映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, frame, 
                             PAGE_PRESENT | PAGE_WRITE | PAGE_USER));
    
    // 验证可以读写
    uint32_t *ptr = (uint32_t*)TEST_VIRT_ADDR1;
    *ptr = 0xCAFEBABE;
    ASSERT_EQ_U(*ptr, 0xCAFEBABE);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    pmm_free_frame(frame);
}

// ============================================================================
// 测试用例：vmm_unmap_page - 取消页面映射
// ============================================================================

TEST_CASE(test_vmm_unmap_page_basic) {
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, frame, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 取消映射
    vmm_unmap_page(TEST_VIRT_ADDR1);
    
    // 注意：访问已取消映射的地址会导致页错误，所以不测试访问
    
    // 清理
    pmm_free_frame(frame);
}

TEST_CASE(test_vmm_unmap_page_double) {
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, frame, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 取消映射两次（第二次应该无害）
    vmm_unmap_page(TEST_VIRT_ADDR1);
    vmm_unmap_page(TEST_VIRT_ADDR1);
    
    // 清理
    pmm_free_frame(frame);
}

// ============================================================================
// 测试用例：vmm_create_page_directory - 创建页目录
// ============================================================================

TEST_CASE(test_vmm_create_page_directory_basic) {
    // 创建新页目录
    uint32_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(new_dir & (PAGE_SIZE - 1), 0);
    
    // 清理
    vmm_free_page_directory(new_dir);
}

TEST_CASE(test_vmm_create_multiple_page_directories) {
    // 创建多个页目录
    uint32_t dir1 = vmm_create_page_directory();
    uint32_t dir2 = vmm_create_page_directory();
    uint32_t dir3 = vmm_create_page_directory();
    
    ASSERT_NE_U(dir1, 0);
    ASSERT_NE_U(dir2, 0);
    ASSERT_NE_U(dir3, 0);
    
    // 应该都不相同
    ASSERT_NE_U(dir1, dir2);
    ASSERT_NE_U(dir2, dir3);
    ASSERT_NE_U(dir1, dir3);
    
    // 清理
    vmm_free_page_directory(dir1);
    vmm_free_page_directory(dir2);
    vmm_free_page_directory(dir3);
}

// ============================================================================
// 测试用例：vmm_map_page_in_directory - 在指定页目录中映射
// ============================================================================

TEST_CASE(test_vmm_map_page_in_directory_basic) {
    // 创建新页目录
    uint32_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 分配物理页帧
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 在新页目录中映射
    bool result = vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, frame,
                                            PAGE_PRESENT | PAGE_WRITE);
    ASSERT_TRUE(result);
    
    // 清理
    vmm_free_page_directory(dir);
    pmm_free_frame(frame);
}

TEST_CASE(test_vmm_map_page_in_directory_multiple) {
    uint32_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 在同一个页目录中映射多个页面
    uint32_t frame1 = pmm_alloc_frame();
    uint32_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, 0);
    ASSERT_NE_U(frame2, 0);
    
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, frame1,
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR2, frame2,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 清理
    vmm_free_page_directory(dir);
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
}

// ============================================================================
// 测试用例：vmm_get_page_directory - 获取当前页目录
// ============================================================================

TEST_CASE(test_vmm_get_page_directory) {
    uint32_t current_dir = vmm_get_page_directory();
    
    // 应该非零
    ASSERT_NE_U(current_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(current_dir & (PAGE_SIZE - 1), 0);
}

// ============================================================================
// 测试用例：vmm_switch_page_directory - 切换页目录
// ============================================================================

TEST_CASE(test_vmm_switch_page_directory) {
    uint32_t original_dir = vmm_get_page_directory();
    
    // 创建新页目录
    uint32_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // 切换到新页目录
    vmm_switch_page_directory(new_dir);
    
    // 验证切换成功
    ASSERT_EQ_U(vmm_get_page_directory(), new_dir);
    
    // 切换回原页目录
    vmm_switch_page_directory(original_dir);
    ASSERT_EQ_U(vmm_get_page_directory(), original_dir);
    
    // 清理
    vmm_free_page_directory(new_dir);
}

// ============================================================================
// 测试用例：vmm_clone_page_directory - 克隆页目录
// ============================================================================

TEST_CASE(test_vmm_clone_page_directory_basic) {
    // 创建源页目录
    uint32_t src_dir = vmm_create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 在源页目录中映射一个页面
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    ASSERT_TRUE(vmm_map_page_in_directory(src_dir, TEST_VIRT_ADDR1, frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 克隆页目录
    uint32_t clone_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, src_dir);
    
    // 清理
    vmm_free_page_directory(src_dir);
    vmm_free_page_directory(clone_dir);
    pmm_free_frame(frame);
}

// ============================================================================
// 测试用例：综合测试
// ============================================================================

TEST_CASE(test_vmm_comprehensive) {
    // 1. 创建新页目录
    uint32_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 2. 分配物理页帧
    uint32_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, 0);
    
    // 3. 在新页目录中映射
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 4. 清理
    vmm_free_page_directory(dir);
    pmm_free_frame(frame);
}

// ============================================================================
// 测试套件定义
// ============================================================================

TEST_SUITE(vmm_map_tests) {
    RUN_TEST(test_vmm_map_page_basic);
    RUN_TEST(test_vmm_map_page_multiple);
    RUN_TEST(test_vmm_map_page_alignment);
    RUN_TEST(test_vmm_map_page_flags);
}

TEST_SUITE(vmm_unmap_tests) {
    RUN_TEST(test_vmm_unmap_page_basic);
    RUN_TEST(test_vmm_unmap_page_double);
}

TEST_SUITE(vmm_directory_tests) {
    RUN_TEST(test_vmm_create_page_directory_basic);
    RUN_TEST(test_vmm_create_multiple_page_directories);
    RUN_TEST(test_vmm_map_page_in_directory_basic);
    RUN_TEST(test_vmm_map_page_in_directory_multiple);
    RUN_TEST(test_vmm_get_page_directory);
    RUN_TEST(test_vmm_switch_page_directory);
    RUN_TEST(test_vmm_clone_page_directory_basic);
}

TEST_SUITE(vmm_comprehensive_tests) {
    RUN_TEST(test_vmm_comprehensive);
}

// ============================================================================
// 运行所有测试
// ============================================================================

void run_vmm_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // 运行所有测试套件
    RUN_SUITE(vmm_map_tests);
    RUN_SUITE(vmm_unmap_tests);
    RUN_SUITE(vmm_directory_tests);
    RUN_SUITE(vmm_comprehensive_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
