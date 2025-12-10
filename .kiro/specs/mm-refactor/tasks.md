# Implementation Plan: 内存管理子系统重构

## Phase 1: 类型系统和基础设施

- [x] 1. 创建内存管理类型定义
  - [x] 1.1 创建 `src/include/mm/mm_types.h`
    - 定义 `paddr_t` (uint64_t)
    - 定义 `vaddr_t` (uintptr_t)
    - 定义 `pfn_t` (uint64_t)
    - 定义地址转换宏 (PADDR_TO_PFN, PFN_TO_PADDR, etc.)
    - 定义架构相关常量 (PHYS_ADDR_BITS, PAGE_TABLE_LEVELS)
    - _Requirements: 1.1, 1.2, 1.4, 1.5_
  - [x] 1.2 Write property test for type sizes
    - **Property 1: Physical Address Type Size**
    - **Property 2: Virtual Address Type Size**
    - **Validates: Requirements 1.1, 1.2**
  - [x] 1.3 Write property test for PFN conversion
    - **Property 3: PFN Conversion Round-Trip**
    - **Validates: Requirements 1.5**

- [x] 2. 创建页表抽象层
  - [x] 2.1 创建 `src/include/mm/pgtable.h`
    - 定义 `pte_t`/`pde_t` 类型（架构相关大小）
    - 定义 PTE 操作宏 (MAKE_PTE, PTE_ADDR, PTE_FLAGS, PTE_PRESENT)
    - 定义虚拟地址分解宏（各架构）
    - _Requirements: 3.1, 3.3, 3.4_
  - [x] 2.2 Write property test for PTE construction
    - **Property 7: PTE Construction Round-Trip**
    - **Validates: Requirements 3.3, 3.4**


## Phase 2: PMM 重构

- [x] 3. 重构 PMM 接口
  - [x] 3.1 更新 `src/include/mm/pmm.h`
    - 修改 `pmm_alloc_frame()` 返回 `paddr_t`
    - 修改 `pmm_free_frame()` 参数为 `paddr_t`
    - 修改引用计数接口使用 `paddr_t`
    - 添加 `pmm_alloc_frames()` 连续分配接口
    - 更新 `pmm_info_t` 使用 `pfn_t`
    - _Requirements: 2.1, 2.2, 2.3_
  - [x] 3.2 更新 `src/mm/pmm.c` 实现
    - 修改位图索引使用 `pfn_t`
    - 修改引用计数数组支持大 PFN
    - 保持 i686 兼容性（限制 4GB）
    - 支持 x86_64 完整物理地址空间
    - _Requirements: 2.2, 2.3, 2.4, 2.5_
  - [x] 3.3 Write property test for PMM allocation alignment
    - **Property 5: PMM Allocation Returns Page-Aligned Address**
    - **Validates: Requirements 2.1, 2.2**
  - [x] 3.4 Write property test for reference counting
    - **Property 6: PMM Reference Count Consistency**
    - **Validates: Requirements 2.3**

- [x] 4. 更新 PMM 调用者
  - [x] 4.1 更新 `src/mm/vmm.c` 使用新 PMM 接口
    - 修改所有 `pmm_alloc_frame()` 调用处理 `paddr_t`
    - 修改地址转换使用新宏
    - _Requirements: 2.1_
  - [x] 4.2 更新 `src/mm/heap.c` 使用新 PMM 接口
    - _Requirements: 2.1_
  - [x] 4.3 更新驱动中的 PMM 调用
    - 搜索并更新所有 `pmm_alloc_frame` 调用
    - _Requirements: 2.1_

- [x] 5. Checkpoint - 验证 PMM 重构
  - Ensure all tests pass, ask the user if questions arise.

## Phase 3: HAL MMU 接口扩展

- [x] 6. 扩展 HAL MMU 接口
  - [x] 6.1 更新 `src/include/hal/hal.h` MMU 部分
    - 添加 `hal_addr_space_t` 类型定义
    - 添加 `hal_page_fault_info_t` 结构
    - 添加 `hal_mmu_query()` 接口
    - 添加 `hal_mmu_protect()` 接口
    - 添加 `hal_mmu_clone_space()` 接口
    - 添加 `hal_mmu_parse_fault()` 接口
    - 更新现有接口使用 `paddr_t`/`vaddr_t`
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

- [x] 7. 实现 i686 HAL MMU 扩展
  - [x] 7.1 更新 `src/arch/i686/mm/paging.c`
    - 实现 `hal_mmu_query()`
    - 实现 `hal_mmu_protect()`
    - 实现 `hal_mmu_clone_space()` (COW)
    - 实现 `hal_mmu_parse_fault()`
    - _Requirements: 4.1, 4.3, 4.4_
  - [x] 7.2 Write property test for i686 map-query round-trip
    - **Property 8: HAL MMU Map-Query Round-Trip**
    - **Validates: Requirements 4.1**
  - [x] 7.3 Write property test for i686 address space switch
    - **Property 9: Address Space Switch Consistency**
    - **Validates: Requirements 4.5**

- [x] 8. Checkpoint - 验证 i686 HAL MMU
  - Ensure all tests pass, ask the user if questions arise.


## Phase 4: x86_64 完整 VMM 实现

- [x] 9. 实现 x86_64 动态页表操作
  - [x] 9.1 更新 `src/arch/x86_64/mm/paging64.c`
    - 实现 `hal_mmu_map()` 4 级页表映射
    - 实现 `hal_mmu_unmap()` 取消映射
    - 实现 `hal_mmu_query()` 页表查询
    - 实现 `hal_mmu_protect()` 修改页属性
    - _Requirements: 5.1_
  - [x] 9.2 Write property test for x86_64 map-query
    - **Property 8: HAL MMU Map-Query Round-Trip (x86_64)**
    - **Validates: Requirements 5.1**

- [x] 10. 实现 x86_64 地址空间管理
  - [x] 10.1 实现 `hal_mmu_create_space()` for x86_64
    - 分配 PML4
    - 复制内核空间映射 (PML4[256..511])
    - _Requirements: 5.2_
  - [x] 10.2 实现 `hal_mmu_clone_space()` for x86_64
    - 实现 COW 语义
    - 复制用户空间页表结构
    - 共享物理页并设置只读
    - _Requirements: 5.3_
  - [x] 10.3 实现 `hal_mmu_destroy_space()` for x86_64
    - 释放所有用户空间页表
    - 递减共享物理页引用计数
    - _Requirements: 5.5_
  - [x] 10.4 Write property test for x86_64 COW
    - **Property 10: COW Clone Shares Physical Pages**
    - **Property 11: COW Write Triggers Copy**
    - **Validates: Requirements 5.3**
  - [x] 10.5 Write property test for x86_64 space destruction
    - **Property 15: Address Space Destruction Frees Memory**
    - **Validates: Requirements 5.5**

- [x] 11. 实现 x86_64 页错误处理
  - [x] 11.1 实现 `hal_mmu_parse_fault()` for x86_64
    - 解析 CR2 和错误码
    - 填充 `hal_page_fault_info_t`
    - _Requirements: 5.4_
  - [x] 11.2 更新 `vmm_handle_cow_page_fault()` 支持 x86_64
    - 移除 `#if defined(ARCH_X86_64)` stub
    - 使用 HAL 接口实现通用 COW 处理
    - _Requirements: 5.4_

- [x] 12. Checkpoint - 验证 x86_64 VMM
  - Ensure all tests pass, ask the user if questions arise.

## Phase 5: VMM 通用层重构

- [x] 13. 重构 VMM 使用 HAL 接口
  - [x] 13.1 更新 `src/mm/vmm.c`
    - 移除架构特定条件编译
    - 使用 `hal_mmu_*` 接口替代直接页表操作
    - 统一 `vmm_map_page()` 实现
    - 统一 `vmm_create_page_directory()` 实现
    - 统一 `vmm_clone_page_directory()` 实现
    - _Requirements: 4.1, 4.2, 4.4_
  - [x] 13.2 Write property test for kernel space sharing
    - **Property 12: Kernel Space Shared Across Address Spaces**
    - **Validates: Requirements 7.2**
  - [x] 13.3 Write property test for user mapping flags
    - **Property 13: User Mapping Has User Flag**
    - **Validates: Requirements 7.3**

- [x] 14. 实现 MMIO 映射
  - [x] 14.1 更新 `vmm_map_mmio()` 使用 HAL 接口
    - 使用 `HAL_PTE_NOCACHE` 标志
    - 支持所有架构
    - _Requirements: 9.1, 9.2_
  - [x] 14.2 Write property test for MMIO flags
    - **Property 14: MMIO Mapping Has No-Cache Flag**
    - **Validates: Requirements 9.1**

- [x] 15. Checkpoint - 验证 VMM 重构
  - Ensure all tests pass, ask the user if questions arise.


## Phase 6: ARM64 MMU 实现（基础）

- [x] 16. 实现 ARM64 MMU 基础
  - [x] 16.1 创建 `src/arch/arm64/mm/mmu.c`
    - 实现 `hal_mmu_init()` 配置 TCR_EL1, MAIR_EL1
    - 实现 `hal_mmu_flush_tlb()` 和 `hal_mmu_flush_tlb_all()`
    - 实现 `hal_mmu_switch_space()` 更新 TTBR0_EL1
    - 实现 `hal_mmu_get_fault_addr()` 读取 FAR_EL1
    - _Requirements: 6.1, 6.5_
  - [x] 16.2 实现 ARM64 页表操作
    - 实现 `hal_mmu_map()` 4 级转换表
    - 实现 `hal_mmu_unmap()`
    - 实现 `hal_mmu_query()`
    - _Requirements: 6.2_
  - [x] 16.3 实现 ARM64 地址空间管理
    - 实现 `hal_mmu_create_space()`
    - 实现 `hal_mmu_clone_space()` (COW)
    - 实现 `hal_mmu_destroy_space()`
    - _Requirements: 6.2_
  - [x] 16.4 实现 ARM64 页错误处理
    - 实现 `hal_mmu_parse_fault()` 解析 ESR_EL1
    - _Requirements: 6.4_

- [x] 17. Checkpoint - 验证 ARM64 MMU
  - Ensure all tests pass, ask the user if questions arise.

## Phase 7: 高级功能和优化

- [x] 18. 实现 DMA 内存管理
  - [x] 18.1 实现 `pmm_alloc_frames()` 连续分配
    - 支持 DMA 区域分配
    - _Requirements: 10.1_
  - [x] 18.2 添加 ARM64 缓存维护操作
    - 实现 `hal_cache_clean()`
    - 实现 `hal_cache_invalidate()`
    - _Requirements: 10.2_

- [x] 19. 实现调试功能
  - [x] 19.1 实现页表转储功能
    - 添加 `vmm_dump_page_tables()` 函数
    - _Requirements: 11.1_
  - [x] 19.2 实现 PMM 一致性检查
    - 添加 `pmm_verify_consistency()` 函数
    - _Requirements: 11.2_

- [x] 20. 大页支持
  - [x] 20.1 实现 2MB 大页分配
    - 修改 PMM 支持 2MB 对齐分配
    - _Requirements: 8.1_
  - [x] 20.2 实现大页映射
    - 修改 HAL MMU 支持大页标志
    - _Requirements: 8.2_

- [x] 21. Final Checkpoint - 完整功能验证
  - Ensure all tests pass, ask the user if questions arise.

