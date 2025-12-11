/**
 * @file cow_flag_test.h
 * @brief COW 标志和引用计数测试头文件
 * 
 * **Feature: test-refactor**
 * **Validates: Requirements 3.4**
 * 
 * 测试 Copy-on-Write 机制的正确性：
 *   1. COW 标志设置和清除
 *   2. 引用计数管理
 * 
 * COW (Copy-on-Write) 标志在各架构上的实现：
 * 
 * | 架构    | COW 标志位置           | 标志定义              |
 * |---------|------------------------|----------------------|
 * | i686    | Available bit 9        | I686_PTE_COW (0x200) |
 * | x86_64  | Available bit 9        | PTE64_COW (1 << 9)   |
 * | ARM64   | Software bit 56        | DESC_COW (1 << 56)   |
 * 
 * 所有架构通过统一的 HAL_PAGE_COW 标志进行抽象。
 * 
 * @see src/include/hal/hal.h - HAL_PAGE_COW 定义
 * @see src/include/mm/vmm.h - PAGE_COW 定义
 * @see src/include/mm/pgtable.h - PTE_FLAG_COW 定义
 * @see src/include/mm/pmm.h - 引用计数接口
 */

#ifndef _TESTS_COW_FLAG_TEST_H_
#define _TESTS_COW_FLAG_TEST_H_

/**
 * @brief 运行 COW 标志和引用计数测试
 * 
 * COW 标志测试内容：
 * 1. COW 标志可以通过 HAL 接口正确设置
 * 2. COW 标志可以通过 HAL 接口正确查询
 * 3. COW 标志可以通过 hal_mmu_protect 正确清除
 * 4. COW 页面被标记为只读（HAL_PAGE_WRITE 被清除）
 * 5. 多个页面的 COW 标志独立维护
 * 
 * 引用计数测试内容：
 * 1. 新分配帧的初始引用计数为 1
 * 2. pmm_frame_ref_inc() 正确增加引用计数
 * 3. pmm_frame_ref_dec() 正确减少引用计数
 * 4. 多次操作后引用计数一致性
 * 5. 多个帧的引用计数独立性
 * 6. 释放后引用计数为 0
 * 7. COW 共享释放行为（refcount > 1 时只减计数）
 */
void run_cow_flag_tests(void);

#endif /* _TESTS_COW_FLAG_TEST_H_ */
