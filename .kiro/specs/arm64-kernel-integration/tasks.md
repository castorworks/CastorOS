# Implementation Plan

## Phase 1: 构建系统更新

- [x] 1. 更新 Makefile 支持完整 ARM64 构建
  - [x] 1.1 添加 PMM/VMM 模块到 ARM64 构建
    - 修改 Makefile 的 ARM64 源文件列表
    - 添加 `src/mm/pmm.c`, `src/mm/vmm.c`, `src/mm/heap.c`
    - 确保条件编译正确处理架构差异
    - _Requirements: 10.1_
  - [x] 1.2 添加任务管理模块到 ARM64 构建
    - 添加 `src/kernel/task.c`, `src/kernel/scheduler.c`
    - 添加 `src/kernel/syscall.c`
    - _Requirements: 10.1_
  - [x] 1.3 添加文件系统模块到 ARM64 构建
    - 添加 `src/fs/vfs.c`, `src/fs/ramfs.c`, `src/fs/devfs.c`
    - _Requirements: 10.2_
  - [x] 1.4 排除 x86 特定驱动
    - 确保 `src/drivers/x86/` 不包含在 ARM64 构建中
    - _Requirements: 10.3_

- [x] 2. Checkpoint - 验证构建系统
  - Ensure all tests pass, ask the user if questions arise.


## Phase 2: Boot Info 和 PMM 集成

- [x] 3. 完善 ARM64 Boot Info
  - [x] 3.1 实现 DTB 内存信息提取
    - 修改 `src/arch/arm64/boot/boot_info.c`
    - 从 DTB `/memory` 节点提取内存区域
    - 填充 `boot_info_t` 结构
    - _Requirements: 1.1_
  - [x] 3.2 实现内核物理地址范围检测
    - 使用链接器符号 `_kernel_start`, `_kernel_end`
    - 计算内核占用的物理内存范围
    - _Requirements: 1.1_

- [x] 4. 集成 PMM 到 ARM64
  - [x] 4.1 移除 PMM stub 实现
    - 删除 `src/arch/arm64/stubs.c` 中的 PMM stub 函数
    - 确保链接到 `src/mm/pmm.c`
    - _Requirements: 1.4_
  - [x] 4.2 适配 PMM 初始化
    - 修改 `pmm_init()` 支持 DTB 来源的 boot_info
    - 处理 ARM64 内存布局（设备内存区域等）
    - _Requirements: 1.1_
  - [ ]* 4.3 Write property test for PMM frame alignment
    - **Property 1: PMM Frame Alignment**
    - **Validates: Requirements 1.2**
  - [ ]* 4.4 Write property test for PMM reference count
    - **Property 2: PMM Reference Count Consistency**
    - **Validates: Requirements 1.3**

- [x] 5. Checkpoint - 验证 PMM 集成
  - Ensure all tests pass, ask the user if questions arise.


## Phase 3: VMM 集成

- [x] 6. 集成 VMM 到 ARM64
  - [x] 6.1 适配 VMM 初始化
    - 修改 `vmm_init()` 使用 HAL MMU 接口
    - 设置内核地址空间映射
    - _Requirements: 2.1_
  - [x] 6.2 实现内核直接映射区
    - 映射物理内存到 `0xFFFF_0000_0000_0000` 开始的区域
    - 使用 2MB 块映射提高效率
    - _Requirements: 2.1_
  - [x] 6.3 集成页错误处理
    - 连接 ARM64 页错误处理到 VMM
    - 修改 `src/arch/arm64/mm/fault.c` 调用 `vmm_handle_page_fault()`
    - _Requirements: 2.3_
  - [ ]* 6.4 Write property test for page fault interpretation
    - **Property 3: VMM Page Fault Interpretation**
    - **Validates: Requirements 2.3**

- [x] 7. 实现 COW 支持
  - [x] 7.1 验证 ARM64 COW 标志位
    - 确认 `DESC_COW` (bit 56) 正确设置和检测
    - 测试 `hal_mmu_protect()` 的 COW 标志操作
    - _Requirements: 2.4_
  - [x] 7.2 集成 COW 页错误处理
    - 修改 `vmm_handle_cow_page_fault()` 支持 ARM64
    - 验证页复制和权限更新
    - _Requirements: 2.4_
  - [ ]* 7.3 Write property test for COW bit correctness
    - **Property 4: VMM COW Bit Correctness**
    - **Validates: Requirements 2.4**

- [x] 8. Checkpoint - 验证 VMM 集成
  - Ensure all tests pass, ask the user if questions arise.


## Phase 4: 堆内存集成

- [x] 9. 集成堆管理到 ARM64
  - [x] 9.1 配置 ARM64 堆区域
    - 定义堆起始地址 `0xFFFF_0000_4000_0000`
    - 配置堆大小（初始 16MB，可扩展）
    - _Requirements: 3.1_
  - [x] 9.2 初始化堆管理器
    - 调用 `heap_init()` 设置堆区域
    - 映射初始堆页面
    - _Requirements: 3.1_
  - [ ]* 9.3 Write property test for heap allocation
    - **Property 5: Heap Allocation Validity**
    - **Validates: Requirements 3.2**
  - [ ]* 9.4 Write property test for heap round-trip
    - **Property 6: Heap Round-Trip**
    - **Validates: Requirements 3.3**

- [x] 10. Checkpoint - 验证堆集成
  - Ensure all tests pass, ask the user if questions arise.


## Phase 5: 任务管理集成

- [x] 11. 集成任务管理到 ARM64
  - [x] 11.1 适配任务创建
    - 修改 `task_create_kernel()` 使用 HAL 上下文接口
    - 分配 ARM64 上下文结构
    - _Requirements: 4.1_
  - [x] 11.2 移除任务 stub
    - 删除 `src/arch/arm64/stubs.c` 中的 `task_exit` stub
    - 链接到 `src/kernel/task.c`
    - _Requirements: 4.3_
  - [x] 11.3 集成调度器
    - 连接定时器中断到调度器
    - 实现 `schedule()` 调用 `hal_context_switch()`
    - _Requirements: 4.4_
  - [ ]* 11.4 Write property test for context preservation
    - **Property 7: Task Context Preservation**
    - **Validates: Requirements 4.1, 4.2**
  - [ ]* 11.5 Write property test for task exit cleanup
    - **Property 8: Task Exit Cleanup**
    - **Validates: Requirements 4.3**

- [x] 12. Checkpoint - 验证任务管理
  - Ensure all tests pass, ask the user if questions arise.


## Phase 6: 系统调用集成

- [x] 13. 集成系统调用分发
  - [x] 13.1 移除系统调用 stub
    - 删除 `src/arch/arm64/stubs.c` 中的 `syscall_dispatcher` stub
    - 链接到 `src/kernel/syscall.c`
    - _Requirements: 5.1_
  - [x] 13.2 适配 ARM64 系统调用入口
    - 修改 `src/arch/arm64/syscall/svc.S` 调用真正的分发器
    - 确保参数传递正确 (X8=num, X0-X5=args)
    - _Requirements: 5.1, 5.2_
  - [x] 13.3 验证系统调用返回
    - 确保返回值正确放入 X0
    - 测试基础系统调用 (exit, write)
    - _Requirements: 5.3_
  - [ ]* 13.4 Write property test for syscall round-trip
    - **Property 9: System Call Round-Trip**
    - **Validates: Requirements 5.1, 5.2, 5.3**
  - [ ]* 13.5 Write property test for syscall error consistency
    - **Property 10: System Call Error Consistency**
    - **Validates: Requirements 5.4**

- [x] 14. Checkpoint - 验证系统调用
  - Ensure all tests pass, ask the user if questions arise.


## Phase 7: 用户模式支持

- [x] 15. 实现用户进程创建
  - [x] 15.1 实现用户地址空间创建
    - 使用 `hal_mmu_create_space()` 创建新 TTBR0
    - 映射用户代码、数据、栈区域
    - _Requirements: 6.1_
  - [x] 15.2 实现用户模式切换
    - 验证 `hal_context_init()` 正确设置 EL0 PSTATE
    - 测试 ERET 返回用户态
    - _Requirements: 6.2_
  - [x] 15.3 实现用户异常处理
    - 确保用户异常正确路由到内核
    - 实现信号机制 (SIGSEGV 等)
    - _Requirements: 6.3_
  - [ ]* 15.4 Write property test for address space isolation
    - **Property 11: User Process Address Space Isolation**
    - **Validates: Requirements 6.1**
  - [ ]* 15.5 Write property test for user exception handling
    - **Property 12: User Exception Handling**
    - **Validates: Requirements 6.3**

- [x] 16. 实现程序加载
  - [x] 16.1 实现 ELF 加载器适配
    - 确保 ELF 加载器支持 ARM64 ELF 格式
    - 映射代码段、数据段到用户空间
    - _Requirements: 6.4_
  - [x] 16.2 设置用户栈
    - 在用户地址空间顶部分配栈
    - 设置初始栈指针和参数
    - _Requirements: 6.4_

- [x] 17. Checkpoint - 验证用户模式
  - Ensure all tests pass, ask the user if questions arise.


## Phase 8: 进程管理系统调用

- [x] 18. 实现 fork 系统调用
  - [x] 18.1 适配 fork 到 ARM64
    - 使用 `hal_mmu_clone_space()` 复制地址空间
    - 设置 COW 标志
    - _Requirements: 7.1_
  - [x] 18.2 复制任务上下文
    - 复制 ARM64 寄存器状态
    - 设置子进程返回值为 0
    - _Requirements: 7.1_
  - [ ]* 18.3 Write property test for fork COW semantics
    - **Property 13: Fork COW Semantics**
    - **Validates: Requirements 7.1**

- [x] 19. 实现 exec 系统调用
  - [x] 19.1 适配 exec 到 ARM64
    - 清空用户地址空间
    - 加载新程序
    - _Requirements: 7.2_
  - [x] 19.2 设置新程序入口
    - 配置 PC 指向程序入口
    - 设置初始栈和参数
    - _Requirements: 7.2_

- [x] 20. 实现 exit 和 wait
  - [x] 20.1 适配 exit 到 ARM64
    - 清理进程资源
    - 通知父进程
    - _Requirements: 7.3_
  - [x] 20.2 适配 wait 到 ARM64
    - 实现等待子进程退出
    - 返回退出状态
    - _Requirements: 7.4_

- [x] 21. Checkpoint - 验证进程管理
  - Ensure all tests pass, ask the user if questions arise.


## Phase 9: 文件系统集成

- [x] 22. 集成 VFS 到 ARM64
  - [x] 22.1 初始化 VFS
    - 调用 `vfs_init()`
    - 挂载 ramfs 为根文件系统
    - _Requirements: 8.1_
  - [x] 22.2 初始化 devfs
    - 挂载 devfs 到 `/dev`
    - 注册串口设备
    - _Requirements: 8.1_
  - [ ]* 22.3 Write property test for file operations
    - **Property 14: File Operation Round-Trip**
    - **Validates: Requirements 8.2, 8.3, 8.4**

- [x] 23. Checkpoint - 验证文件系统
  - Ensure all tests pass, ask the user if questions arise.


## Phase 10: 调试支持和最终验证

- [x] 24. 配置 GDB 调试支持
  - [x] 24.1 创建 ARM64 GDB 配置文件
    - 创建 `.gdbinit-arm64`
    - 配置 ARM64 目标和符号加载
    - _Requirements: 9.1, 9.2_
  - [x] 24.2 更新 Makefile debug 目标
    - 添加 `debug-arm64` 目标
    - 配置 QEMU GDB 服务器
    - _Requirements: 9.1_

- [x] 25. 更新 kernel_main
  - [x] 25.1 使用通用 kernel_main
    - 修改 ARM64 使用 `src/kernel/kernel.c` 的 `kernel_main`
    - 移除 `src/arch/arm64/stubs.c` 中的 `kernel_main`
    - _Requirements: 10.1_
  - [x] 25.2 添加 ARM64 特定初始化
    - 在通用初始化流程中添加 ARM64 分支
    - 处理 DTB 解析等 ARM64 特定步骤
    - _Requirements: 10.1_

- [x] 26. 运行用户程序测试
  - [x] 26.1 测试 helloworld
    - 编译 ARM64 用户程序
    - 运行并验证输出
    - _Requirements: 6.4_
  - [x] 26.2 测试 shell
    - 运行用户 shell
    - 验证基本命令
    - _Requirements: 6.4_

- [ ] 27. Final Checkpoint - 完整功能验证
  - Ensure all tests pass, ask the user if questions arise.

