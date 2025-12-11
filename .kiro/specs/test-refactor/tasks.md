# Implementation Plan

## Phase 1: 测试框架增强

- [x] 1. 增强测试模块接口
  - [x] 1.1 创建 test_module.h 定义模块元数据结构和注册宏
    - 定义 test_module_t 结构体
    - 实现 TEST_MODULE 和 TEST_MODULE_WITH_DEPS 宏
    - _Requirements: 10.1, 10.2, 11.1_
  - [ ]* 1.2 编写属性测试验证模块注册机制
    - **Property 16: Test Hierarchy Structure**
    - **Validates: Requirements 12.1**
  - [x] 1.3 更新 test_runner.c 支持模块化注册
    - 实现模块注册表
    - 支持按子系统和模块名过滤
    - _Requirements: 10.2, 12.2, 12.3, 13.1_

- [x] 2. 增强 PBT 框架
  - [x] 2.1 创建 pbt_generators.h 添加领域特定生成器
    - 实现 pbt_gen_paddr, pbt_gen_vaddr_user, pbt_gen_vaddr_kernel
    - 实现 pbt_gen_pte, pbt_gen_pte_with_flags
    - _Requirements: 2.2_
  - [ ]* 2.2 编写属性测试验证生成器有效性
    - **Property 2: PBT Custom Generator Validity**
    - **Validates: Requirements 2.2**
  - [x] 2.3 创建 pbt_patterns.h 添加属性模式宏
    - 实现 PBT_ROUNDTRIP, PBT_INVARIANT, PBT_IDEMPOTENT 宏
    - _Requirements: 2.4_
  - [x] 2.4 增强 PBT 失败报告
    - 添加种子、迭代号、收缩反例到失败输出
    - _Requirements: 2.1_
  - [ ]* 2.5 编写属性测试验证 PBT-ktest 统计集成
    - **Property 3: PBT-ktest Statistics Integration**
    - **Validates: Requirements 2.3**

- [ ] 3. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 2: 内存管理测试完善

- [x] 4. 完善 PMM 测试
  - [x] 4.1 重构 pmm_test.c 使用新的模块接口
    - 添加模块元数据
    - 按功能组织测试套件
    - _Requirements: 10.1, 11.1_
  - [ ]* 4.2 编写属性测试验证分配对齐和唯一性
    - **Property 4: PMM Allocation Alignment and Uniqueness**
    - **Validates: Requirements 3.1**

- [x] 5. 完善 VMM 测试
  - [x] 5.1 重构 vmm_test.c 使用新的模块接口
    - 添加模块元数据
    - 分离架构相关测试
    - _Requirements: 10.1, 7.2_
  - [ ]* 5.2 编写属性测试验证映射查询往返
    - **Property 5: VMM Map-Query Round-Trip**
    - **Validates: Requirements 3.2**

- [x] 6. 完善 Heap 测试
  - [x] 6.1 重构 heap_test.c 使用新的模块接口
    - 添加模块元数据
    - 增加边界情况测试
    - _Requirements: 10.1, 11.1_
  - [ ]* 6.2 编写属性测试验证分配对齐
    - **Property 6: Heap Allocation Alignment**
    - **Validates: Requirements 3.3**

- [x] 7. 新增 COW 测试模块
  - [x] 7.1 创建 src/tests/cow_flag_test.c（如不存在则完善）
    - 测试 COW 标志设置和清除
    - 测试引用计数管理
    - _Requirements: 3.4_
  - [ ]* 7.2 编写属性测试验证引用计数一致性
    - **Property 7: COW Reference Count Consistency**
    - **Validates: Requirements 3.4**
  - [ ]* 7.3 编写属性测试验证内存泄漏检测
    - **Property 8: Memory Leak Detection**
    - **Validates: Requirements 3.5**

- [ ]* 8. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 3: 文件系统测试新增

- [x] 9. 新增 VFS 测试模块
  - [x] 9.1 创建 src/tests/fs/vfs_test.c
    - 测试文件打开、关闭、读写
    - 测试目录操作
    - _Requirements: 4.1, 4.2_
  - [ ]* 9.2 编写属性测试验证读写往返
    - **Property 9: VFS Read-Write Round-Trip**
    - **Validates: Requirements 4.2**

- [x] 10. 新增 Ramfs 测试模块
  - [x] 10.1 创建 src/tests/fs/ramfs_test.c
    - 测试文件创建、删除
    - 测试内容持久化
    - _Requirements: 4.4_
  - [ ]* 10.2 编写属性测试验证创建删除一致性
    - **Property 10: Ramfs Create-Delete Consistency**
    - **Validates: Requirements 4.4**

- [x] 11. 新增 FAT32 测试模块（可选）
  - [x] 11.1 创建 src/tests/fs/fat32_test.c
    - 测试目录项解析
    - 测试短文件名和长文件名
    - _Requirements: 4.3_

- [x] 12. 新增 Devfs 测试模块（可选）
  - [x] 12.1 创建 src/tests/fs/devfs_test.c
    - 测试设备注册
    - 测试设备节点访问
    - _Requirements: 4.5_

- [ ]* 13. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 4: 网络栈测试新增

- [x] 14. 新增 Checksum 测试模块
  - [x] 14.1 创建 src/tests/net/checksum_test.c
    - 使用已知测试向量验证校验和
    - 测试边界情况
    - _Requirements: 5.1_

- [x] 15. 新增 Netbuf 测试模块
  - [x] 15.1 创建 src/tests/net/netbuf_test.c
    - 测试缓冲区分配和释放
    - 测试数据读写
    - _Requirements: 5.5_
  - [ ]* 15.2 编写属性测试验证数据完整性
    - **Property 12: Netbuf Data Integrity**
    - **Validates: Requirements 5.5**

- [x] 16. 新增 ARP 测试模块
  - [x] 16.1 创建 src/tests/net/arp_test.c
    - 测试 ARP 表添加和查找
    - 测试条目过期
    - _Requirements: 5.4_
  - [ ]* 16.2 编写属性测试验证查找往返
    - **Property 11: ARP Table Lookup Round-Trip**
    - **Validates: Requirements 5.4**

- [x] 17. 新增 IP 测试模块（可选）
  - [x] 17.1 创建 src/tests/net/ip_test.c
    - 测试 IP 包构造
    - 测试头部字段和校验和
    - _Requirements: 5.2_

- [x] 18. 新增 TCP 测试模块（可选）
  - [x] 18.1 创建 src/tests/net/tcp_test.c
    - 测试 TCP 段解析
    - 测试头部字段提取
    - _Requirements: 5.3_

- [ ] 19. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 5: 驱动测试框架

- [ ] 20. 创建 Mock I/O 框架
  - [ ] 20.1 创建 src/tests/common/mock_io.c
    - 实现 mock I/O 端口函数
    - 实现 mock MMIO 函数
    - _Requirements: 6.1_

- [x] 21. 新增 PCI 测试模块（可选）
  - [x] 21.1 创建 src/tests/drivers/pci_test.c
    - 测试配置空间读写
    - _Requirements: 6.2_
  - [ ]* 21.2 编写属性测试验证配置空间往返
    - **Property 13: Mock PCI Config Space Round-Trip**
    - **Validates: Requirements 6.2**

- [x] 22. 新增 Timer 测试模块（可选）
  - [x] 22.1 创建 src/tests/drivers/timer_test.c
    - 测试 tick 计数
    - 测试回调调用
    - _Requirements: 6.3_

- [x] 23. 新增 Serial 测试模块（可选）
  - [x] 23.1 创建 src/tests/drivers/serial_test.c
    - 测试字符发送和接收
    - _Requirements: 6.4_
  - [ ]* 23.2 编写属性测试验证字符往返
    - **Property 14: Mock Serial Character Round-Trip**
    - **Validates: Requirements 6.4**

- [ ] 24. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 6: 多架构测试支持

- [ ] 25. 完善架构测试
  - [ ] 25.1 重构 hal_test.c 使用新的模块接口
    - 添加模块元数据
    - 增强架构诊断信息
    - _Requirements: 7.1, 7.3_
  - [ ] 25.2 重构 pgtable_test.c 使用新的模块接口
    - 添加模块元数据
    - 分离架构相关测试
    - _Requirements: 7.2, 7.4_
  - [ ]* 25.3 编写属性测试验证页表级数
    - **Property 15: Page Table Level Correctness**
    - **Validates: Requirements 7.4**

- [ ] 26. 增强测试报告
  - [ ] 26.1 更新 test_runner.c 添加架构信息到报告
    - 在测试开始时打印架构详情
    - 在失败时提供架构特定调试提示
    - _Requirements: 7.1, 7.3_
  - [ ]* 26.2 编写属性测试验证报告格式一致性
    - **Property 1: Test Result Format Consistency**
    - **Validates: Requirements 1.4, 7.1**

- [ ] 27. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Phase 7: 测试文档和组织

- [ ] 28. 更新现有测试文件注释
  - [ ] 28.1 为所有测试文件添加标准头注释
    - 包含模块名、描述、功能引用
    - _Requirements: 9.1_
  - [ ] 28.2 为所有测试用例添加属性/行为描述注释
    - 包含 "For any..." 语句（对于 PBT）
    - 包含需求引用
    - _Requirements: 9.2, 9.3_

- [ ] 29. 创建测试覆盖率文档
  - [ ] 29.1 创建 docs/testing.md 文档
    - 列出所有测试模块和覆盖的需求
    - 提供测试运行指南
    - _Requirements: 8.2, 8.4, 9.4_

- [ ] 30. 更新 test_runner.c 注册所有新模块
  - [ ] 30.1 将所有新测试模块添加到测试运行器
    - 按子系统组织
    - 设置正确的依赖关系
    - _Requirements: 10.2, 10.3, 10.4_

- [ ] 31. Final Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.
