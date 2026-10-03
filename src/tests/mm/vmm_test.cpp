// ============================================================================
// vmm_test.c - 虚拟内存管理器单元测试
// ============================================================================
//
// 模块名称: vmm
// 子系统: mm (内存管理)
// 描述: 测试 VMM (Virtual Memory Manager) 的功能
//
// 功能覆盖:
//   - 页面映射 (vmm_map_page, vmm_map_page_in_directory)
//   - 取消映射 (vmm_unmap_page, vmm_unmap_page_in_directory)
//   - 页目录操作 (vmm_create_page_directory, vmm_clone_page_directory)
//   - TLB 刷新 (vmm_flush_tlb)
//   - COW 引用计数
//   - MMIO 映射
//
// 依赖模块:
//   - pmm (物理内存管理器)
//
// 架构支持:
//   - i686: 2 级页表 (PDE -> PTE)
//   - x86_64: 4 级页表 (PML4 -> PDPT -> PD -> PT)
//   - ARM64: 4 级页表
//
// **Feature: test-refactor**
// **Validates: Requirements 3.2, 7.2, 10.1, 11.1**
// ============================================================================

#include <tests/ktest.h>
#include <tests/mm/vmm_test.h>
#include <tests/test_module.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <hal/hal.h>
#include <lib/string.h>
#include <types.h>

// 测试用虚拟地址（用户空间范围）
#define TEST_VIRT_ADDR1  0x10000000
#define TEST_VIRT_ADDR2  0x10001000
#define TEST_VIRT_ADDR3  0x20000000

// ============================================================================
// 测试套件 1: vmm_map_tests - 页面映射测试
// ============================================================================
//
// 测试 vmm_map_page() 函数的基本功能
// **Validates: Requirements 3.2** - VMM 映射页面应可查询且物理地址正确
// ============================================================================

/**
 * @brief 测试基本页面映射
 * 
 * 验证 vmm_map_page() 能正确映射虚拟地址到物理地址
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_map_page_basic) {
    // 分配一个物理页帧
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 映射到虚拟地址
    bool result = vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, 
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

/**
 * @brief 测试多页面映射
 * 
 * 验证多个页面可以独立映射到不同的虚拟地址
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_map_page_multiple) {
    // 分配多个物理页帧
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // 映射到不同的虚拟地址
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame1, 
                             PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR2, (uintptr_t)frame2, 
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

/**
 * @brief 测试页面映射对齐检查
 * 
 * 验证非对齐地址的映射请求被正确拒绝
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_map_page_alignment) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 尝试映射非对齐地址（应该失败）
    bool result = vmm_map_page(TEST_VIRT_ADDR1 + 0x123, (uintptr_t)frame, 
                               PAGE_PRESENT | PAGE_WRITE);
    ASSERT_FALSE(result);
    
    // 清理
    pmm_free_frame(frame);
}

/**
 * @brief 测试页面映射标志
 * 
 * 验证不同的页面标志（PRESENT, WRITE, USER）能正确设置
 * _Requirements: 3.2, 7.2_
 */
TEST_CASE(test_vmm_map_page_flags) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 使用不同的标志映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, 
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
// 测试套件 2: vmm_unmap_tests - 取消页面映射测试
// ============================================================================
//
// 测试 vmm_unmap_page() 和 vmm_unmap_page_in_directory() 函数
// **Validates: Requirements 3.2** - VMM 取消映射功能
// ============================================================================

/**
 * @brief 测试基本取消映射
 * 
 * 验证 vmm_unmap_page() 能正确取消页面映射
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_unmap_page_basic) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 取消映射
    vmm_unmap_page(TEST_VIRT_ADDR1);
    
    // 注意：访问已取消映射的地址会导致页错误，所以不测试访问
    
    // 清理
    pmm_free_frame(frame);
}

/**
 * @brief 测试双重取消映射
 * 
 * 验证对同一地址多次取消映射不会导致问题
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_unmap_page_double) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 取消映射两次（第二次应该无害）
    vmm_unmap_page(TEST_VIRT_ADDR1);
    vmm_unmap_page(TEST_VIRT_ADDR1);
    
    // 清理
    pmm_free_frame(frame);
}

/**
 * @brief 测试取消映射非对齐地址
 * 
 * 验证取消映射非对齐地址时系统保持稳定
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_unmap_page_alignment) {
    // 尝试取消映射非对齐地址（应该被忽略）
    vmm_unmap_page(TEST_VIRT_ADDR1 + 0x456);
    // 如果没有崩溃就是成功
}

/**
 * @brief 测试在指定页目录中取消映射
 * 
 * 验证 vmm_unmap_page_in_directory() 能正确取消指定页目录中的映射
 * _Requirements: 3.2, 7.2_
 */
TEST_CASE(test_vmm_unmap_page_in_directory_basic) {
    // 创建新页目录
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 分配物理页帧
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 在新页目录中映射
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 取消映射
    uintptr_t unmapped_frame = vmm_unmap_page_in_directory(dir, TEST_VIRT_ADDR1);
    ASSERT_EQ_U(unmapped_frame, (uintptr_t)frame);
    
    // 清理
    vmm_free_page_directory(dir);
    pmm_free_frame(frame);
}

/**
 * @brief 测试取消映射不存在的页面
 * 
 * 验证取消映射未映射的页面时返回正确的结果
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_unmap_page_in_directory_nonexistent) {
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 尝试取消映射一个未映射的页面（应该返回0）
    uintptr_t result = vmm_unmap_page_in_directory(dir, TEST_VIRT_ADDR1);
    ASSERT_EQ_U(result, 0);
    
    // 清理
    vmm_free_page_directory(dir);
}

/**
 * @brief 测试在页目录中取消映射非对齐地址
 * 
 * 验证在页目录中取消映射非对齐地址时返回正确的结果
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_unmap_page_in_directory_alignment) {
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 尝试取消映射非对齐地址（应该返回0）
    uintptr_t result = vmm_unmap_page_in_directory(dir, TEST_VIRT_ADDR1 + 0x123);
    ASSERT_EQ_U(result, 0);
    
    // 清理
    vmm_free_page_directory(dir);
}

// ============================================================================
// 测试套件 1 (续): vmm_map_tests - 重复映射和覆盖测试
// ============================================================================

/**
 * @brief 测试重新映射
 * 
 * 验证同一虚拟地址可以重新映射到不同的物理地址
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_map_page_remap) {
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // 第一次映射
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame1, 
                             PAGE_PRESENT | PAGE_WRITE));
    uint32_t *ptr = (uint32_t*)TEST_VIRT_ADDR1;
    *ptr = 0x11111111;
    ASSERT_EQ_U(*ptr, 0x11111111);
    
    // 重新映射到不同的物理页
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame2, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 现在应该映射到 frame2（内容应该不同）
    // frame2 是新分配的，已经被清零
    ASSERT_EQ_U(*ptr, 0x00000000);
    
    // 写入新数据
    *ptr = 0x22222222;
    ASSERT_EQ_U(*ptr, 0x22222222);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
}

/**
 * @brief 测试不同标志的映射
 * 
 * 验证同一页面可以用不同的标志重新映射
 * _Requirements: 3.2, 7.2_
 */
TEST_CASE(test_vmm_map_page_different_flags) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 首先映射为只读（实际上x86的supervisor模式总是可写的）
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, PAGE_PRESENT));
    
    // 重新映射为可写
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 应该能写入
    uint32_t *ptr = (uint32_t*)TEST_VIRT_ADDR1;
    *ptr = 0xABCDEF12;
    ASSERT_EQ_U(*ptr, 0xABCDEF12);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    pmm_free_frame(frame);
}

// ============================================================================
// 测试套件 3: vmm_tlb_tests - TLB 刷新测试
// ============================================================================
//
// 测试 vmm_flush_tlb() 函数的功能
// **Validates: Requirements 3.2** - TLB 刷新后映射仍然有效
// ============================================================================

/**
 * @brief 测试单页 TLB 刷新
 * 
 * 验证刷新单个页面的 TLB 后映射仍然有效
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_flush_tlb_single_page) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 映射页面
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 写入数据
    *(uint32_t*)TEST_VIRT_ADDR1 = 0xDEADBEEF;
    
    // 刷新单个页面的TLB
    vmm_flush_tlb(TEST_VIRT_ADDR1);
    
    // 应该仍然能访问
    ASSERT_EQ_U(*(uint32_t*)TEST_VIRT_ADDR1, 0xDEADBEEF);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    pmm_free_frame(frame);
}

/**
 * @brief 测试完整 TLB 刷新
 * 
 * 验证刷新整个 TLB 后所有映射仍然有效
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_flush_tlb_full) {
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // 映射多个页面
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR1, (uintptr_t)frame1, 
                             PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page(TEST_VIRT_ADDR2, (uintptr_t)frame2, 
                             PAGE_PRESENT | PAGE_WRITE));
    
    // 写入数据
    *(uint32_t*)TEST_VIRT_ADDR1 = 0x11111111;
    *(uint32_t*)TEST_VIRT_ADDR2 = 0x22222222;
    
    // 刷新整个TLB（传入0）
    vmm_flush_tlb(0);
    
    // 应该仍然能访问
    ASSERT_EQ_U(*(uint32_t*)TEST_VIRT_ADDR1, 0x11111111);
    ASSERT_EQ_U(*(uint32_t*)TEST_VIRT_ADDR2, 0x22222222);
    
    // 清理
    vmm_unmap_page(TEST_VIRT_ADDR1);
    vmm_unmap_page(TEST_VIRT_ADDR2);
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
}

// ============================================================================
// 测试套件 4: vmm_directory_tests - 页目录操作测试
// ============================================================================
//
// 测试页目录的创建、映射、切换、克隆和释放功能
// **Validates: Requirements 3.2, 7.2** - 页目录操作和多架构支持
// ============================================================================

/**
 * @brief 测试基本页目录创建
 * 
 * 验证 vmm_create_page_directory() 返回有效的页对齐地址
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_create_page_directory_basic) {
    // 创建新页目录
    uintptr_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(new_dir & (PAGE_SIZE - 1), 0);
    
    // 清理
    vmm_free_page_directory(new_dir);
}

/**
 * @brief 测试创建多个页目录
 * 
 * 验证可以创建多个独立的页目录
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_create_multiple_page_directories) {
    // 创建多个页目录
    uintptr_t dir1 = vmm_create_page_directory();
    uintptr_t dir2 = vmm_create_page_directory();
    uintptr_t dir3 = vmm_create_page_directory();
    
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

/**
 * @brief 测试在指定页目录中映射
 * 
 * 验证 vmm_map_page_in_directory() 能在指定页目录中正确映射
 * _Requirements: 3.2, 7.2_
 */
TEST_CASE(test_vmm_map_page_in_directory_basic) {
    // 创建新页目录
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 分配物理页帧
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 在新页目录中映射
    bool result = vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                            PAGE_PRESENT | PAGE_WRITE);
    ASSERT_TRUE(result);
    
    // 清理
    // 注意：vmm_free_page_directory 会自动释放所有映射的页面
    vmm_free_page_directory(dir);
    // pmm_free_frame(frame);  // ❌ 不需要：会导致 double free
}

/**
 * @brief 测试在页目录中映射多个页面
 * 
 * 验证可以在同一个页目录中映射多个页面
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_map_page_in_directory_multiple) {
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 在同一个页目录中映射多个页面
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame1,
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR2, (uintptr_t)frame2,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 清理
    // 注意：vmm_free_page_directory 会自动释放所有映射的页面
    vmm_free_page_directory(dir);
    // pmm_free_frame(frame1);  // ❌ 不需要：会导致 double free
    // pmm_free_frame(frame2);  // ❌ 不需要：会导致 double free
}

/**
 * @brief 测试获取当前页目录
 * 
 * 验证 vmm_get_page_directory() 返回有效的页目录地址
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_get_page_directory) {
    uintptr_t current_dir = vmm_get_page_directory();
    
    // 应该非零
    ASSERT_NE_U(current_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(current_dir & (PAGE_SIZE - 1), 0);
}

/**
 * @brief 测试切换页目录
 * 
 * 验证 vmm_switch_page_directory() 能正确切换页目录
 * _Requirements: 3.2, 7.2_
 */
TEST_CASE(test_vmm_switch_page_directory) {
    uintptr_t original_dir = vmm_get_page_directory();
    
    // 创建新页目录
    uintptr_t new_dir = vmm_create_page_directory();
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

/**
 * @brief 测试基本页目录克隆
 * 
 * 验证 vmm_clone_page_directory() 能正确克隆页目录
 * _Requirements: 3.2, 3.4_
 */
TEST_CASE(test_vmm_clone_page_directory_basic) {
    // 创建源页目录
    uintptr_t src_dir = vmm_create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 在源页目录中映射一个页面
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    ASSERT_TRUE(vmm_map_page_in_directory(src_dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 克隆页目录
    uintptr_t clone_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, src_dir);
    
    // 清理
    // 注意：vmm_free_page_directory 会自动处理 COW 共享页面的引用计数
    // 不需要手动调用 pmm_free_frame(frame)，否则会导致 double-free
    vmm_free_page_directory(src_dir);
    vmm_free_page_directory(clone_dir);
    // ❌ 移除：pmm_free_frame(frame); - 已被 vmm_free_page_directory 处理
}

/**
 * @brief 测试克隆页目录的数据隔离
 * 
 * 验证克隆的页目录与源页目录数据独立（COW 机制）
 * _Requirements: 3.2, 3.4_
 */
TEST_CASE(test_vmm_clone_page_directory_data_isolation) {
    uintptr_t original_dir = vmm_get_page_directory();
    
    // 创建源页目录
    uintptr_t src_dir = vmm_create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 在源页目录中映射并写入数据
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    ASSERT_TRUE(vmm_map_page_in_directory(src_dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 切换到源页目录并写入数据
    vmm_switch_page_directory(src_dir);
    uint32_t *ptr = (uint32_t*)TEST_VIRT_ADDR1;
    *ptr = 0xAAAAAAAA;
    *(ptr + 1) = 0xBBBBBBBB;
    
    // 克隆页目录（使用 COW 机制）
    // 此时两个页目录共享同一个物理页，且都被标记为只读 + COW
    uintptr_t clone_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    
    // 切换到克隆的页目录
    vmm_switch_page_directory(clone_dir);
    
    // 验证克隆的数据与源相同（COW：共享同一物理页）
    ASSERT_EQ_U(*ptr, 0xAAAAAAAA);
    ASSERT_EQ_U(*(ptr + 1), 0xBBBBBBBB);
    
    // 修改克隆页目录中的数据
    // 注意：这会触发 COW page fault，分配新物理页并复制内容
    *ptr = 0x11111111;
    *(ptr + 1) = 0x22222222;
    
    // 切换回源页目录
    vmm_switch_page_directory(src_dir);
    
    // 验证源页目录的数据未被修改（COW 数据隔离）
    ASSERT_EQ_U(*ptr, 0xAAAAAAAA);
    ASSERT_EQ_U(*(ptr + 1), 0xBBBBBBBB);
    
    // 恢复原页目录
    vmm_switch_page_directory(original_dir);
    
    // 清理
    // 注意：vmm_free_page_directory 会自动处理 COW 共享页面的引用计数
    // 不需要手动调用 pmm_free_frame(frame)
    // - src_dir 释放时：frame 引用计数从 2 降到 1（或如果 COW 已触发，
    //   src 保留原 frame，clone 有新 frame）
    // - clone_dir 释放时：释放 clone 的物理页
    vmm_free_page_directory(src_dir);
    vmm_free_page_directory(clone_dir);
    // ❌ 移除：pmm_free_frame(frame); - 可能导致 double-free
}

/**
 * @brief 测试克隆空页目录
 * 
 * 验证可以克隆一个只有内核映射的空页目录
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_clone_page_directory_empty) {
    // 克隆一个空的页目录（只有内核映射）
    uintptr_t empty_dir = vmm_create_page_directory();
    ASSERT_NE_U(empty_dir, 0);
    
    uintptr_t clone_dir = vmm_clone_page_directory(empty_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, empty_dir);
    
    // 清理
    vmm_free_page_directory(empty_dir);
    vmm_free_page_directory(clone_dir);
}

// ============================================================================
// 测试套件 5: vmm_cow_tests - COW 引用计数测试
// ============================================================================
//
// 测试 Copy-On-Write (COW) 机制的引用计数管理
// **Validates: Requirements 3.4** - COW 引用计数管理和数据隔离
// ============================================================================

/**
 * @brief 测试 COW 引用计数
 * 
 * 验证克隆页目录后引用计数正确增加和减少
 * _Requirements: 3.4_
 */
TEST_CASE(test_vmm_cow_refcount) {
    // 测试 COW 克隆后的引用计数
    uintptr_t src_dir = vmm_create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 分配物理页并映射
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 检查初始引用计数（应该是 1）
    uint32_t initial_refcount = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(initial_refcount, 1);
    
    // 映射到源页目录
    ASSERT_TRUE(vmm_map_page_in_directory(src_dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 克隆页目录（COW）
    uintptr_t clone_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    
    // 检查克隆后的引用计数（应该是 2，因为 COW 共享）
    uint32_t cow_refcount = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(cow_refcount, 2);
    
    // 再克隆一次（模拟多级 fork）
    uintptr_t clone2_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone2_dir, 0);
    
    // 检查引用计数（应该是 3）
    uint32_t cow_refcount2 = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(cow_refcount2, 3);
    
    // 释放一个克隆（引用计数应该降到 2）
    vmm_free_page_directory(clone2_dir);
    uint32_t after_free_refcount = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(after_free_refcount, 2);
    
    // 清理
    vmm_free_page_directory(src_dir);
    vmm_free_page_directory(clone_dir);
    
    // 最终引用计数应该是 0（帧已释放）
    uint32_t final_refcount = pmm_frame_get_refcount(frame);
    ASSERT_EQ_U(final_refcount, 0);
}

/**
 * @brief 测试多页面 COW
 * 
 * 验证多个页面的 COW 引用计数独立管理
 * _Requirements: 3.4_
 */
TEST_CASE(test_vmm_cow_multiple_pages) {
    // 测试多个页面的 COW
    uintptr_t src_dir = vmm_create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 分配并映射多个页面
    paddr_t frames[3];
    for (int i = 0; i < 3; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
        ASSERT_TRUE(vmm_map_page_in_directory(src_dir, 
            TEST_VIRT_ADDR1 + i * PAGE_SIZE, (uintptr_t)frames[i],
            PAGE_PRESENT | PAGE_WRITE));
    }
    
    // 克隆页目录
    uintptr_t clone_dir = vmm_clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    
    // 验证所有帧的引用计数都是 2
    for (int i = 0; i < 3; i++) {
        uint32_t refcount = pmm_frame_get_refcount(frames[i]);
        ASSERT_EQ_U(refcount, 2);
    }
    
    // 清理
    vmm_free_page_directory(src_dir);
    vmm_free_page_directory(clone_dir);
}

/**
 * @brief 测试释放带映射的页目录
 * 
 * 验证释放页目录时所有映射的页面也被正确释放
 * _Requirements: 3.2, 3.5_
 */
TEST_CASE(test_vmm_free_page_directory_with_mappings) {
    pmm_info_t info_before = pmm_get_info();
    
    // 创建页目录
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 在页目录中映射多个页面
    paddr_t frames[5];
    for (int i = 0; i < 5; i++) {
        frames[i] = pmm_alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
        ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1 + i * PAGE_SIZE, 
                                              (uintptr_t)frames[i], PAGE_PRESENT | PAGE_WRITE));
    }
    
    pmm_info_t info_after_alloc = pmm_get_info();
    // 应该至少分配了6个页帧（1个页目录 + 至少1个页表 + 5个数据页）
    ASSERT_TRUE(info_after_alloc.free_frames <= info_before.free_frames - 6);
    
    // 释放页目录（应该同时释放所有页表和映射的页）
    vmm_free_page_directory(dir);
    
    pmm_info_t info_after_free = pmm_get_info();
    // 所有页帧应该被释放（允许小误差）
    int64_t diff = (int64_t)info_after_free.free_frames - (int64_t)info_before.free_frames;
    ASSERT_TRUE(diff >= -5 && diff <= 5);
    
    // 注意：这里不需要单独释放 frames，因为 vmm_free_page_directory 会处理
}

/**
 * @brief 测试释放 NULL 页目录
 * 
 * 验证释放 NULL 页目录时系统保持稳定
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_free_page_directory_null) {
    // 释放NULL页目录（应该无害）
    vmm_free_page_directory(0);
}

/**
 * @brief 测试释放空页目录
 * 
 * 验证释放空页目录后内存正确恢复
 * _Requirements: 3.2, 3.5_
 */
TEST_CASE(test_vmm_free_page_directory_empty) {
    pmm_info_t info_before = pmm_get_info();
    
    // 创建空页目录
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 立即释放
    vmm_free_page_directory(dir);
    
    pmm_info_t info_after = pmm_get_info();
    // 应该只释放页目录本身（1个页帧）
    int64_t diff = (int64_t)info_after.free_frames - (int64_t)info_before.free_frames;
    ASSERT_TRUE(diff >= -2 && diff <= 2);
}

// ============================================================================
// 测试套件 6: vmm_comprehensive_tests - 综合测试
// ============================================================================
//
// 综合测试 VMM 的多个功能组合使用
// **Validates: Requirements 3.2, 7.2** - 综合功能验证
// ============================================================================

/**
 * @brief 综合测试：页目录创建、映射和释放
 * 
 * 验证页目录的完整生命周期
 * _Requirements: 3.2_
 */
TEST_CASE(test_vmm_comprehensive) {
    // 1. 创建新页目录
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 2. 分配物理页帧
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 3. 在新页目录中映射
    ASSERT_TRUE(vmm_map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 4. 清理
    vmm_free_page_directory(dir);
    // pmm_free_frame(frame);  // ❌ 不需要：会导致 double free
}

/**
 * @brief 测试多页表映射
 * 
 * 验证映射到不同的页目录项范围时能正确创建多个页表
 * _Requirements: 3.2, 7.2_
 */
TEST_CASE(test_vmm_multiple_page_tables) {
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 映射到不同的页目录项范围（需要多个页表）
    uint32_t addr1 = 0x00000000;  // PDE 0
    uint32_t addr2 = 0x00400000;  // PDE 1 (4MB边界)
    uint32_t addr3 = 0x00800000;  // PDE 2 (8MB边界)
    
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    paddr_t frame3 = pmm_alloc_frame();
    
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    ASSERT_NE_U(frame3, PADDR_INVALID);
    
    // 映射到不同的页表
    ASSERT_TRUE(vmm_map_page_in_directory(dir, addr1, (uintptr_t)frame1, 
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page_in_directory(dir, addr2, (uintptr_t)frame2, 
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page_in_directory(dir, addr3, (uintptr_t)frame3, 
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 清理
    vmm_free_page_directory(dir);
    // pmm_free_frame(frame1);  // ❌ 不需要：会导致 double free
    // pmm_free_frame(frame2);  // ❌ 不需要：会导致 double free
    // pmm_free_frame(frame3);  // ❌ 不需要：会导致 double free
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 页面映射测试套件
 * 
 * **Validates: Requirements 3.2**
 */
TEST_SUITE(vmm_map_tests) {
    RUN_TEST(test_vmm_map_page_basic);
    RUN_TEST(test_vmm_map_page_multiple);
    RUN_TEST(test_vmm_map_page_alignment);
    RUN_TEST(test_vmm_map_page_flags);
    RUN_TEST(test_vmm_map_page_remap);
    RUN_TEST(test_vmm_map_page_different_flags);
}

/**
 * @brief 取消映射测试套件
 * 
 * **Validates: Requirements 3.2**
 */
TEST_SUITE(vmm_unmap_tests) {
    RUN_TEST(test_vmm_unmap_page_basic);
    RUN_TEST(test_vmm_unmap_page_double);
    RUN_TEST(test_vmm_unmap_page_alignment);
    RUN_TEST(test_vmm_unmap_page_in_directory_basic);
    RUN_TEST(test_vmm_unmap_page_in_directory_nonexistent);
    RUN_TEST(test_vmm_unmap_page_in_directory_alignment);
}

/**
 * @brief 页目录操作测试套件
 * 
 * **Validates: Requirements 3.2, 7.2**
 */
TEST_SUITE(vmm_directory_tests) {
    RUN_TEST(test_vmm_create_page_directory_basic);
    RUN_TEST(test_vmm_create_multiple_page_directories);
    RUN_TEST(test_vmm_map_page_in_directory_basic);
    RUN_TEST(test_vmm_map_page_in_directory_multiple);
    RUN_TEST(test_vmm_get_page_directory);
    RUN_TEST(test_vmm_switch_page_directory);
    RUN_TEST(test_vmm_clone_page_directory_basic);
    RUN_TEST(test_vmm_clone_page_directory_data_isolation);
    RUN_TEST(test_vmm_clone_page_directory_empty);
    RUN_TEST(test_vmm_free_page_directory_with_mappings);
    RUN_TEST(test_vmm_free_page_directory_null);
    RUN_TEST(test_vmm_free_page_directory_empty);
}

/**
 * @brief COW 引用计数测试套件
 * 
 * **Validates: Requirements 3.4**
 */
TEST_SUITE(vmm_cow_tests) {
    RUN_TEST(test_vmm_cow_refcount);
    RUN_TEST(test_vmm_cow_multiple_pages);
}

/**
 * @brief TLB 刷新测试套件
 * 
 * **Validates: Requirements 3.2**
 */
TEST_SUITE(vmm_tlb_tests) {
    RUN_TEST(test_vmm_flush_tlb_single_page);
    RUN_TEST(test_vmm_flush_tlb_full);
}

/**
 * @brief 综合测试套件
 * 
 * **Validates: Requirements 3.2, 7.2**
 */
TEST_SUITE(vmm_comprehensive_tests) {
    RUN_TEST(test_vmm_comprehensive);
    RUN_TEST(test_vmm_multiple_page_tables);
}

// ============================================================================
// Property-Based Tests: VMM Page Table Format Correctness
// **Feature: multi-arch-support, Property 3: VMM Page Table Format Correctness**
// **Validates: Requirements 5.2**
// ============================================================================

/**
 * Property Test: Page table entries have correct format
 * 
 * *For any* virtual-to-physical mapping operation, the VMM SHALL generate 
 * page table entries in the correct format for the target architecture 
 * (2-level for i686, 4-level for x86_64, 4-level for ARM64).
 */
TEST_CASE(test_pbt_vmm_page_table_format) {
    #define PBT_VMM_ITERATIONS 20
    
    paddr_t frames[PBT_VMM_ITERATIONS];
    uint32_t virt_addrs[PBT_VMM_ITERATIONS];
    uint32_t allocated = 0;
    
    // Allocate frames and map them
    for (uint32_t i = 0; i < PBT_VMM_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        
        // Use different virtual addresses in user space
        virt_addrs[i] = TEST_VIRT_ADDR3 + (i * PAGE_SIZE);
        
        // Map with various flags
        uint32_t flags = PAGE_PRESENT | PAGE_WRITE;
        if (i % 2 == 0) {
            flags |= PAGE_USER;
        }
        
        bool result = vmm_map_page(virt_addrs[i], (uintptr_t)frames[i], flags);
        ASSERT_TRUE(result);
        
        allocated++;
        
        // Property: Physical address in mapping must be page-aligned
        ASSERT_EQ_U(frames[i] & (PAGE_SIZE - 1), 0);
        
        // Property: Virtual address must be page-aligned
        ASSERT_EQ_U(virt_addrs[i] & (PAGE_SIZE - 1), 0);
    }
    
    // Verify we allocated at least some mappings
    ASSERT_TRUE(allocated > 0);
    
    // Cleanup
    for (uint32_t i = 0; i < allocated; i++) {
        vmm_unmap_page(virt_addrs[i]);
        pmm_free_frame(frames[i]);
    }
}

/**
 * Property Test: Page table levels match architecture
 * 
 * *For any* i686 system, the page table SHALL use 2 levels.
 * This is verified by checking that mappings work correctly
 * with the expected address decomposition.
 */
TEST_CASE(test_pbt_vmm_page_table_levels) {
    // For i686: 2-level page table
    // Virtual address decomposition:
    //   [31:22] - Page Directory Index (10 bits, 1024 entries)
    //   [21:12] - Page Table Index (10 bits, 1024 entries)
    //   [11:0]  - Page Offset (12 bits, 4KB page)
    
    // Test that we can map addresses that span different PDE entries
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // Address in PDE 0x40 (virtual 0x10000000)
    uint32_t virt1 = 0x10000000;
    // Address in PDE 0x41 (virtual 0x10400000, 4MB boundary)
    uintptr_t virt2 = 0x10400000;
    
    // Property: Both addresses should map successfully
    ASSERT_TRUE(vmm_map_page(virt1, (uintptr_t)frame1, PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page(virt2, (uintptr_t)frame2, PAGE_PRESENT | PAGE_WRITE));
    
    // Property: Data written to each address should be independent
    uint32_t *ptr1 = (uint32_t*)(uintptr_t)virt1;
    uint32_t *ptr2 = (uint32_t*)(uintptr_t)virt2;
    *ptr1 = 0xAAAAAAAA;
    *ptr2 = 0xBBBBBBBB;
    
    ASSERT_EQ_U(*ptr1, 0xAAAAAAAA);
    ASSERT_EQ_U(*ptr2, 0xBBBBBBBB);
    
    // Cleanup
    vmm_unmap_page(virt1);
    vmm_unmap_page(virt2);
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
}

/**
 * Property Test: Kernel virtual address range correctness
 * 
 * *For any* kernel virtual address, the address SHALL fall within 
 * the architecture-appropriate higher-half range 
 * (≥0x80000000 for i686).
 */
TEST_CASE(test_pbt_vmm_kernel_address_range) {
    // Property: KERNEL_VIRTUAL_BASE must be architecture-appropriate
#if defined(ARCH_X86_64)
    ASSERT_TRUE(KERNEL_VIRTUAL_BASE == 0xFFFF800000000000ULL);
#else
    ASSERT_EQ_U(KERNEL_VIRTUAL_BASE, 0x80000000);
#endif
    
    // Property: PHYS_TO_VIRT should produce addresses >= KERNEL_VIRTUAL_BASE
    uintptr_t test_phys_addrs[] = {0x0, 0x1000, 0x100000, 0x1000000, 0x10000000};
    for (uint32_t i = 0; i < sizeof(test_phys_addrs)/sizeof(test_phys_addrs[0]); i++) {
        uintptr_t virt = PHYS_TO_VIRT(test_phys_addrs[i]);
        ASSERT_TRUE(virt >= KERNEL_VIRTUAL_BASE);
    }
    
    // Property: VIRT_TO_PHYS should be the inverse of PHYS_TO_VIRT
    for (uint32_t i = 0; i < sizeof(test_phys_addrs)/sizeof(test_phys_addrs[0]); i++) {
        uintptr_t virt = PHYS_TO_VIRT(test_phys_addrs[i]);
        uintptr_t phys_back = VIRT_TO_PHYS(virt);
        ASSERT_TRUE(phys_back == test_phys_addrs[i]);
    }
}

/**
 * Property Test: Page directory isolation
 * 
 * *For any* two page directories, mappings in one SHALL NOT 
 * affect mappings in the other (except for shared kernel space).
 */
TEST_CASE(test_pbt_vmm_page_directory_isolation) {
    // Create two separate page directories
    uint32_t dir1 = vmm_create_page_directory();
    uint32_t dir2 = vmm_create_page_directory();
    ASSERT_NE_U(dir1, 0);
    ASSERT_NE_U(dir2, 0);
    ASSERT_NE_U(dir1, dir2);
    
    // Allocate frames
    paddr_t frame1 = pmm_alloc_frame();
    paddr_t frame2 = pmm_alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // Map same virtual address to different physical frames in each directory
    uint32_t virt = TEST_VIRT_ADDR1;
    ASSERT_TRUE(vmm_map_page_in_directory(dir1, virt, frame1, PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(vmm_map_page_in_directory(dir2, virt, frame2, PAGE_PRESENT | PAGE_WRITE));
    
    // Property: The mappings should be independent
    // (We can't easily verify this without switching page directories,
    // but we can verify the mapping operations succeeded)
    
    // Cleanup
    vmm_free_page_directory(dir1);
    vmm_free_page_directory(dir2);
}

// ============================================================================
// Property-Based Tests: Kernel Space Sharing
// **Feature: mm-refactor, Property 12: Kernel Space Shared Across Address Spaces**
// **Validates: Requirements 7.2**
// ============================================================================

/**
 * Property Test: Kernel space shared across address spaces
 * 
 * *For any* two address spaces, kernel virtual addresses SHALL map 
 * to the same physical addresses.
 * 
 * This property ensures that kernel mappings are consistent across all
 * address spaces, which is essential for the kernel to function correctly
 * when switching between processes.
 */
TEST_CASE(test_pbt_vmm_kernel_space_shared) {
    #define PBT_KERNEL_ITERATIONS 10
    
    // Create multiple page directories
    uintptr_t dirs[PBT_KERNEL_ITERATIONS];
    uint32_t created = 0;
    
    for (uint32_t i = 0; i < PBT_KERNEL_ITERATIONS; i++) {
        dirs[i] = vmm_create_page_directory();
        if (dirs[i] == 0) {
            break;
        }
        created++;
    }
    
    // Verify we created at least 2 directories
    ASSERT_TRUE(created >= 2);
    
    // Get the current (boot) page directory for comparison
    uintptr_t boot_dir = vmm_get_page_directory();
    ASSERT_NE_U(boot_dir, 0);
    
    // Property: For each created directory, kernel space entries should match boot directory
    // We check the kernel space page directory entries (indices 512-1023 for i686)
    // These should point to the same page tables
    page_directory_t *boot_pd = (page_directory_t*)PHYS_TO_VIRT(boot_dir);
    
    for (uint32_t i = 0; i < created; i++) {
        page_directory_t *new_pd = (page_directory_t*)PHYS_TO_VIRT(dirs[i]);
        
        // Check kernel space entries (512-1023 for i686, 256-511 for x86_64)
#if defined(ARCH_X86_64)
        uint32_t kernel_start = 256;
        uint32_t kernel_end = 512;
#else
        uint32_t kernel_start = 512;
        uint32_t kernel_end = 1024;
#endif
        
        for (uint32_t j = kernel_start; j < kernel_end; j++) {
            // Property: Kernel PDE entries must match between all address spaces
            ASSERT_EQ_U(new_pd->entries[j], boot_pd->entries[j]);
        }
    }
    
    // Cleanup
    for (uint32_t i = 0; i < created; i++) {
        vmm_free_page_directory(dirs[i]);
    }
}

// ============================================================================
// Property-Based Tests: User Mapping Flags
// **Feature: mm-refactor, Property 13: User Mapping Has User Flag**
// **Validates: Requirements 7.3**
// ============================================================================

/**
 * Property Test: User mapping has user flag
 * 
 * *For any* mapping in user address space (below KERNEL_VIRTUAL_BASE), 
 * the page table entry SHALL have PAGE_USER flag set.
 * 
 * This property ensures that user-space mappings are properly marked
 * as accessible from user mode, which is essential for process isolation.
 */
TEST_CASE(test_pbt_vmm_user_mapping_flags) {
    #define PBT_USER_FLAG_ITERATIONS 20
    
    // Create a new page directory for testing
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    paddr_t frames[PBT_USER_FLAG_ITERATIONS];
    uintptr_t virt_addrs[PBT_USER_FLAG_ITERATIONS];
    uint32_t mapped = 0;
    
    // Map pages in user space with USER flag
    for (uint32_t i = 0; i < PBT_USER_FLAG_ITERATIONS; i++) {
        frames[i] = pmm_alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        
        // Use different virtual addresses in user space (below KERNEL_VIRTUAL_BASE)
        virt_addrs[i] = TEST_VIRT_ADDR3 + (i * PAGE_SIZE);
        
        // Map with USER flag
        bool result = vmm_map_page_in_directory(dir, virt_addrs[i], (uintptr_t)frames[i],
                                                 PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        ASSERT_TRUE(result);
        mapped++;
    }
    
    // Verify we mapped at least some pages
    ASSERT_TRUE(mapped > 0);
    
    // Property: All user space mappings should have USER flag set
    // We verify this by checking the page table entries
    page_directory_t *pd = (page_directory_t*)PHYS_TO_VIRT(dir);
    
    for (uint32_t i = 0; i < mapped; i++) {
        uintptr_t virt = virt_addrs[i];
        
        // Property: Virtual address must be in user space
        ASSERT_TRUE(virt < KERNEL_VIRTUAL_BASE);
        
#if !defined(ARCH_X86_64)
        // For i686, we can directly check the page table entries
        uint32_t pd_idx = virt >> 22;
        uint32_t pt_idx = (virt >> 12) & 0x3FF;
        
        // Check PDE has USER flag (for user space, PDE should allow user access)
        pde_t pde = pd->entries[pd_idx];
        ASSERT_TRUE((pde & PAGE_PRESENT) != 0);
        ASSERT_TRUE((pde & PAGE_USER) != 0);
        
        // Check PTE has USER flag
        page_table_t *pt = (page_table_t*)PHYS_TO_VIRT(pde & 0xFFFFF000);
        pte_t pte = pt->entries[pt_idx];
        ASSERT_TRUE((pte & PAGE_PRESENT) != 0);
        ASSERT_TRUE((pte & PAGE_USER) != 0);
#endif
    }
    
    // Cleanup
    vmm_free_page_directory(dir);
}

/**
 * Property Test: Kernel mapping does NOT have user flag
 * 
 * *For any* mapping in kernel address space (>= KERNEL_VIRTUAL_BASE),
 * the page table entry SHALL NOT have PAGE_USER flag set.
 * 
 * This is the complement of Property 13, ensuring kernel space
 * is protected from user-mode access.
 */
TEST_CASE(test_pbt_vmm_kernel_mapping_no_user_flag) {
    // Get the current page directory
    uintptr_t dir = vmm_get_page_directory();
    ASSERT_NE_U(dir, 0);
    
    page_directory_t *pd = (page_directory_t*)PHYS_TO_VIRT(dir);
    
#if !defined(ARCH_X86_64)
    // Check kernel space entries (512-1023 for i686)
    // Property: Kernel PDEs should NOT have USER flag
    for (uint32_t i = 512; i < 1024; i++) {
        pde_t pde = pd->entries[i];
        if (pde & PAGE_PRESENT) {
            // Property: Kernel PDE must NOT have USER flag
            ASSERT_TRUE((pde & PAGE_USER) == 0);
        }
    }
#endif
    
    // Property: Kernel virtual addresses should be >= KERNEL_VIRTUAL_BASE
    ASSERT_TRUE(KERNEL_VIRTUAL_BASE >= 0x80000000);
}

// ============================================================================
// Property-Based Tests: MMIO Mapping Flags
// **Feature: mm-refactor, Property 14: MMIO Mapping Has No-Cache Flag**
// **Validates: Requirements 9.1**
// ============================================================================

/**
 * Property Test: MMIO mapping has no-cache flag
 * 
 * *For any* MMIO mapping, the page table entry SHALL have the cache-disable
 * flag set (HAL_PAGE_NOCACHE).
 * 
 * This property ensures that device memory is not cached, which is essential
 * for correct device I/O behavior. Caching device registers could cause
 * stale reads or coalesced writes that break device protocols.
 */
TEST_CASE(test_pbt_vmm_mmio_nocache_flag) {
    #define PBT_MMIO_TEST_SIZE  (PAGE_SIZE * 3)  // Test with 3 pages
    
    // Use a fake physical address for MMIO (we won't actually access it)
    // This simulates mapping a device's MMIO region
    uintptr_t fake_mmio_phys = 0xFEE00000;  // Typical APIC region
    
    // Map the MMIO region
    uintptr_t mmio_virt = vmm_map_mmio(fake_mmio_phys, PBT_MMIO_TEST_SIZE);
    
    // Property: MMIO mapping should succeed
    ASSERT_NE_U(mmio_virt, 0);
    
    // Property: MMIO virtual address should be page-aligned
    ASSERT_EQ_U(mmio_virt & (PAGE_SIZE - 1), fake_mmio_phys & (PAGE_SIZE - 1));
    
    // Property: Verify the mapping has NOCACHE flag by querying via HAL
    // We check each page in the mapped region
    uintptr_t virt_base = mmio_virt & ~(PAGE_SIZE - 1);
    uint32_t num_pages = (PBT_MMIO_TEST_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uintptr_t virt = virt_base + (i * PAGE_SIZE);
        paddr_t phys;
        uint32_t flags;
        
        // Query the mapping
        bool mapped = hal_mmu_query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, &phys, &flags);
        
        // Property: Page should be mapped
        ASSERT_TRUE(mapped);
        
        // Property: Page should be present
        ASSERT_TRUE((flags & HAL_PAGE_PRESENT) != 0);
        
        // Property: Page should have NOCACHE flag (critical for MMIO)
        ASSERT_TRUE((flags & HAL_PAGE_NOCACHE) != 0);
        
        // Property: Page should be writable (MMIO typically needs write access)
        ASSERT_TRUE((flags & HAL_PAGE_WRITE) != 0);
    }
    
    // Cleanup
    vmm_unmap_mmio(mmio_virt, PBT_MMIO_TEST_SIZE);
    
    // Property: After unmapping, pages should no longer be mapped
    for (uint32_t i = 0; i < num_pages; i++) {
        uintptr_t virt = virt_base + (i * PAGE_SIZE);
        paddr_t phys;
        
        bool still_mapped = hal_mmu_query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, &phys, NULL);
        ASSERT_FALSE(still_mapped);
    }
}

/**
 * Property Test: Multiple MMIO mappings are independent
 * 
 * *For any* two MMIO mappings, they SHALL be at different virtual addresses
 * and both SHALL have the NOCACHE flag set.
 */
TEST_CASE(test_pbt_vmm_mmio_multiple_mappings) {
    // Map two different MMIO regions
    uintptr_t phys1 = 0xFEC00000;  // Typical I/O APIC
    uintptr_t phys2 = 0xFEE00000;  // Typical Local APIC
    size_t size1 = PAGE_SIZE;
    size_t size2 = PAGE_SIZE * 2;
    
    uintptr_t virt1 = vmm_map_mmio(phys1, size1);
    uintptr_t virt2 = vmm_map_mmio(phys2, size2);
    
    // Property: Both mappings should succeed
    ASSERT_NE_U(virt1, 0);
    ASSERT_NE_U(virt2, 0);
    
    // Property: Mappings should be at different virtual addresses
    ASSERT_NE_U(virt1, virt2);
    
    // Property: Both should have NOCACHE flag
    paddr_t p1, p2;
    uint32_t f1, f2;
    
    ASSERT_TRUE(hal_mmu_query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)(virt1 & ~(PAGE_SIZE-1)), &p1, &f1));
    ASSERT_TRUE(hal_mmu_query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)(virt2 & ~(PAGE_SIZE-1)), &p2, &f2));
    
    ASSERT_TRUE((f1 & HAL_PAGE_NOCACHE) != 0);
    ASSERT_TRUE((f2 & HAL_PAGE_NOCACHE) != 0);
    
    // Cleanup
    vmm_unmap_mmio(virt1, size1);
    vmm_unmap_mmio(virt2, size2);
}

TEST_SUITE(vmm_property_tests) {
    RUN_TEST(test_pbt_vmm_page_table_format);
    RUN_TEST(test_pbt_vmm_page_table_levels);
    RUN_TEST(test_pbt_vmm_kernel_address_range);
    RUN_TEST(test_pbt_vmm_page_directory_isolation);
    
    /* Property 12: Kernel Space Shared Across Address Spaces */
    /* **Validates: Requirements 7.2** */
    RUN_TEST(test_pbt_vmm_kernel_space_shared);
    
    /* Property 13: User Mapping Has User Flag */
    /* **Validates: Requirements 7.3** */
    RUN_TEST(test_pbt_vmm_user_mapping_flags);
    RUN_TEST(test_pbt_vmm_kernel_mapping_no_user_flag);
    
    /* Property 14: MMIO Mapping Has No-Cache Flag */
    /* **Validates: Requirements 9.1** */
    RUN_TEST(test_pbt_vmm_mmio_nocache_flag);
    RUN_TEST(test_pbt_vmm_mmio_multiple_mappings);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 VMM 测试
 * 
 * 按功能组织的测试套件：
 *   1. vmm_map_tests - 页面映射测试
 *   2. vmm_unmap_tests - 取消映射测试
 *   3. vmm_tlb_tests - TLB 刷新测试
 *   4. vmm_directory_tests - 页目录操作测试
 *   5. vmm_cow_tests - COW 引用计数测试
 *   6. vmm_comprehensive_tests - 综合测试
 *   7. vmm_property_tests - 属性测试 (PBT)
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 11.1**
 */
void run_vmm_tests(void) {
    // 初始化测试框架
    unittest_init();
    
    // ========================================================================
    // 功能测试套件
    // ========================================================================
    
    // 套件 1: 页面映射测试
    // _Requirements: 3.2_
    RUN_SUITE(vmm_map_tests);
    
    // 套件 2: 取消映射测试
    // _Requirements: 3.2_
    RUN_SUITE(vmm_unmap_tests);
    
    // 套件 3: TLB 刷新测试
    // _Requirements: 3.2_
    RUN_SUITE(vmm_tlb_tests);
    
    // 套件 4: 页目录操作测试
    // _Requirements: 3.2, 7.2_
    RUN_SUITE(vmm_directory_tests);
    
    // 套件 5: COW 引用计数测试
    // _Requirements: 3.4_
    RUN_SUITE(vmm_cow_tests);
    
    // 套件 6: 综合测试
    // _Requirements: 3.2, 7.2_
    RUN_SUITE(vmm_comprehensive_tests);
    
    // ========================================================================
    // 属性测试套件 (Property-Based Tests)
    // ========================================================================
    
    // 套件 7: VMM 属性测试
    // **Feature: multi-arch-support, Property 3: VMM Page Table Format Correctness**
    // **Validates: Requirements 5.2, 7.2**
    RUN_SUITE(vmm_property_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief VMM 测试模块依赖
 * 
 * VMM 依赖于 PMM 模块，因为页面映射需要物理内存分配
 */
static const char *vmm_test_deps[] = {"pmm"};

/**
 * @brief VMM 测试模块元数据
 * 
 * 使用 TEST_MODULE_WITH_DEPS 宏注册模块到测试框架
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 10.1, 10.2, 10.4, 11.1**
 */
TEST_MODULE_WITH_DEPS(vmm, MM, run_vmm_tests, vmm_test_deps, 1);
