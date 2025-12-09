# Implementation Plan

> **注意**: 本 spec 的部分任务依赖于 `mm-refactor` spec 的完成。标记为 **[依赖 mm-refactor]** 的任务需要在 mm-refactor 相应 Phase 完成后才能进行。
>
> **依赖关系**:
> - Task 14.5-14.7 (x86_64 完整 VMM) → mm-refactor Phase 4
> - Task 20.1-20.3 (ARM64 MMU) → mm-refactor Phase 6
> - Task 22.1 (ARM64 页错误) → mm-refactor Phase 6
> - Task 35-36 (COW 机制) → mm-refactor Phase 3-6

## Phase 1: 基础架构重构

- [x] 1. 创建架构目录结构和构建系统
  - [x] 1.1 创建 `src/arch/` 目录结构
    - 创建 `src/arch/i686/`, `src/arch/x86_64/`, `src/arch/arm64/` 目录
    - 创建各架构的子目录: `boot/`, `cpu/`, `interrupt/`, `mm/`, `task/`, `syscall/`, `include/`
    - _Requirements: 12.1, 12.2_
  - [x] 1.2 修改 Makefile 支持多架构构建
    - 添加 ARCH 变量支持 (i686, x86_64, arm64)
    - 配置架构特定的编译器和标志
    - 实现架构特定源文件选择
    - _Requirements: 2.1, 2.2, 2.3, 2.4_
  - [x] 1.3 Write property test for build system architecture selection
    - **Property: Build system selects correct compiler for each ARCH value**
    - **Validates: Requirements 2.1, 2.2, 2.3**

- [x] 2. 创建 HAL 接口定义
  - [x] 2.1 创建 HAL 头文件 `src/include/hal/hal.h`
    - 定义 CPU 初始化接口
    - 定义中断管理接口
    - 定义 MMU 接口
    - 定义上下文切换接口
    - 定义系统调用接口
    - 定义定时器接口
    - 定义 I/O 操作接口
    - _Requirements: 1.1, 1.3_
  - [x] 2.2 创建架构特定类型头文件
    - 创建 `src/include/arch/i686/arch_types.h`
    - 创建 `src/include/arch/x86_64/arch_types.h`
    - 创建 `src/include/arch/arm64/arch_types.h`
    - _Requirements: 10.3_
  - [x] 2.3 修改 `src/include/types.h` 使用架构特定类型
    - 添加架构条件编译
    - 统一 uintptr_t, size_t 定义
    - _Requirements: 10.3_
  - [x] 2.4 Write property test for data type sizes
    - **Property 17: User Library Data Type Size Correctness**
    - **Validates: Requirements 10.3**


## Phase 2: i686 代码迁移

- [x] 3. 迁移 i686 引导代码
  - [x] 3.1 移动 `src/boot/` 到 `src/arch/i686/boot/`
    - 移动 boot.asm, multiboot.asm
    - 更新 Makefile 引用
    - _Requirements: 12.1_
  - [x] 3.2 移动 GDT/IDT/TSS 代码到 `src/arch/i686/cpu/`
    - 移动 gdt.c, gdt_asm.asm, idt.c, idt_asm.asm
    - 移动相关头文件到 `src/arch/i686/include/`
    - _Requirements: 12.1, 12.2_
  - [x] 3.3 移动中断处理代码到 `src/arch/i686/interrupt/`
    - 移动 isr.c, isr_asm.asm, irq.c, irq_asm.asm
    - 移动 PIC 相关代码
    - _Requirements: 12.1_

- [x] 4. 迁移 i686 内存管理代码
  - [x] 4.1 创建 `src/arch/i686/mm/paging.c`
    - 提取 vmm.c 中的 i686 特定页表操作
    - 实现 HAL MMU 接口
    - _Requirements: 5.2, 12.1_
  - [x] 4.2 保持 PMM 和 VMM 核心逻辑在 `src/mm/`
    - 修改 pmm.c 使用 HAL 接口
    - 修改 vmm.c 调用架构特定页表操作
    - _Requirements: 5.1_
  - [x] 4.3 Write property test for PMM page alignment
    - **Property 2: PMM Page Size Correctness**
    - **Validates: Requirements 5.1**
  - [x] 4.4 Write property test for VMM page table format
    - **Property 3: VMM Page Table Format Correctness**
    - **Validates: Requirements 5.2**
  - [ ] 4.5 **[依赖 mm-refactor]** 更新 i686 使用新类型系统
    - 参考 mm-refactor Phase 1-2
    - 使用 `paddr_t`, `vaddr_t`, `pfn_t` 类型
    - 更新 PMM 接口使用新类型
    - _Requirements: 5.1, mm-refactor 1.1, 1.2, 2.1_

- [x] 5. 迁移 i686 任务切换代码
  - [x] 5.1 移动 `task_asm.asm` 到 `src/arch/i686/task/`
    - 实现 HAL 上下文切换接口
    - _Requirements: 7.1, 12.1_
  - [x] 5.2 创建 i686 上下文结构定义
    - 在 `src/arch/i686/include/context.h` 定义 hal_context_t
    - _Requirements: 7.1_
  - [x] 5.3 Write property test for context switch register preservation
    - **Property 9: Context Switch Register Preservation**
    - **Validates: Requirements 7.1**

- [x] 6. 迁移 i686 系统调用代码
  - [x] 6.1 移动 `syscall_asm.asm` 到 `src/arch/i686/syscall/`
    - 实现 HAL 系统调用入口接口
    - _Requirements: 8.1, 12.1_
  - [x] 6.2 保持系统调用分发器在 `src/kernel/syscall.c`
    - 修改使用 HAL 接口
    - _Requirements: 8.1_
  - [x] 6.3 Write property test for system call round-trip
    - **Property 12: System Call Round-Trip Correctness**
    - **Validates: Requirements 8.1, 8.2, 8.3**

- [x] 7. Checkpoint - 验证 i686 迁移
  - Ensure all tests pass, ask the user if questions arise.

## Phase 3: 实现 HAL 适配层

- [x] 8. 实现 i686 HAL 适配
  - [x] 8.1 创建 `src/arch/i686/hal.c`
    - 实现 hal_cpu_init() 调用 GDT/TSS 初始化
    - 实现 hal_interrupt_init() 调用 IDT/ISR/IRQ 初始化
    - 实现 hal_mmu_init() 调用 VMM 初始化
    - _Requirements: 1.1_
  - [x] 8.2 实现 i686 I/O 操作
    - 实现端口 I/O 函数 (inb, outb, etc.)
    - 实现 MMIO 函数
    - _Requirements: 9.1, 9.2_
  - [x] 8.3 Write property test for HAL initialization dispatch
    - **Property 1: HAL Initialization Dispatch**
    - **Validates: Requirements 1.1**
  - [x] 8.4 Write property test for MMIO memory barriers
    - **Property 14: MMIO Memory Barrier Correctness**
    - **Validates: Requirements 9.1**

- [x] 9. 修改内核主函数使用 HAL
  - [x] 9.1 修改 `src/kernel/kernel.c`
    - 替换直接调用为 HAL 接口调用
    - 保持初始化顺序
    - _Requirements: 1.1_
  - [x] 9.2 验证 i686 功能完整性
    - 运行现有测试套件
    - 验证所有功能正常
    - _Requirements: 11.3_

- [x] 10. Checkpoint - 验证 HAL 适配
  - Ensure all tests pass, ask the user if questions arise.


## Phase 4: x86_64 架构支持

- [x] 11. 实现 x86_64 引导代码
  - [x] 11.1 创建 `src/arch/x86_64/boot/boot64.asm`
    - 实现 Multiboot2 头部
    - 实现实模式到保护模式切换
    - 实现保护模式到长模式切换
    - 设置临时 4 级页表
    - _Requirements: 3.1, 3.2_
  - [x] 11.2 创建 x86_64 链接器脚本 `linker_x86_64.ld`
    - 配置 64 位内核虚拟地址 (0xFFFF800000000000)
    - 配置段布局
    - _Requirements: 3.1_

- [x] 12. 实现 x86_64 CPU 初始化
  - [x] 12.1 创建 `src/arch/x86_64/cpu/gdt64.c`
    - 实现 64 位 GDT 结构
    - 实现 64 位 TSS 结构
    - _Requirements: 3.3_
  - [x] 12.2 创建 `src/arch/x86_64/cpu/idt64.c`
    - 实现 64 位 IDT 结构
    - 支持 IST (Interrupt Stack Table)
    - _Requirements: 3.4_

- [x] 13. 实现 x86_64 中断处理
  - [x] 13.1 创建 `src/arch/x86_64/interrupt/isr64.asm`
    - 实现 64 位异常处理入口
    - 保存/恢复 64 位寄存器
    - _Requirements: 6.1_
  - [x] 13.2 创建 `src/arch/x86_64/interrupt/apic.c`
    - 实现 Local APIC 初始化
    - 实现 I/O APIC 初始化
    - _Requirements: 6.3_
  - [x] 13.3 Write property test for x86_64 interrupt register preservation
    - **Property 7: Interrupt Register State Preservation (x86_64)**
    - **Validates: Requirements 6.1**

- [x] 14. 实现 x86_64 内存管理（基础）
  - [x] 14.1 创建 `src/arch/x86_64/mm/paging64.c`
    - 实现 4 级页表操作 (PML4, PDPT, PD, PT)
    - 实现基础 HAL MMU 接口 (hal_mmu_init, hal_mmu_map 静态映射)
    - _Requirements: 5.2_
  - [x] 14.2 实现 x86_64 页错误处理（基础）
    - 解析 64 位错误码
    - 读取 CR2 获取错误地址
    - _Requirements: 5.4_
  - [x] 14.3 Write property test for x86_64 kernel mapping range
    - **Property 4: VMM Kernel Mapping Range Correctness (x86_64)**
    - **Validates: Requirements 5.3**
  - [x] 14.4 Write property test for x86_64 page fault interpretation
    - **Property 5: VMM Page Fault Interpretation (x86_64)**
    - **Validates: Requirements 5.4**
  - [ ] 14.5 **[依赖 mm-refactor]** 完善 x86_64 动态页表操作
    - 参考 mm-refactor Phase 4 (Task 9-11)
    - 实现 `hal_mmu_map()` 动态 4 级页表映射
    - 实现 `hal_mmu_unmap()` 取消映射
    - 实现 `hal_mmu_query()` 页表查询
    - 实现 `hal_mmu_protect()` 修改页属性
    - _Requirements: 5.2, mm-refactor 5.1_
  - [ ] 14.6 **[依赖 mm-refactor]** 实现 x86_64 地址空间管理
    - 参考 mm-refactor Phase 4 (Task 10)
    - 实现 `hal_mmu_create_space()` 分配 PML4
    - 实现 `hal_mmu_clone_space()` COW 语义
    - 实现 `hal_mmu_destroy_space()` 释放页表
    - _Requirements: 5.2, mm-refactor 5.2, 5.3, 5.5_
  - [ ] 14.7 **[依赖 mm-refactor]** 完善 x86_64 页错误处理
    - 参考 mm-refactor Phase 4 (Task 11)
    - 实现 `hal_mmu_parse_fault()` 填充 `hal_page_fault_info_t`
    - 更新 `vmm_handle_cow_page_fault()` 支持 x86_64
    - _Requirements: 5.4, mm-refactor 5.4_

- [x] 15. 实现 x86_64 任务切换
  - [x] 15.1 创建 `src/arch/x86_64/task/context64.asm`
    - 保存/恢复 64 位通用寄存器 (RAX-R15)
    - 处理 CR3 切换
    - _Requirements: 7.1, 7.3_
  - [x] 15.2 创建 x86_64 上下文结构
    - 定义 64 位 hal_context_t
    - _Requirements: 7.1_
  - [x] 15.3 Write property test for x86_64 address space switch
    - **Property 10: Address Space Switch Correctness (x86_64)**
    - **Validates: Requirements 7.3**

- [x] 16. 实现 x86_64 系统调用
  - [x] 16.1 创建 `src/arch/x86_64/syscall/syscall64_asm.asm`
    - 实现 SYSCALL/SYSRET 机制
    - 配置 MSR 寄存器
    - _Requirements: 7.5, 8.1_
  - [x] 16.2 实现 x86_64 用户模式切换
    - 实现 IRETQ 返回用户态
    - _Requirements: 7.4_
  - [x] 16.3 Write property test for x86_64 user mode transition
    - **Property 11: User Mode Transition Correctness (x86_64)**
    - **Validates: Requirements 7.4**

- [x] 17. 实现 x86_64 HAL 适配
  - [x] 17.1 创建 `src/arch/x86_64/hal.c`
    - 实现所有 HAL 接口
    - _Requirements: 1.1_

- [x] 18. Checkpoint - 验证 x86_64 基础功能
  - Ensure all tests pass, ask the user if questions arise.


## Phase 5: ARM64 架构支持

- [ ] 19. 实现 ARM64 引导代码
  - [ ] 19.1 创建 `src/arch/arm64/boot/start.S`
    - 实现 UEFI/U-Boot 入口点
    - 验证 Exception Level (EL1)
    - 设置初始栈
    - _Requirements: 4.1_
  - [ ] 19.2 创建 ARM64 链接器脚本 `linker_arm64.ld`
    - 配置 64 位内核虚拟地址 (0xFFFF000000000000)
    - 配置段布局
    - _Requirements: 4.1_

- [ ] 20. 实现 ARM64 MMU 初始化
  - [ ] 20.1 创建 `src/arch/arm64/mm/mmu.c`
    - 配置 TCR_EL1 (Translation Control Register)
    - 配置 MAIR_EL1 (Memory Attribute Indirection Register)
    - 设置 TTBR0_EL1 和 TTBR1_EL1
    - 实现 `hal_mmu_init()` 初始化
    - 实现 `hal_mmu_flush_tlb()` 和 `hal_mmu_flush_tlb_all()`
    - 实现 `hal_mmu_switch_space()` 更新 TTBR0_EL1
    - 实现 `hal_mmu_get_fault_addr()` 读取 FAR_EL1
    - _Requirements: 4.2, mm-refactor 6.1, 6.5_
  - [ ] 20.2 实现 ARM64 4 级页表操作
    - 参考 mm-refactor Phase 6 (Task 16.2)
    - 实现 `hal_mmu_map()` 4 级转换表映射
    - 实现 `hal_mmu_unmap()` 取消映射
    - 实现 `hal_mmu_query()` 页表查询
    - 实现 HAL MMU 接口
    - _Requirements: 5.2, mm-refactor 6.2_
  - [ ] 20.3 实现 ARM64 地址空间管理
    - 参考 mm-refactor Phase 6 (Task 16.3)
    - 实现 `hal_mmu_create_space()` 分配顶级页表
    - 实现 `hal_mmu_clone_space()` COW 语义
    - 实现 `hal_mmu_destroy_space()` 释放页表
    - _Requirements: 5.2, mm-refactor 6.2_
  - [ ] 20.4 Write property test for ARM64 kernel mapping range
    - **Property 4: VMM Kernel Mapping Range Correctness (ARM64)**
    - **Validates: Requirements 5.3**

- [ ] 21. 实现 ARM64 异常处理
  - [ ] 21.1 创建 `src/arch/arm64/interrupt/vectors.S`
    - 实现异常向量表 (VBAR_EL1)
    - 处理同步异常、IRQ、FIQ、SError
    - _Requirements: 4.5_
  - [ ] 21.2 创建 `src/arch/arm64/interrupt/gic.c`
    - 实现 GICv2/GICv3 初始化
    - 实现中断分发
    - _Requirements: 4.4, 6.3_
  - [ ] 21.3 Write property test for ARM64 interrupt register preservation
    - **Property 7: Interrupt Register State Preservation (ARM64)**
    - **Validates: Requirements 6.2**
  - [ ] 21.4 Write property test for interrupt handler registration
    - **Property 8: Interrupt Handler Registration API Consistency**
    - **Validates: Requirements 6.4**

- [ ] 22. 实现 ARM64 页错误处理
  - [ ] 22.1 创建 `src/arch/arm64/mm/fault.c`
    - 参考 mm-refactor Phase 6 (Task 16.4)
    - 解析 ESR_EL1 (Exception Syndrome Register)
    - 读取 FAR_EL1 (Fault Address Register)
    - 实现 `hal_mmu_parse_fault()` 填充 `hal_page_fault_info_t`
    - _Requirements: 5.4, mm-refactor 6.4_
  - [ ] 22.2 Write property test for ARM64 page fault interpretation
    - **Property 5: VMM Page Fault Interpretation (ARM64)**
    - **Validates: Requirements 5.4**

- [ ] 23. 实现 ARM64 任务切换
  - [ ] 23.1 创建 `src/arch/arm64/task/context.S`
    - 保存/恢复 X0-X30, SP, PC, PSTATE
    - 处理 TTBR0_EL1 切换
    - _Requirements: 7.2, 7.3_
  - [ ] 23.2 创建 ARM64 上下文结构
    - 定义 arm64_context_t 作为 hal_context_t
    - _Requirements: 7.2_
  - [ ] 23.3 Write property test for ARM64 context switch
    - **Property 9: Context Switch Register Preservation (ARM64)**
    - **Validates: Requirements 7.2**
  - [ ] 23.4 Write property test for ARM64 address space switch
    - **Property 10: Address Space Switch Correctness (ARM64)**
    - **Validates: Requirements 7.3**

- [ ] 24. 实现 ARM64 系统调用
  - [ ] 24.1 创建 `src/arch/arm64/syscall/svc.S`
    - 实现 SVC 指令处理
    - 参数通过 X0-X5 传递
    - _Requirements: 7.5, 8.1, 8.2_
  - [ ] 24.2 实现 ARM64 用户模式切换
    - 实现 ERET 返回用户态
    - _Requirements: 7.4_
  - [ ] 24.3 Write property test for ARM64 user mode transition
    - **Property 11: User Mode Transition Correctness (ARM64)**
    - **Validates: Requirements 7.4**

- [ ] 25. 实现 ARM64 HAL 适配
  - [ ] 25.1 创建 `src/arch/arm64/hal.c`
    - 实现所有 HAL 接口
    - _Requirements: 1.1_
  - [ ] 25.2 实现 ARM64 内存屏障
    - 实现 DMB, DSB, ISB 指令封装
    - _Requirements: 9.1_
  - [ ] 25.3 **[依赖 mm-refactor]** 实现 ARM64 缓存维护操作
    - 参考 mm-refactor Phase 7 (Task 18.2)
    - 实现 `hal_cache_clean()` 清理缓存
    - 实现 `hal_cache_invalidate()` 无效化缓存
    - _Requirements: 9.4, mm-refactor 10.2_

- [ ] 26. 实现 ARM64 设备树解析
  - [ ] 26.1 创建 `src/arch/arm64/dtb/dtb.c`
    - 实现 DTB 解析器
    - 提取内存、中断、设备信息
    - _Requirements: 4.3_

- [ ] 27. Checkpoint - 验证 ARM64 基础功能
  - Ensure all tests pass, ask the user if questions arise.


## Phase 6: 用户空间适配

- [ ] 28. 适配用户空间库
  - [ ] 28.1 创建架构特定系统调用封装
    - 创建 `user/lib/src/arch/i686/syscall.S`
    - 创建 `user/lib/src/arch/x86_64/syscall.S`
    - 创建 `user/lib/src/arch/arm64/syscall.S`
    - _Requirements: 10.2_
  - [ ] 28.2 修改用户库 Makefile
    - 支持多架构编译
    - 选择正确的交叉编译器
    - _Requirements: 10.1, 10.4_
  - [ ] 28.3 Write property test for user library system call instruction
    - **Property 16: User Library System Call Instruction Correctness**
    - **Validates: Requirements 10.2**

- [ ] 29. 适配用户程序
  - [ ] 29.1 修改用户程序链接器脚本
    - 创建架构特定的 user.ld
    - _Requirements: 10.4_
  - [ ] 29.2 验证用户程序在各架构上运行
    - 测试 helloworld
    - 测试 shell
    - _Requirements: 10.1_

- [ ] 30. Checkpoint - 验证用户空间
  - Ensure all tests pass, ask the user if questions arise.

## Phase 7: 驱动适配

- [ ] 31. 重组驱动目录结构
  - [ ] 31.1 创建通用驱动目录 `src/drivers/common/`
    - 移动架构无关驱动 (VFS 相关)
    - _Requirements: 9.5_
  - [ ] 31.2 创建 x86 特定驱动目录 `src/drivers/x86/`
    - 移动 PCI, ATA, E1000, VGA, 键盘等驱动
    - _Requirements: 9.2, 9.5_
  - [ ] 31.3 创建 ARM 特定驱动目录 `src/drivers/arm/`
    - 预留 ARM 特定驱动位置
    - _Requirements: 9.5_

- [ ] 32. 实现 ARM64 基础驱动
  - [ ] 32.1 实现 ARM64 串口驱动 (PL011)
    - 用于早期调试输出
    - _Requirements: 9.3_
  - [ ] 32.2 实现 ARM64 定时器驱动
    - 使用 ARM Generic Timer
    - _Requirements: 9.3_

- [ ] 33. 实现 DMA 缓存一致性
  - [ ] 33.1 创建 `src/include/hal/dma.h`
    - 定义 DMA 缓存操作接口
    - _Requirements: 9.4_
  - [ ] 33.2 实现各架构 DMA 缓存操作
    - x86: 通常硬件一致性，无需操作
    - ARM64: 实现 cache clean/invalidate
    - _Requirements: 9.4_
  - [ ] 33.3 Write property test for DMA cache coherency
    - **Property 15: DMA Cache Coherency**
    - **Validates: Requirements 9.4**

- [ ] 34. Checkpoint - 验证驱动功能
  - Ensure all tests pass, ask the user if questions arise.

## Phase 8: COW 和高级内存功能

- [ ] 35. 适配 COW 机制
  - [ ] 35.1 **[依赖 mm-refactor]** 实现各架构 COW 页表标志
    - 参考 mm-refactor Phase 3-6 中的 COW 实现
    - i686: 使用 Available bit (mm-refactor Task 7 已实现)
    - x86_64: 使用 Available bit (mm-refactor Task 10.2)
    - ARM64: 使用 Software bit (mm-refactor Task 16.3)
    - 使用统一的 `HAL_PTE_COW` 标志
    - _Requirements: 5.5, mm-refactor 4.4, 5.3_
  - [ ] 35.2 Write property test for COW flag correctness
    - **Property 6: VMM COW Flag Correctness**
    - **Validates: Requirements 5.5**

- [ ] 36. 验证 fork/exec 在各架构上工作
  - [ ] 36.1 **[依赖 mm-refactor]** 测试 fork 系统调用
    - 验证 `hal_mmu_clone_space()` COW 正确工作
    - 验证 COW 页错误处理正确触发复制
    - _Requirements: 5.5, mm-refactor 4.4, 5.3_
  - [ ] 36.2 测试 exec 系统调用
    - 验证程序加载正确
    - _Requirements: 7.4_

- [ ] 37. Checkpoint - 验证高级内存功能
  - Ensure all tests pass, ask the user if questions arise.

## Phase 9: 测试和文档

- [ ] 38. 实现属性测试框架
  - [ ] 38.1 创建 `src/tests/pbt/pbt.c`
    - 实现轻量级属性测试框架
    - 实现随机数生成器
    - _Requirements: 11.3_
  - [ ] 38.2 Write property test for system call error consistency
    - **Property 13: System Call Error Consistency**
    - **Validates: Requirements 8.4**

- [ ] 39. 完善多架构测试
  - [ ] 39.1 更新测试运行器支持多架构
    - 修改 test_runner.c
    - _Requirements: 11.3_
  - [ ] 39.2 添加架构特定诊断信息
    - 在测试失败时输出架构信息
    - _Requirements: 11.4_

- [ ] 40. 配置 GDB 调试支持
  - [ ] 40.1 创建各架构 GDB 配置文件
    - 创建 .gdbinit 文件
    - _Requirements: 11.1, 11.2_
  - [ ] 40.2 更新 Makefile debug 目标
    - 支持各架构 QEMU 调试
    - _Requirements: 11.1, 11.2_

- [ ] 41. 更新文档
  - [ ] 41.1 更新 README.md
    - 添加多架构构建说明
    - _Requirements: 2.1, 2.2_
  - [ ] 41.2 更新开发环境文档
    - 添加各架构交叉编译器安装说明
    - _Requirements: 2.1, 2.2_

- [ ] 42. Final Checkpoint - 完整功能验证
  - Ensure all tests pass, ask the user if questions arise.
