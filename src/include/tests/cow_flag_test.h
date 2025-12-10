/**
 * @file cow_flag_test.h
 * @brief COW 标志正确性属性测试头文件
 * 
 * Property-Based Tests for COW Flag Correctness
 * **Feature: multi-arch-support, Property 6: VMM COW Flag Correctness**
 * **Validates: Requirements 5.5**
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
 */

#ifndef _TESTS_COW_FLAG_TEST_H_
#define _TESTS_COW_FLAG_TEST_H_

/**
 * @brief 运行 COW 标志正确性属性测试
 * 
 * 测试内容：
 * 1. COW 标志可以通过 HAL 接口正确设置
 * 2. COW 标志可以通过 HAL 接口正确查询
 * 3. COW 标志可以通过 hal_mmu_protect 正确清除
 * 4. COW 页面被标记为只读（HAL_PAGE_WRITE 被清除）
 * 5. 多个页面的 COW 标志独立维护
 */
void run_cow_flag_tests(void);

#endif /* _TESTS_COW_FLAG_TEST_H_ */
