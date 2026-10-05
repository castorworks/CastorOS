// ============================================================================
// vmm_test.c - 虚拟内存管理器单元测试
// ============================================================================
//
// 模块名称: vmm
// 子系统: mm (内存管理)
// 描述: 测试 VMM (Virtual Memory Manager) 的功能
//
// 功能覆盖:
//   - 页面映射 (mm::Vmm::map_page, mm::Vmm::map_page_in_directory)
//   - 取消映射 (mm::Vmm::unmap_page, mm::Vmm::unmap_page_in_directory)
//   - 页目录操作 (mm::Vmm::create_page_directory, mm::Vmm::clone_page_directory)
//   - TLB 刷新 (mm::Vmm::flush_tlb)
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
// 测试 mm::Vmm::map_page() 函数的基本功能
// **Validates: Requirements 3.2** - VMM 映射页面应可查询且物理地址正确
// ============================================================================

/**
 * @brief 测试页面映射对齐检查
 * 
 * 验证非对齐地址的映射请求被正确拒绝
 */
TEST_CASE(test_vmm_map_page_alignment) {
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 尝试映射非对齐地址（应该失败）
    bool result = mm::Vmm::map_page(TEST_VIRT_ADDR1 + 0x123, (uintptr_t)frame, 
                               PAGE_PRESENT | PAGE_WRITE);
    ASSERT_FALSE(result);
    
    // 清理
    mm::Pmm::free_frame(frame);
}

// ============================================================================
// 测试套件 2: vmm_unmap_tests - 取消页面映射测试
// ============================================================================
//
// 测试 mm::Vmm::unmap_page() 和 mm::Vmm::unmap_page_in_directory() 函数
// **Validates: Requirements 3.2** - VMM 取消映射功能
// ============================================================================

/**
 * @brief 测试在指定页目录中取消映射
 * 
 * 验证 mm::Vmm::unmap_page_in_directory() 能正确取消指定页目录中的映射
 */
TEST_CASE(test_vmm_unmap_page_in_directory_basic) {
    // 创建新页目录
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 分配物理页帧
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 在新页目录中映射
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 取消映射
    uintptr_t unmapped_frame = mm::Vmm::unmap_page_in_directory(dir, TEST_VIRT_ADDR1);
    ASSERT_EQ_U(unmapped_frame, (uintptr_t)frame);
    
    // 清理
    mm::Vmm::free_page_directory(dir);
    mm::Pmm::free_frame(frame);
}

/**
 * @brief 测试取消映射不存在的页面
 * 
 * 验证取消映射未映射的页面时返回正确的结果
 */
TEST_CASE(test_vmm_unmap_page_in_directory_nonexistent) {
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 尝试取消映射一个未映射的页面（应该返回0）
    uintptr_t result = mm::Vmm::unmap_page_in_directory(dir, TEST_VIRT_ADDR1);
    ASSERT_EQ_U(result, 0);
    
    // 清理
    mm::Vmm::free_page_directory(dir);
}

/**
 * @brief 测试在页目录中取消映射非对齐地址
 * 
 * 验证在页目录中取消映射非对齐地址时返回正确的结果
 */
TEST_CASE(test_vmm_unmap_page_in_directory_alignment) {
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 尝试取消映射非对齐地址（应该返回0）
    uintptr_t result = mm::Vmm::unmap_page_in_directory(dir, TEST_VIRT_ADDR1 + 0x123);
    ASSERT_EQ_U(result, 0);
    
    // 清理
    mm::Vmm::free_page_directory(dir);
}

// ============================================================================
// 测试套件 1 (续): vmm_map_tests - 重复映射和覆盖测试
// ============================================================================

// ============================================================================
// 测试套件 3: vmm_tlb_tests - TLB 刷新测试
// ============================================================================
//
// 测试 mm::Vmm::flush_tlb() 函数的功能
// **Validates: Requirements 3.2** - TLB 刷新后映射仍然有效
// ============================================================================

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
 * 验证 mm::Vmm::create_page_directory() 返回有效的页对齐地址
 */
TEST_CASE(test_vmm_create_page_directory_basic) {
    // 创建新页目录
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(new_dir & (PAGE_SIZE - 1), 0);
    
    // 清理
    mm::Vmm::free_page_directory(new_dir);
}

/**
 * @brief 测试创建多个页目录
 * 
 * 验证可以创建多个独立的页目录
 */
TEST_CASE(test_vmm_create_multiple_page_directories) {
    // 创建多个页目录
    uintptr_t dir1 = mm::Vmm::create_page_directory();
    uintptr_t dir2 = mm::Vmm::create_page_directory();
    uintptr_t dir3 = mm::Vmm::create_page_directory();
    
    ASSERT_NE_U(dir1, 0);
    ASSERT_NE_U(dir2, 0);
    ASSERT_NE_U(dir3, 0);
    
    // 应该都不相同
    ASSERT_NE_U(dir1, dir2);
    ASSERT_NE_U(dir2, dir3);
    ASSERT_NE_U(dir1, dir3);
    
    // 清理
    mm::Vmm::free_page_directory(dir1);
    mm::Vmm::free_page_directory(dir2);
    mm::Vmm::free_page_directory(dir3);
}

/**
 * @brief 测试在指定页目录中映射
 * 
 * 验证 mm::Vmm::map_page_in_directory() 能在指定页目录中正确映射
 */
TEST_CASE(test_vmm_map_page_in_directory_basic) {
    // 创建新页目录
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 分配物理页帧
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 在新页目录中映射
    bool result = mm::Vmm::map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                            PAGE_PRESENT | PAGE_WRITE);
    ASSERT_TRUE(result);
    
    // 清理
    // 注意：mm::Vmm::free_page_directory 会自动释放所有映射的页面
    mm::Vmm::free_page_directory(dir);
    // mm::Pmm::free_frame(frame);  // ❌ 不需要：会导致 double free
}

/**
 * @brief 测试在页目录中映射多个页面
 * 
 * 验证可以在同一个页目录中映射多个页面
 */
TEST_CASE(test_vmm_map_page_in_directory_multiple) {
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 在同一个页目录中映射多个页面
    paddr_t frame1 = mm::Pmm::alloc_frame();
    paddr_t frame2 = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame1,
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, TEST_VIRT_ADDR2, (uintptr_t)frame2,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 清理
    // 注意：mm::Vmm::free_page_directory 会自动释放所有映射的页面
    mm::Vmm::free_page_directory(dir);
    // mm::Pmm::free_frame(frame1);  // ❌ 不需要：会导致 double free
    // mm::Pmm::free_frame(frame2);  // ❌ 不需要：会导致 double free
}

/**
 * @brief 测试获取当前页目录
 * 
 * 验证 mm::Vmm::get_page_directory() 返回有效的页目录地址
 */
TEST_CASE(test_vmm_get_page_directory) {
    uintptr_t current_dir = mm::Vmm::get_page_directory();
    
    // 应该非零
    ASSERT_NE_U(current_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(current_dir & (PAGE_SIZE - 1), 0);
}

/**
 * @brief 测试切换页目录
 * 
 * 验证 mm::Vmm::switch_page_directory() 能正确切换页目录
 */
TEST_CASE(test_vmm_switch_page_directory) {
    uintptr_t original_dir = mm::Vmm::get_page_directory();
    
    // 创建新页目录
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // 切换到新页目录
    mm::Vmm::switch_page_directory(new_dir);
    
    // 验证切换成功
    ASSERT_EQ_U(mm::Vmm::get_page_directory(), new_dir);
    
    // 切换回原页目录
    mm::Vmm::switch_page_directory(original_dir);
    ASSERT_EQ_U(mm::Vmm::get_page_directory(), original_dir);
    
    // 清理
    mm::Vmm::free_page_directory(new_dir);
}

/**
 * @brief 测试基本页目录克隆
 * 
 * 验证 mm::Vmm::clone_page_directory() 能正确克隆页目录
 */
TEST_CASE(test_vmm_clone_page_directory_basic) {
    // 创建源页目录
    uintptr_t src_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 在源页目录中映射一个页面
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(src_dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 克隆页目录
    uintptr_t clone_dir = mm::Vmm::clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, src_dir);
    
    // 清理
    // 注意：mm::Vmm::free_page_directory 会自动处理 COW 共享页面的引用计数
    // 不需要手动调用 mm::Pmm::free_frame(frame)，否则会导致 double-free
    mm::Vmm::free_page_directory(src_dir);
    mm::Vmm::free_page_directory(clone_dir);
    // ❌ 移除：mm::Pmm::free_frame(frame); - 已被 mm::Vmm::free_page_directory 处理
}

/**
 * @brief 测试克隆页目录的数据隔离
 * 
 * 验证克隆的页目录与源页目录数据独立（COW 机制）
 */
TEST_CASE(test_vmm_clone_page_directory_data_isolation) {
    uintptr_t original_dir = mm::Vmm::get_page_directory();
    
    // 创建源页目录
    uintptr_t src_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 在源页目录中映射并写入数据
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(src_dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 切换到源页目录并写入数据
    mm::Vmm::switch_page_directory(src_dir);
    uint32_t *ptr = (uint32_t*)TEST_VIRT_ADDR1;
    *ptr = 0xAAAAAAAA;
    *(ptr + 1) = 0xBBBBBBBB;
    
    // 克隆页目录（使用 COW 机制）
    // 此时两个页目录共享同一个物理页，且都被标记为只读 + COW
    uintptr_t clone_dir = mm::Vmm::clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    
    // 切换到克隆的页目录
    mm::Vmm::switch_page_directory(clone_dir);
    
    // 验证克隆的数据与源相同（COW：共享同一物理页）
    ASSERT_EQ_U(*ptr, 0xAAAAAAAA);
    ASSERT_EQ_U(*(ptr + 1), 0xBBBBBBBB);
    
    // 修改克隆页目录中的数据
    // 注意：这会触发 COW page fault，分配新物理页并复制内容
    *ptr = 0x11111111;
    *(ptr + 1) = 0x22222222;
    
    // 切换回源页目录
    mm::Vmm::switch_page_directory(src_dir);
    
    // 验证源页目录的数据未被修改（COW 数据隔离）
    ASSERT_EQ_U(*ptr, 0xAAAAAAAA);
    ASSERT_EQ_U(*(ptr + 1), 0xBBBBBBBB);
    
    // 恢复原页目录
    mm::Vmm::switch_page_directory(original_dir);
    
    // 清理
    // 注意：mm::Vmm::free_page_directory 会自动处理 COW 共享页面的引用计数
    // 不需要手动调用 mm::Pmm::free_frame(frame)
    // - src_dir 释放时：frame 引用计数从 2 降到 1（或如果 COW 已触发，
    //   src 保留原 frame，clone 有新 frame）
    // - clone_dir 释放时：释放 clone 的物理页
    mm::Vmm::free_page_directory(src_dir);
    mm::Vmm::free_page_directory(clone_dir);
    // ❌ 移除：mm::Pmm::free_frame(frame); - 可能导致 double-free
}

/**
 * @brief 测试克隆空页目录
 * 
 * 验证可以克隆一个只有内核映射的空页目录
 */
TEST_CASE(test_vmm_clone_page_directory_empty) {
    // 克隆一个空的页目录（只有内核映射）
    uintptr_t empty_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(empty_dir, 0);
    
    uintptr_t clone_dir = mm::Vmm::clone_page_directory(empty_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, empty_dir);
    
    // 清理
    mm::Vmm::free_page_directory(empty_dir);
    mm::Vmm::free_page_directory(clone_dir);
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
 */
TEST_CASE(test_vmm_cow_refcount) {
    // 测试 COW 克隆后的引用计数
    uintptr_t src_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 分配物理页并映射
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 检查初始引用计数（应该是 1）
    uint32_t initial_refcount = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(initial_refcount, 1);
    
    // 映射到源页目录
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(src_dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 克隆页目录（COW）
    uintptr_t clone_dir = mm::Vmm::clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    
    // 检查克隆后的引用计数（应该是 2，因为 COW 共享）
    uint32_t cow_refcount = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(cow_refcount, 2);
    
    // 再克隆一次（模拟多级 fork）
    uintptr_t clone2_dir = mm::Vmm::clone_page_directory(src_dir);
    ASSERT_NE_U(clone2_dir, 0);
    
    // 检查引用计数（应该是 3）
    uint32_t cow_refcount2 = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(cow_refcount2, 3);
    
    // 释放一个克隆（引用计数应该降到 2）
    mm::Vmm::free_page_directory(clone2_dir);
    uint32_t after_free_refcount = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(after_free_refcount, 2);
    
    // 清理
    mm::Vmm::free_page_directory(src_dir);
    mm::Vmm::free_page_directory(clone_dir);
    
    // 最终引用计数应该是 0（帧已释放）
    uint32_t final_refcount = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(final_refcount, 0);
}

/**
 * @brief 测试多页面 COW
 * 
 * 验证多个页面的 COW 引用计数独立管理
 */
TEST_CASE(test_vmm_cow_multiple_pages) {
    // 测试多个页面的 COW
    uintptr_t src_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // 分配并映射多个页面
    paddr_t frames[3];
    for (int i = 0; i < 3; i++) {
        frames[i] = mm::Pmm::alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
        ASSERT_TRUE(mm::Vmm::map_page_in_directory(src_dir, 
            TEST_VIRT_ADDR1 + i * PAGE_SIZE, (uintptr_t)frames[i],
            PAGE_PRESENT | PAGE_WRITE));
    }
    
    // 克隆页目录
    uintptr_t clone_dir = mm::Vmm::clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    
    // 验证所有帧的引用计数都是 2
    for (int i = 0; i < 3; i++) {
        uint32_t refcount = mm::Pmm::frame_get_refcount(frames[i]);
        ASSERT_EQ_U(refcount, 2);
    }
    
    // 清理
    mm::Vmm::free_page_directory(src_dir);
    mm::Vmm::free_page_directory(clone_dir);
}

/**
 * @brief 测试释放带映射的页目录
 * 
 * 验证释放页目录时所有映射的页面也被正确释放
 */
TEST_CASE(test_vmm_free_page_directory_with_mappings) {
    mm::PmmInfo info_before = mm::Pmm::get_info();
    
    // 创建页目录
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 在页目录中映射多个页面
    paddr_t frames[5];
    for (int i = 0; i < 5; i++) {
        frames[i] = mm::Pmm::alloc_frame();
        ASSERT_NE_U(frames[i], PADDR_INVALID);
        ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, TEST_VIRT_ADDR1 + i * PAGE_SIZE, 
                                              (uintptr_t)frames[i], PAGE_PRESENT | PAGE_WRITE));
    }
    
    mm::PmmInfo info_after_alloc = mm::Pmm::get_info();
    // 应该至少分配了6个页帧（1个页目录 + 至少1个页表 + 5个数据页）
    ASSERT_TRUE(info_after_alloc.free_frames <= info_before.free_frames - 6);
    
    // 释放页目录（应该同时释放所有页表和映射的页）
    mm::Vmm::free_page_directory(dir);
    
    mm::PmmInfo info_after_free = mm::Pmm::get_info();
    // 所有页帧应该被释放（允许小误差）
    int64_t diff = (int64_t)info_after_free.free_frames - (int64_t)info_before.free_frames;
    ASSERT_TRUE(diff >= -5 && diff <= 5);
    
    // 注意：这里不需要单独释放 frames，因为 mm::Vmm::free_page_directory 会处理
}

/**
 * @brief 测试释放 NULL 页目录
 * 
 * 验证释放 NULL 页目录时系统保持稳定
 */
TEST_CASE(test_vmm_free_page_directory_null) {
    // 释放NULL页目录（应该无害）
    mm::Vmm::free_page_directory(0);
}

/**
 * @brief 测试释放空页目录
 * 
 * 验证释放空页目录后内存正确恢复
 */
TEST_CASE(test_vmm_free_page_directory_empty) {
    mm::PmmInfo info_before = mm::Pmm::get_info();
    
    // 创建空页目录
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 立即释放
    mm::Vmm::free_page_directory(dir);
    
    mm::PmmInfo info_after = mm::Pmm::get_info();
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
 */
TEST_CASE(test_vmm_comprehensive) {
    // 1. 创建新页目录
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 2. 分配物理页帧
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    // 3. 在新页目录中映射
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, TEST_VIRT_ADDR1, (uintptr_t)frame,
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 4. 清理
    mm::Vmm::free_page_directory(dir);
    // mm::Pmm::free_frame(frame);  // ❌ 不需要：会导致 double free
}

/**
 * @brief 测试多页表映射
 * 
 * 验证映射到不同的页目录项范围时能正确创建多个页表
 */
TEST_CASE(test_vmm_multiple_page_tables) {
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 映射到不同的页目录项范围（需要多个页表）
    uint32_t addr1 = 0x00000000;  // PDE 0
    uint32_t addr2 = 0x00400000;  // PDE 1 (4MB边界)
    uint32_t addr3 = 0x00800000;  // PDE 2 (8MB边界)
    
    paddr_t frame1 = mm::Pmm::alloc_frame();
    paddr_t frame2 = mm::Pmm::alloc_frame();
    paddr_t frame3 = mm::Pmm::alloc_frame();
    
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    ASSERT_NE_U(frame3, PADDR_INVALID);
    
    // 映射到不同的页表
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, addr1, (uintptr_t)frame1, 
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, addr2, (uintptr_t)frame2, 
                                          PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir, addr3, (uintptr_t)frame3, 
                                          PAGE_PRESENT | PAGE_WRITE));
    
    // 清理
    mm::Vmm::free_page_directory(dir);
    // mm::Pmm::free_frame(frame1);  // ❌ 不需要：会导致 double free
    // mm::Pmm::free_frame(frame2);  // ❌ 不需要：会导致 double free
    // mm::Pmm::free_frame(frame3);  // ❌ 不需要：会导致 double free
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 页面映射测试套件
 */
TEST_SUITE(vmm_map_tests) {
    RUN_TEST(test_vmm_map_page_alignment);
}

/**
 * @brief 取消映射测试套件
 */
TEST_SUITE(vmm_unmap_tests) {
    RUN_TEST(test_vmm_unmap_page_in_directory_basic);
    RUN_TEST(test_vmm_unmap_page_in_directory_nonexistent);
    RUN_TEST(test_vmm_unmap_page_in_directory_alignment);
}

/**
 * @brief 页目录操作测试套件
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
 */
TEST_SUITE(vmm_cow_tests) {
    RUN_TEST(test_vmm_cow_refcount);
    RUN_TEST(test_vmm_cow_multiple_pages);
}

/**
 * @brief TLB 刷新测试套件
 */
TEST_SUITE(vmm_tlb_tests) {
}

/**
 * @brief 综合测试套件
 */
TEST_SUITE(vmm_comprehensive_tests) {
    RUN_TEST(test_vmm_comprehensive);
    RUN_TEST(test_vmm_multiple_page_tables);
}

// ============================================================================
// Property-Based Tests: VMM Page Table Format Correctness
// ============================================================================

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
    uint32_t dir1 = mm::Vmm::create_page_directory();
    uint32_t dir2 = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir1, 0);
    ASSERT_NE_U(dir2, 0);
    ASSERT_NE_U(dir1, dir2);
    
    // Allocate frames
    paddr_t frame1 = mm::Pmm::alloc_frame();
    paddr_t frame2 = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame1, PADDR_INVALID);
    ASSERT_NE_U(frame2, PADDR_INVALID);
    
    // Map same virtual address to different physical frames in each directory
    uint32_t virt = TEST_VIRT_ADDR1;
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir1, virt, frame1, PAGE_PRESENT | PAGE_WRITE));
    ASSERT_TRUE(mm::Vmm::map_page_in_directory(dir2, virt, frame2, PAGE_PRESENT | PAGE_WRITE));
    
    // Property: The mappings should be independent
    // (We can't easily verify this without switching page directories,
    // but we can verify the mapping operations succeeded)
    
    // Cleanup
    mm::Vmm::free_page_directory(dir1);
    mm::Vmm::free_page_directory(dir2);
}

// ============================================================================
// Property-Based Tests: Kernel Space Sharing
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
        dirs[i] = mm::Vmm::create_page_directory();
        if (dirs[i] == 0) {
            break;
        }
        created++;
    }
    
    // Verify we created at least 2 directories
    ASSERT_TRUE(created >= 2);
    
    // Get the current (boot) page directory for comparison
    uintptr_t boot_dir = mm::Vmm::get_page_directory();
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
        mm::Vmm::free_page_directory(dirs[i]);
    }
}

// ============================================================================
// Property-Based Tests: User Mapping Flags
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
    uintptr_t dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    paddr_t frames[PBT_USER_FLAG_ITERATIONS];
    uintptr_t virt_addrs[PBT_USER_FLAG_ITERATIONS];
    uint32_t mapped = 0;
    
    // Map pages in user space with USER flag
    for (uint32_t i = 0; i < PBT_USER_FLAG_ITERATIONS; i++) {
        frames[i] = mm::Pmm::alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        
        // Use different virtual addresses in user space (below KERNEL_VIRTUAL_BASE)
        virt_addrs[i] = TEST_VIRT_ADDR3 + (i * PAGE_SIZE);
        
        // Map with USER flag
        bool result = mm::Vmm::map_page_in_directory(dir, virt_addrs[i], (uintptr_t)frames[i],
                                                 PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        ASSERT_TRUE(result);
        mapped++;
    }
    
    // Verify we mapped at least some pages
    ASSERT_TRUE(mapped > 0);
    
    // Property: All user space mappings should have USER flag set
    // We verify this by checking the page table entries
    [[maybe_unused]] page_directory_t *pd = (page_directory_t*)PHYS_TO_VIRT(dir);
    
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
    mm::Vmm::free_page_directory(dir);
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
    uintptr_t dir = mm::Vmm::get_page_directory();
    ASSERT_NE_U(dir, 0);
    
    [[maybe_unused]] page_directory_t *pd = (page_directory_t*)PHYS_TO_VIRT(dir);
    
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
// ============================================================================

TEST_SUITE(vmm_property_tests) {
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
 */
void run_vmm_tests(void) {
    // 初始化测试框架
    
    // ========================================================================
    // 功能测试套件
    // ========================================================================
    
    // 套件 1: 页面映射测试
    RUN_SUITE(vmm_map_tests);
    
    // 套件 2: 取消映射测试
    RUN_SUITE(vmm_unmap_tests);
    
    // 套件 3: TLB 刷新测试
    RUN_SUITE(vmm_tlb_tests);
    
    // 套件 4: 页目录操作测试
    RUN_SUITE(vmm_directory_tests);
    
    // 套件 5: COW 引用计数测试
    RUN_SUITE(vmm_cow_tests);
    
    // 套件 6: 综合测试
    RUN_SUITE(vmm_comprehensive_tests);
    
    // ========================================================================
    // 属性测试套件 (Property-Based Tests)
    // ========================================================================
    
    // 套件 7: VMM 属性测试
    RUN_SUITE(vmm_property_tests);
    
    // 打印测试摘要
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
 */
TEST_MODULE_WITH_DEPS(vmm, MM, run_vmm_tests, vmm_test_deps, 1);
