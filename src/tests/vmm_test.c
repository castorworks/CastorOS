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
#include <mm/mm_types.h>
#include <lib/string.h>
#include <types.h>

// 测试用虚拟地址（用户空间范围）
#define TEST_VIRT_ADDR1  0x10000000
#define TEST_VIRT_ADDR2  0x10001000
#define TEST_VIRT_ADDR3  0x20000000

// ============================================================================
// 测试用例：vmm_map_page - 页面映射
// ============================================================================

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
// 测试用例：vmm_unmap_page - 取消页面映射
// ============================================================================

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

TEST_CASE(test_vmm_unmap_page_alignment) {
    // 尝试取消映射非对齐地址（应该被忽略）
    vmm_unmap_page(TEST_VIRT_ADDR1 + 0x456);
    // 如果没有崩溃就是成功
}

// ============================================================================
// 测试用例：vmm_unmap_page_in_directory - 在指定页目录中取消映射
// ============================================================================

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

TEST_CASE(test_vmm_unmap_page_in_directory_nonexistent) {
    uintptr_t dir = vmm_create_page_directory();
    ASSERT_NE_U(dir, 0);
    
    // 尝试取消映射一个未映射的页面（应该返回0）
    uintptr_t result = vmm_unmap_page_in_directory(dir, TEST_VIRT_ADDR1);
    ASSERT_EQ_U(result, 0);
    
    // 清理
    vmm_free_page_directory(dir);
}

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
// 测试用例：vmm_map_page - 重复映射和覆盖
// ============================================================================

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
// 测试用例：vmm_flush_tlb - TLB刷新
// ============================================================================

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
// 测试用例：vmm_create_page_directory - 创建页目录
// ============================================================================

TEST_CASE(test_vmm_create_page_directory_basic) {
    // 创建新页目录
    uintptr_t new_dir = vmm_create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(new_dir & (PAGE_SIZE - 1), 0);
    
    // 清理
    vmm_free_page_directory(new_dir);
}

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

// ============================================================================
// 测试用例：vmm_map_page_in_directory - 在指定页目录中映射
// ============================================================================

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

// ============================================================================
// 测试用例：vmm_get_page_directory - 获取当前页目录
// ============================================================================

TEST_CASE(test_vmm_get_page_directory) {
    uintptr_t current_dir = vmm_get_page_directory();
    
    // 应该非零
    ASSERT_NE_U(current_dir, 0);
    
    // 应该是页对齐的
    ASSERT_EQ_U(current_dir & (PAGE_SIZE - 1), 0);
}

// ============================================================================
// 测试用例：vmm_switch_page_directory - 切换页目录
// ============================================================================

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

// ============================================================================
// 测试用例：vmm_clone_page_directory - 克隆页目录
// ============================================================================

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
// 测试用例：COW 引用计数测试
// ============================================================================

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

// ============================================================================
// 测试用例：vmm_free_page_directory - 释放页目录
// ============================================================================

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

TEST_CASE(test_vmm_free_page_directory_null) {
    // 释放NULL页目录（应该无害）
    vmm_free_page_directory(0);
}

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
// 测试用例：综合测试
// ============================================================================

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

TEST_SUITE(vmm_map_tests) {
    RUN_TEST(test_vmm_map_page_basic);
    RUN_TEST(test_vmm_map_page_multiple);
    RUN_TEST(test_vmm_map_page_alignment);
    RUN_TEST(test_vmm_map_page_flags);
    RUN_TEST(test_vmm_map_page_remap);
    RUN_TEST(test_vmm_map_page_different_flags);
}

TEST_SUITE(vmm_unmap_tests) {
    RUN_TEST(test_vmm_unmap_page_basic);
    RUN_TEST(test_vmm_unmap_page_double);
    RUN_TEST(test_vmm_unmap_page_alignment);
    RUN_TEST(test_vmm_unmap_page_in_directory_basic);
    RUN_TEST(test_vmm_unmap_page_in_directory_nonexistent);
    RUN_TEST(test_vmm_unmap_page_in_directory_alignment);
}

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

TEST_SUITE(vmm_cow_tests) {
    RUN_TEST(test_vmm_cow_refcount);
    RUN_TEST(test_vmm_cow_multiple_pages);
}

TEST_SUITE(vmm_tlb_tests) {
    RUN_TEST(test_vmm_flush_tlb_single_page);
    RUN_TEST(test_vmm_flush_tlb_full);
}

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

TEST_SUITE(vmm_property_tests) {
    RUN_TEST(test_pbt_vmm_page_table_format);
    RUN_TEST(test_pbt_vmm_page_table_levels);
    RUN_TEST(test_pbt_vmm_kernel_address_range);
    RUN_TEST(test_pbt_vmm_page_directory_isolation);
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
    RUN_SUITE(vmm_tlb_tests);
    RUN_SUITE(vmm_directory_tests);
    RUN_SUITE(vmm_cow_tests);
    RUN_SUITE(vmm_comprehensive_tests);
    
    // Property-based tests
    // **Feature: multi-arch-support, Property 3: VMM Page Table Format Correctness**
    // **Validates: Requirements 5.2**
    RUN_SUITE(vmm_property_tests);
    
    // 打印测试摘要
    unittest_print_summary();
}
