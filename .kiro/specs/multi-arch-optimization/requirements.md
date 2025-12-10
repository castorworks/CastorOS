# Requirements Document

## Introduction

本文档定义了 CastorOS 多架构支持优化的需求规格。基于对现有代码库的分析，结合业界最佳实践（如 Linux、FreeBSD、Zephyr RTOS 的架构抽象设计），提出系统性的改进方案。

目标是提升 CastorOS 多架构支持的：
- **可维护性**：减少条件编译，统一代码路径
- **可扩展性**：简化新架构的添加流程
- **正确性**：通过更清晰的抽象减少架构相关 bug
- **一致性**：确保所有架构行为一致

## Glossary

- **HAL**: Hardware Abstraction Layer，硬件抽象层
- **ABI**: Application Binary Interface，应用程序二进制接口
- **PTE**: Page Table Entry，页表项
- **DTB**: Device Tree Blob，设备树二进制文件
- **PCI**: Peripheral Component Interconnect，外设组件互连
- **MMIO**: Memory-Mapped I/O，内存映射输入输出
- **IRQ**: Interrupt Request，中断请求
- **COW**: Copy-on-Write，写时复制
- **TLB**: Translation Lookaside Buffer，转换后备缓冲区
- **Platform Driver**: 平台驱动，与特定硬件平台绑定的驱动程序
- **Capability Query**: 能力查询，运行时查询硬件/软件支持的功能

## Requirements

### Requirement 1: HAL 能力查询接口

**User Story:** As a kernel developer, I want to query HAL capabilities at runtime, so that I can write portable code that adapts to different architecture features.

#### Acceptance Criteria

1. WHEN the kernel queries HAL capabilities THEN the HAL SHALL return a structure containing supported features (huge pages, NX bit, port I/O, cache coherent DMA)
2. WHEN a feature is not supported on the current architecture THEN the HAL capability query SHALL return false for that feature
3. WHEN the kernel needs architecture-specific constants THEN the HAL SHALL provide page table levels, page sizes, and address space limits through the capability structure
4. WHEN a new architecture is added THEN the HAL capability query SHALL require only implementing the `hal_get_capabilities()` function

### Requirement 2: HAL 接口完整性

**User Story:** As a kernel developer, I want all HAL interfaces to be fully implemented on all architectures, so that I can rely on consistent behavior across platforms.

#### Acceptance Criteria

1. WHEN `hal_arch_name()` is called on any architecture THEN the HAL SHALL return the correct architecture name string
2. WHEN `hal_context_size()` is called THEN the HAL SHALL return the correct size of the architecture-specific context structure
3. WHEN `hal_context_init()` is called THEN the HAL SHALL properly initialize all registers for the target privilege level
4. WHEN any HAL function is not implementable on an architecture THEN the HAL SHALL provide a stub that returns an appropriate error or no-op behavior with documentation

### Requirement 3: 页表抽象层

**User Story:** As a kernel developer, I want a unified page table abstraction, so that VMM code does not need architecture-specific conditionals.

#### Acceptance Criteria

1. WHEN creating a page table entry THEN the pgtable abstraction SHALL convert architecture-independent flags to architecture-specific format
2. WHEN querying a page table entry THEN the pgtable abstraction SHALL extract physical address and flags in architecture-independent format
3. WHEN walking page tables THEN the pgtable abstraction SHALL handle different page table levels (2 for i686, 4 for x86_64/ARM64) transparently
4. WHEN the VMM performs page table operations THEN the VMM SHALL use only pgtable abstraction functions without direct page table manipulation

### Requirement 4: VMM 代码统一

**User Story:** As a kernel developer, I want VMM code to be architecture-independent, so that memory management logic is easier to maintain and test.

#### Acceptance Criteria

1. WHEN handling COW page faults THEN the VMM SHALL use a single code path that delegates to HAL for architecture-specific operations
2. WHEN creating or cloning address spaces THEN the VMM SHALL use HAL interfaces exclusively without architecture-specific conditionals
3. WHEN the VMM needs to know kernel/user space boundaries THEN the VMM SHALL query these from HAL or architecture-specific headers
4. WHEN page table operations fail THEN the VMM SHALL receive consistent error codes from HAL across all architectures

### Requirement 5: 逻辑中断号抽象

**User Story:** As a driver developer, I want to use logical interrupt numbers, so that my driver code works across different interrupt controller implementations.

#### Acceptance Criteria

1. WHEN a driver requests an interrupt for a device type THEN the HAL SHALL map the logical IRQ type to the physical IRQ number
2. WHEN registering a timer interrupt handler THEN the driver SHALL use `HAL_IRQ_TIMER` instead of architecture-specific IRQ numbers
3. WHEN the interrupt controller differs between architectures THEN the HAL SHALL hide these differences behind the logical IRQ interface
4. WHEN a logical IRQ type is not available on an architecture THEN the HAL SHALL return an error code indicating unavailability

### Requirement 6: 平台设备模型

**User Story:** As a driver developer, I want a platform device abstraction, so that I can write drivers that work with both PCI enumeration and Device Tree discovery.

#### Acceptance Criteria

1. WHEN a platform driver is registered THEN the driver framework SHALL match it against discovered devices using PCI IDs or DTB compatible strings
2. WHEN a device is discovered via PCI THEN the platform framework SHALL create a platform_device structure with MMIO regions and IRQ information
3. WHEN a device is discovered via Device Tree THEN the platform framework SHALL create a platform_device structure with the same interface as PCI-discovered devices
4. WHEN a driver needs device resources THEN the driver SHALL access them through platform_device methods without knowing the discovery mechanism

### Requirement 7: 系统调用 ABI 统一

**User Story:** As a kernel developer, I want a unified system call interface, so that syscall handlers do not need architecture-specific argument extraction.

#### Acceptance Criteria

1. WHEN a system call is invoked THEN the HAL SHALL extract arguments into a unified `hal_syscall_args_t` structure
2. WHEN the syscall handler returns THEN the HAL SHALL place the return value in the architecture-appropriate register
3. WHEN passing system call arguments THEN the HAL SHALL handle ABI differences (register conventions, stack usage) transparently
4. WHEN a system call has more than 6 arguments THEN the HAL SHALL provide a mechanism to access additional arguments consistently

### Requirement 8: 启动信息标准化

**User Story:** As a kernel developer, I want a standardized boot information structure, so that kernel initialization code is architecture-independent.

#### Acceptance Criteria

1. WHEN the bootloader passes control to the kernel THEN the architecture-specific boot code SHALL populate a `boot_info_t` structure
2. WHEN the kernel needs memory map information THEN the kernel SHALL read it from `boot_info_t` without knowing the bootloader protocol
3. WHEN the kernel needs command line arguments THEN the kernel SHALL access them through `boot_info_t.cmdline`
4. WHEN framebuffer information is available THEN the boot code SHALL include it in `boot_info_t` in a standardized format

### Requirement 9: 架构特定测试

**User Story:** As a developer, I want architecture-specific tests, so that I can verify HAL implementations are correct on each platform.

#### Acceptance Criteria

1. WHEN running tests on a specific architecture THEN the test framework SHALL execute HAL interface tests for that architecture
2. WHEN a HAL function has architecture-specific behavior THEN the test suite SHALL include tests that verify the expected behavior
3. WHEN memory management is tested THEN the test suite SHALL verify page table operations produce correct mappings
4. WHEN tests fail THEN the test framework SHALL report architecture-specific diagnostic information

### Requirement 10: 条件编译最小化

**User Story:** As a contributor, I want minimal conditional compilation in common code, so that the codebase is easier to understand and modify.

#### Acceptance Criteria

1. WHEN architecture-specific behavior is needed in common code THEN the code SHALL call HAL functions instead of using `#ifdef`
2. WHEN architecture-specific constants are needed THEN the code SHALL use HAL capability queries or architecture header macros
3. WHEN a file contains more than 3 architecture conditionals THEN the code SHALL be refactored to use HAL abstractions
4. WHEN reviewing code changes THEN the review process SHALL flag new architecture conditionals in common code for discussion

### Requirement 11: 文档和注释规范

**User Story:** As a contributor, I want clear documentation for architecture abstractions, so that I can correctly implement support for new architectures.

#### Acceptance Criteria

1. WHEN a HAL function is defined THEN the header SHALL include documentation describing expected behavior on all architectures
2. WHEN architecture-specific behavior differs THEN the documentation SHALL explicitly list the differences
3. WHEN adding a new architecture THEN the documentation SHALL provide a checklist of required HAL implementations
4. WHEN a HAL function has performance implications THEN the documentation SHALL note any architecture-specific considerations

### Requirement 12: 错误处理一致性

**User Story:** As a kernel developer, I want consistent error handling across architectures, so that error recovery code works reliably.

#### Acceptance Criteria

1. WHEN a HAL function fails THEN the function SHALL return a consistent error code defined in a common header
2. WHEN an architecture does not support an operation THEN the HAL SHALL return `HAL_ERR_NOT_SUPPORTED` instead of silently failing
3. WHEN a resource allocation fails THEN the HAL SHALL return `HAL_ERR_NO_MEMORY` consistently across architectures
4. WHEN an invalid parameter is passed THEN the HAL SHALL return `HAL_ERR_INVALID_PARAM` with optional debug logging

