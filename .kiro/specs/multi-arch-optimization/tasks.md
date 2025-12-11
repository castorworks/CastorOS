# Implementation Plan

## Phase 1: HAL 基础设施增强

- [x] 1. 实现 HAL 错误码和能力查询
  - [x] 1.1 创建 `src/include/hal/hal_error.h` 定义统一错误码
    - 定义 `hal_error_t` 枚举和 `HAL_SUCCESS`/`HAL_FAILED` 宏
    - _Requirements: 12.1, 12.2, 12.3, 12.4_
  - [x] 1.2 创建 `src/include/hal/hal_caps.h` 定义能力查询接口
    - 定义 `hal_capabilities_t` 结构体
    - 声明 `hal_get_capabilities()` 函数
    - _Requirements: 1.1, 1.3_
  - [x] 1.3 实现 i686 架构的 `hal_get_capabilities()`
    - 在 `src/arch/i686/hal_caps.c` 中实现
    - 设置 `has_huge_pages=false`, `has_port_io=true`, `page_table_levels=2`
    - _Requirements: 1.1, 1.2_
  - [x] 1.4 实现 x86_64 架构的 `hal_get_capabilities()`
    - 在 `src/arch/x86_64/hal_caps.c` 中实现
    - 设置 `has_huge_pages=true`, `has_nx_bit=true`, `page_table_levels=4`
    - _Requirements: 1.1, 1.2_
  - [x] 1.5 实现 arm64 架构的 `hal_get_capabilities()`
    - 在 `src/arch/arm64/hal_caps.c` 中实现
    - 设置 `has_huge_pages=true`, `has_port_io=false`, `cache_coherent_dma=false`
    - _Requirements: 1.1, 1.2_
  - [ ]* 1.6 编写属性测试：HAL 能力查询一致性
    - **Property 2: HAL 能力查询一致性**
    - **Validates: Requirements 1.1, 1.3**

- [x] 2. Checkpoint - 确保所有测试通过
  - All three architectures (i686, x86_64, arm64) build successfully
  - i686 kernel boots and runs correctly in QEMU


## Phase 2: 页表抽象层

- [x] 3. 实现页表抽象接口
  - [x] 3.1 创建 `src/include/hal/pgtable.h` 定义页表抽象接口
    - 定义 `pte_flags_t` 枚举（架构无关标志）
    - 声明 `pgtable_make_entry()`, `pgtable_get_phys()`, `pgtable_get_flags()` 等函数
    - _Requirements: 3.1, 3.2_
  - [x] 3.2 实现 i686 架构的页表抽象
    - 在 `src/arch/i686/mm/pgtable.c` 中实现
    - 处理 2 级页表格式，32 位页表项
    - _Requirements: 3.1, 3.2, 3.3_
  - [x] 3.3 实现 x86_64 架构的页表抽象
    - 在 `src/arch/x86_64/mm/pgtable.c` 中实现
    - 处理 4 级页表格式，64 位页表项，NX 位
    - _Requirements: 3.1, 3.2, 3.3_
  - [x] 3.4 实现 arm64 架构的页表抽象
    - 在 `src/arch/arm64/mm/pgtable.c` 中实现
    - 处理 ARM64 页表格式，AP 位，PXN/UXN 位
    - _Requirements: 3.1, 3.2, 3.3_
  - [ ]* 3.5 编写属性测试：页表项往返一致性
    - **Property 1: 页表项往返一致性**
    - **Validates: Requirements 3.1, 3.2**

- [x] 4. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 3: VMM 代码统一

- [x] 5. 重构 VMM 使用页表抽象
  - [x] 5.1 修改 `src/mm/vmm.c` 使用 pgtable 抽象
    - 替换直接页表操作为 `pgtable_*` 函数调用
    - 移除 `#if defined(ARCH_X86_64)` 条件编译
    - _Requirements: 3.4, 4.2_
  - [x] 5.2 统一 COW 处理逻辑
    - 将 i686 和 x86_64 的 COW 处理合并为单一代码路径
    - 使用 HAL 接口进行架构特定操作
    - _Requirements: 4.1_
  - [x] 5.3 添加 VMM 错误码转换
    - 将 HAL 错误码转换为 VMM 返回值
    - 确保错误处理一致性
    - _Requirements: 4.4, 12.1_
  - [ ]* 5.4 编写属性测试：COW 页面复制正确性
    - **Property 4: COW 页面复制正确性**
    - **Validates: Requirements 4.1**
  - [ ]* 5.5 编写属性测试：页表映射一致性
    - **Property 9: 页表映射一致性**
    - **Validates: Requirements 9.3**

- [ ] 6. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 4: 逻辑中断号抽象

- [x] 7. 实现逻辑中断号接口
  - [x] 7.1 创建 `src/include/hal/hal_irq.h` 定义逻辑中断接口
    - 定义 `hal_irq_type_t` 枚举
    - 声明 `hal_irq_get_number()` 和 `hal_irq_register_logical()` 函数
    - _Requirements: 5.1, 5.4_
  - [x] 7.2 实现 i686 架构的逻辑 IRQ 映射
    - 在 `src/arch/i686/interrupt/hal_irq.c` 中实现
    - 映射 PIC IRQ 号
    - _Requirements: 5.1_
  - [x] 7.3 实现 x86_64 架构的逻辑 IRQ 映射
    - 在 `src/arch/x86_64/interrupt/hal_irq.c` 中实现
    - 支持 PIC 和 APIC
    - _Requirements: 5.1_
  - [x] 7.4 实现 arm64 架构的逻辑 IRQ 映射
    - 在 `src/arch/arm64/interrupt/hal_irq.c` 中实现
    - 映射 GIC IRQ 号
    - _Requirements: 5.1_
  - [ ]* 7.5 编写属性测试：逻辑 IRQ 映射有效性
    - **Property 5: 逻辑 IRQ 映射有效性**
    - **Validates: Requirements 5.1**

- [ ] 8. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.


## Phase 5: 平台设备模型

- [-] 9. 实现平台设备框架
  - [x] 9.1 创建 `src/include/drivers/platform.h` 定义平台设备接口
    - 定义 `platform_device_t` 和 `platform_driver_t` 结构体
    - 声明 `platform_driver_register()` 和 `platform_get_resource()` 函数
    - _Requirements: 6.1, 6.4_
  - [x] 9.2 实现平台设备核心框架
    - 在 `src/drivers/platform/platform.c` 中实现
    - 实现驱动注册和设备匹配逻辑
    - _Requirements: 6.1_
  - [x] 9.3 实现 PCI 设备到平台设备的转换
    - 在 `src/drivers/platform/pci_platform.c` 中实现
    - 从 PCI 配置空间提取资源信息
    - _Requirements: 6.2_
  - [x] 9.4 实现 DTB 设备到平台设备的转换
    - 在 `src/drivers/platform/dtb_platform.c` 中实现
    - 从设备树节点提取资源信息
    - _Requirements: 6.3_
  - [ ]* 9.5 编写属性测试：平台设备资源完整性
    - **Property 6: 平台设备资源完整性**
    - **Validates: Requirements 6.2, 6.3**

- [ ] 10. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 6: 系统调用 ABI 统一

- [x] 11. 实现系统调用参数统一接口
  - [x] 11.1 创建 `src/include/hal/hal_syscall.h` 定义系统调用接口
    - 定义 `hal_syscall_args_t` 结构体
    - 声明 `hal_syscall_get_args()` 和 `hal_syscall_set_return()` 函数
    - _Requirements: 7.1, 7.2_
  - [x] 11.2 实现 i686 架构的系统调用参数提取
    - 在 `src/arch/i686/syscall/hal_syscall.c` 中实现
    - 从栈和寄存器提取参数
    - _Requirements: 7.1, 7.3_
  - [x] 11.3 实现 x86_64 架构的系统调用参数提取
    - 在 `src/arch/x86_64/syscall/hal_syscall.c` 中实现
    - 从 RDI, RSI, RDX, R10, R8, R9 提取参数
    - _Requirements: 7.1, 7.3_
  - [x] 11.4 实现 arm64 架构的系统调用参数提取
    - 在 `src/arch/arm64/syscall/hal_syscall.c` 中实现
    - 从 X0-X5 提取参数
    - _Requirements: 7.1, 7.3_
  - [ ]* 11.5 编写属性测试：系统调用参数往返一致性
    - **Property 7: 系统调用参数往返一致性**
    - **Validates: Requirements 7.1, 7.2**

- [ ] 12. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 7: 启动信息标准化

- [x] 13. 实现启动信息标准化
  - [x] 13.1 创建 `src/include/boot/boot_info.h` 定义启动信息结构
    - 定义 `boot_info_t`, `boot_mmap_entry_t`, `boot_framebuffer_t` 结构体
    - _Requirements: 8.1, 8.2, 8.3, 8.4_
  - [x] 13.2 修改 i686 启动代码填充 boot_info_t
    - 在 `src/arch/i686/boot/` 中修改
    - 从 Multiboot 信息转换为 boot_info_t
    - _Requirements: 8.1_
  - [x] 13.3 修改 x86_64 启动代码填充 boot_info_t
    - 在 `src/arch/x86_64/boot/` 中修改
    - 从 Multiboot2 信息转换为 boot_info_t
    - _Requirements: 8.1_
  - [x] 13.4 修改 arm64 启动代码填充 boot_info_t
    - 在 `src/arch/arm64/boot/` 中修改
    - 从 DTB 或 UEFI 信息转换为 boot_info_t
    - _Requirements: 8.1_
  - [ ]* 13.5 编写属性测试：启动信息内存映射有效性
    - **Property 8: 启动信息内存映射有效性**
    - **Validates: Requirements 8.1**

- [ ] 14. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 8: HAL 接口完整性

- [x] 15. 完善 HAL 接口实现
  - [x] 15.1 实现 i686 缺失的 HAL 函数
    - 添加 `hal_arch_name()` 实现
    - 确保所有 HAL 函数有实现或 stub
    - _Requirements: 2.1, 2.4_
  - [x] 15.2 实现 x86_64 缺失的 HAL 函数
    - 完善 `hal_context_init()` 实现
    - 确保所有 HAL 函数有实现或 stub
    - _Requirements: 2.2, 2.3_
  - [x] 15.3 实现 arm64 缺失的 HAL 函数
    - 完善 `hal_context_init()` 实现
    - 确保所有 HAL 函数有实现或 stub
    - _Requirements: 2.2, 2.3_
  - [ ]* 15.4 编写属性测试：上下文初始化正确性
    - **Property 3: 上下文初始化正确性**
    - **Validates: Requirements 2.3**
  - [ ]* 15.5 编写属性测试：HAL 错误码一致性
    - **Property 10: HAL 错误码一致性**
    - **Validates: Requirements 12.1, 12.2, 12.4**

- [x] 16. Final Checkpoint - 确保所有测试通过
  - All three architectures (i686, x86_64, arm64) build successfully
  - i686 tests: ALL TESTS PASSED (19 test modules, including 6 property-based tests)
  - x86_64 and arm64 are in development status but compile correctly

