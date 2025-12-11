# Requirements Document

## Introduction

本文档定义了 CastorOS 测试模块重构和完善的需求规格。目标是建立一个统一、可扩展、多架构兼容的测试框架，提升测试覆盖率和代码质量。当前测试框架已具备基础功能（ktest 单元测试框架、PBT 属性测试框架），但存在以下问题需要解决：

1. 测试组织结构不统一，部分测试文件缺少标准化注释
2. 部分模块测试覆盖不足（如 fs、net、drivers 等）
3. PBT 框架与 ktest 框架集成度不够
4. 测试报告格式需要增强
5. 多架构测试支持需要完善

## Glossary

- **ktest**: CastorOS 内核单元测试框架，提供 TEST_CASE、ASSERT_* 等宏
- **PBT (Property-Based Testing)**: 属性测试框架，通过随机生成输入验证属性
- **Test Suite**: 测试套件，一组相关测试用例的集合
- **Test Runner**: 测试运行器，负责执行所有注册的测试套件
- **Test Coverage**: 测试覆盖率，代码被测试执行的比例
- **Correctness Property**: 正确性属性，软件必须满足的形式化规范
- **Round-Trip Property**: 往返属性，操作与其逆操作组合后恢复原值
- **Invariant**: 不变量，在操作前后保持不变的属性

## Requirements

### Requirement 1: 测试框架统一化

**User Story:** As a kernel developer, I want a unified testing framework, so that I can write consistent tests across all modules.

#### Acceptance Criteria

1. WHEN a developer creates a new test file THEN the Test_Framework SHALL provide a standard template with required sections (header comment, includes, test cases, test suites, run function)
2. WHEN a test file is compiled THEN the Test_Framework SHALL validate that all test cases follow the naming convention `test_<module>_<function>_<scenario>`
3. WHEN a test suite is defined THEN the Test_Framework SHALL require explicit feature and requirement references in comments
4. WHEN the test runner executes THEN the Test_Framework SHALL report test results in a consistent format across all architectures

### Requirement 2: PBT 框架增强

**User Story:** As a kernel developer, I want an enhanced property-based testing framework, so that I can verify correctness properties with better diagnostics.

#### Acceptance Criteria

1. WHEN a PBT test fails THEN the PBT_Framework SHALL report the failing seed, iteration number, and shrunk counterexample
2. WHEN a PBT property is defined THEN the PBT_Framework SHALL support custom generators for domain-specific types (paddr_t, vaddr_t, pte_t)
3. WHEN a PBT test runs THEN the PBT_Framework SHALL integrate with ktest statistics for unified reporting
4. WHEN a developer writes a PBT property THEN the PBT_Framework SHALL provide macros for common property patterns (round-trip, invariant, idempotence)

### Requirement 3: 内存管理测试完善

**User Story:** As a kernel developer, I want comprehensive memory management tests, so that I can ensure PMM, VMM, and heap allocator correctness.

#### Acceptance Criteria

1. WHEN PMM allocates a frame THEN the PMM_Tests SHALL verify the frame is page-aligned and unique
2. WHEN VMM maps a page THEN the VMM_Tests SHALL verify the mapping is queryable and the physical address is correct
3. WHEN heap allocates memory THEN the Heap_Tests SHALL verify alignment, data integrity, and coalescing behavior
4. WHEN COW is triggered THEN the COW_Tests SHALL verify reference count management and data isolation
5. WHEN memory operations complete THEN the Memory_Tests SHALL verify no memory leaks via PMM info comparison

### Requirement 4: 文件系统测试新增

**User Story:** As a kernel developer, I want file system tests, so that I can verify VFS, FAT32, and other filesystem implementations.

#### Acceptance Criteria

1. WHEN VFS opens a file THEN the VFS_Tests SHALL verify the file descriptor is valid and operations succeed
2. WHEN VFS reads and writes data THEN the VFS_Tests SHALL verify data integrity through round-trip testing
3. WHEN FAT32 parses directory entries THEN the FAT32_Tests SHALL verify correct parsing of short and long filenames
4. WHEN ramfs creates files THEN the Ramfs_Tests SHALL verify file creation, deletion, and content persistence
5. WHEN devfs registers a device THEN the Devfs_Tests SHALL verify device node creation and accessibility

### Requirement 5: 网络栈测试新增

**User Story:** As a kernel developer, I want network stack tests, so that I can verify protocol implementations.

#### Acceptance Criteria

1. WHEN checksum is calculated THEN the Checksum_Tests SHALL verify correctness using known test vectors
2. WHEN IP packet is constructed THEN the IP_Tests SHALL verify header fields and checksum
3. WHEN TCP segment is parsed THEN the TCP_Tests SHALL verify correct extraction of header fields
4. WHEN ARP table is updated THEN the ARP_Tests SHALL verify entry addition, lookup, and expiration
5. WHEN netbuf is allocated THEN the Netbuf_Tests SHALL verify buffer management and data integrity

### Requirement 6: 驱动测试框架

**User Story:** As a kernel developer, I want driver testing support, so that I can verify driver implementations without hardware.

#### Acceptance Criteria

1. WHEN a driver test runs THEN the Driver_Test_Framework SHALL provide mock I/O port and MMIO functions
2. WHEN PCI driver is tested THEN the PCI_Tests SHALL verify configuration space read/write operations
3. WHEN timer driver is tested THEN the Timer_Tests SHALL verify tick counting and callback invocation
4. WHEN serial driver is tested THEN the Serial_Tests SHALL verify character transmission and reception

### Requirement 7: 多架构测试支持

**User Story:** As a kernel developer, I want multi-architecture test support, so that I can verify code works correctly on i686, x86_64, and ARM64.

#### Acceptance Criteria

1. WHEN tests run on different architectures THEN the Test_Runner SHALL report architecture-specific information in results
2. WHEN architecture-specific tests are defined THEN the Test_Framework SHALL support conditional compilation with clear markers
3. WHEN a test fails on one architecture THEN the Test_Framework SHALL provide architecture-specific debugging hints
4. WHEN page table tests run THEN the Pgtable_Tests SHALL verify correct behavior for 2-level (i686) and 4-level (x86_64/ARM64) page tables

### Requirement 8: 测试覆盖率追踪

**User Story:** As a kernel developer, I want test coverage tracking, so that I can identify untested code paths.

#### Acceptance Criteria

1. WHEN tests complete THEN the Test_Runner SHALL report the number of test modules, test cases, and assertions executed
2. WHEN a module lacks tests THEN the Test_Coverage_Report SHALL identify the module as untested
3. WHEN new code is added THEN the Test_Framework SHALL provide guidance on required test coverage
4. WHEN tests are organized THEN the Test_Framework SHALL maintain a mapping between requirements and test cases

### Requirement 9: 测试文档和注释标准

**User Story:** As a kernel developer, I want standardized test documentation, so that I can understand test purpose and coverage.

#### Acceptance Criteria

1. WHEN a test file is created THEN the Test_File SHALL include a header comment with module name, description, and feature references
2. WHEN a test case is defined THEN the Test_Case SHALL include a comment describing the property or behavior being tested
3. WHEN a PBT property is defined THEN the Property SHALL include a formal "For any..." statement and requirement reference
4. WHEN tests are reviewed THEN the Test_Documentation SHALL enable traceability from requirements to test cases

### Requirement 10: 测试模块化架构

**User Story:** As a kernel developer, I want a modular test architecture, so that I can organize, maintain, and extend tests independently.

#### Acceptance Criteria

1. WHEN a test module is created THEN the Test_Module SHALL be self-contained with its own header, implementation, and registration function
2. WHEN a test module is added THEN the Test_Runner SHALL support dynamic registration without modifying core framework code
3. WHEN test modules are organized THEN the Test_Framework SHALL group modules by subsystem (mm, fs, net, drivers, kernel, arch)
4. WHEN a test module depends on another THEN the Test_Framework SHALL support explicit dependency declaration
5. WHEN tests are compiled THEN the Build_System SHALL support building individual test modules independently

### Requirement 11: 测试模块接口标准

**User Story:** As a kernel developer, I want standardized test module interfaces, so that all test modules follow consistent patterns.

#### Acceptance Criteria

1. WHEN a test module is defined THEN the Test_Module SHALL export a single `run_<module>_tests(void)` function
2. WHEN a test module header is created THEN the Header SHALL declare only the run function and any shared test utilities
3. WHEN test utilities are shared THEN the Test_Framework SHALL provide a common utilities module for reusable helpers
4. WHEN a test module initializes THEN the Test_Module SHALL call `unittest_init()` at the start of its run function
5. WHEN a test module completes THEN the Test_Module SHALL call `unittest_print_summary()` to report results

### Requirement 12: 测试分层组织

**User Story:** As a kernel developer, I want hierarchical test organization, so that I can run tests at different granularity levels.

#### Acceptance Criteria

1. WHEN tests are organized THEN the Test_Framework SHALL support three levels: module, suite, and case
2. WHEN a subsystem is tested THEN the Test_Runner SHALL support running all modules in a subsystem (e.g., all mm tests)
3. WHEN a specific module is tested THEN the Test_Runner SHALL support running only that module's tests
4. WHEN a specific suite is tested THEN the Test_Framework SHALL support running only that suite within a module
5. WHEN test results are reported THEN the Test_Framework SHALL show hierarchical pass/fail counts

### Requirement 13: 测试执行优化

**User Story:** As a kernel developer, I want optimized test execution, so that I can run tests quickly during development.

#### Acceptance Criteria

1. WHEN tests are registered THEN the Test_Runner SHALL support selective test execution by module name
2. WHEN a test suite is slow THEN the Test_Framework SHALL support marking tests as "slow" for optional execution
3. WHEN tests complete THEN the Test_Runner SHALL report execution time for each test suite
4. WHEN tests fail THEN the Test_Framework SHALL support early termination option to stop on first failure
