# Requirements Document

## Introduction

本文档定义了 CastorOS 操作系统多架构兼容性支持的需求规格。目标是将当前仅支持 i686 (x86 32位) 的 CastorOS 扩展为同时支持 x86_64 (64位) 和 ARM64 (AArch64) 架构。

CastorOS 是一个教学和实验性质的操作系统，当前实现包括：
- 高半核设计 (Higher-Half Kernel)
- 分页内存管理 (PMM/VMM)
- 抢占式多任务调度
- Copy-on-Write (COW) 机制
- VFS 文件系统抽象
- POSIX 兼容系统调用
- 多种设备驱动 (ATA, E1000, USB UHCI, ACPI 等)

## Glossary

- **CastorOS**: 本项目的操作系统名称
- **i686**: Intel 32位 x86 架构 (当前支持)
- **x86_64**: AMD64/Intel 64位架构 (目标架构之一)
- **ARM64/AArch64**: ARM 64位架构 (目标架构之一)
- **HAL**: Hardware Abstraction Layer，硬件抽象层
- **PMM**: Physical Memory Manager，物理内存管理器
- **VMM**: Virtual Memory Manager，虚拟内存管理器
- **GDT**: Global Descriptor Table，全局描述符表 (x86 特有)
- **IDT**: Interrupt Descriptor Table，中断描述符表 (x86 特有)
- **TSS**: Task State Segment，任务状态段 (x86 特有)
- **GIC**: Generic Interrupt Controller，通用中断控制器 (ARM 特有)
- **UEFI**: Unified Extensible Firmware Interface，统一可扩展固件接口
- **DTB**: Device Tree Blob，设备树二进制文件 (ARM 特有)
- **MMIO**: Memory-Mapped I/O，内存映射输入输出
- **PIC**: Programmable Interrupt Controller，可编程中断控制器 (x86)
- **APIC**: Advanced PIC，高级可编程中断控制器 (x86_64)
- **Multiboot**: GRUB 引导规范 (x86)
- **Cross-Compiler**: 交叉编译器，在一种架构上编译另一种架构的代码

## Requirements

### Requirement 1: 架构抽象层 (HAL)

**User Story:** As a kernel developer, I want a hardware abstraction layer that isolates architecture-specific code, so that I can add new architectures without modifying core kernel logic.

#### Acceptance Criteria

1. WHEN the kernel initializes THEN the CastorOS SHALL load architecture-specific initialization routines through a unified HAL interface
2. WHEN architecture-specific code is needed THEN the CastorOS SHALL provide separate implementation files under `src/arch/{arch_name}/` directories
3. WHEN common kernel code requires hardware operations THEN the CastorOS SHALL use HAL function pointers or inline wrappers that dispatch to architecture-specific implementations
4. WHEN a new architecture is added THEN the CastorOS SHALL require only implementing the HAL interface without modifying existing architecture code

### Requirement 2: 构建系统多架构支持

**User Story:** As a developer, I want to build CastorOS for different target architectures using a single build system, so that I can easily switch between architectures during development.

#### Acceptance Criteria

1. WHEN a developer specifies `ARCH=x86_64` THEN the build system SHALL select the x86_64 cross-compiler and architecture-specific source files
2. WHEN a developer specifies `ARCH=arm64` THEN the build system SHALL select the ARM64 cross-compiler and architecture-specific source files
3. WHEN no ARCH is specified THEN the build system SHALL default to `i686` for backward compatibility
4. WHEN building for a specific architecture THEN the build system SHALL generate architecture-appropriate linker scripts and binary formats
5. WHEN running `make run` THEN the build system SHALL invoke the appropriate QEMU emulator for the target architecture

### Requirement 3: x86_64 引导支持

**User Story:** As a system administrator, I want CastorOS to boot on x86_64 hardware, so that I can utilize modern 64-bit processors.

#### Acceptance Criteria

1. WHEN booting on x86_64 hardware THEN the CastorOS SHALL transition from real mode through protected mode to long mode (64-bit mode)
2. WHEN entering long mode THEN the CastorOS SHALL set up 4-level page tables (PML4, PDPT, PD, PT)
3. WHEN initializing the CPU THEN the CastorOS SHALL configure the 64-bit GDT with appropriate code and data segments
4. WHEN handling interrupts THEN the CastorOS SHALL use the 64-bit IDT format with IST (Interrupt Stack Table) support
5. WHEN the kernel loads THEN the CastorOS SHALL support both Multiboot2 and UEFI boot protocols

### Requirement 4: ARM64 引导支持

**User Story:** As a system administrator, I want CastorOS to boot on ARM64 hardware, so that I can run the OS on ARM-based systems like Raspberry Pi or cloud instances.

#### Acceptance Criteria

1. WHEN booting on ARM64 hardware THEN the CastorOS SHALL initialize from the UEFI or U-Boot entry point in EL1 (Exception Level 1)
2. WHEN initializing the MMU THEN the CastorOS SHALL configure ARM64 translation tables (TTBR0/TTBR1) for user/kernel space separation
3. WHEN parsing hardware configuration THEN the CastorOS SHALL read device information from Device Tree Blob (DTB) or ACPI tables
4. WHEN handling interrupts THEN the CastorOS SHALL configure the GIC (Generic Interrupt Controller) for interrupt routing
5. WHEN the kernel loads THEN the CastorOS SHALL establish exception vectors for synchronous exceptions, IRQs, FIQs, and SErrors

### Requirement 5: 内存管理架构适配

**User Story:** As a kernel developer, I want the memory management subsystem to work correctly on all supported architectures, so that processes can allocate and use memory consistently.

#### Acceptance Criteria

1. WHEN managing physical memory THEN the PMM SHALL use architecture-appropriate page sizes (4KB standard, with optional 2MB/1GB huge pages on x86_64, 4KB/16KB/64KB on ARM64)
2. WHEN creating page tables THEN the VMM SHALL generate architecture-specific page table formats (2-level for i686, 4-level for x86_64, 4-level for ARM64)
3. WHEN mapping kernel space THEN the VMM SHALL use the higher-half design with architecture-appropriate virtual address ranges
4. WHEN handling page faults THEN the VMM SHALL interpret architecture-specific fault codes and registers (CR2 on x86, FAR_EL1 on ARM64)
5. WHEN implementing COW THEN the VMM SHALL use architecture-specific page table entry flags for read-only and dirty tracking

### Requirement 6: 中断和异常处理

**User Story:** As a kernel developer, I want interrupt handling to work correctly on all architectures, so that the kernel can respond to hardware events and exceptions.

#### Acceptance Criteria

1. WHEN an interrupt occurs on x86_64 THEN the CastorOS SHALL save the 64-bit register state and dispatch through the IDT
2. WHEN an interrupt occurs on ARM64 THEN the CastorOS SHALL save the ARM64 register state and dispatch through the exception vector table
3. WHEN configuring interrupt controllers THEN the CastorOS SHALL initialize PIC/APIC on x86 or GIC on ARM64
4. WHEN registering interrupt handlers THEN the CastorOS SHALL provide a unified API that abstracts architecture-specific interrupt numbers
5. WHEN returning from interrupts THEN the CastorOS SHALL use architecture-appropriate return instructions (IRETQ on x86_64, ERET on ARM64)

### Requirement 7: 上下文切换和任务管理

**User Story:** As a kernel developer, I want task switching to work correctly on all architectures, so that multitasking operates consistently.

#### Acceptance Criteria

1. WHEN saving task context on x86_64 THEN the CastorOS SHALL preserve all 64-bit general-purpose registers (RAX-R15), segment registers, and control registers
2. WHEN saving task context on ARM64 THEN the CastorOS SHALL preserve general-purpose registers (X0-X30), SP, PC, PSTATE, and floating-point registers
3. WHEN switching address spaces THEN the CastorOS SHALL update CR3 on x86 or TTBR0_EL1 on ARM64
4. WHEN entering user mode THEN the CastorOS SHALL use architecture-specific privilege transition mechanisms (IRET/SYSRET on x86, ERET on ARM64)
5. WHEN handling system calls THEN the CastorOS SHALL support architecture-specific system call conventions (SYSCALL/SYSENTER on x86_64, SVC on ARM64)

### Requirement 8: 系统调用接口

**User Story:** As an application developer, I want system calls to work consistently across architectures, so that user programs are portable.

#### Acceptance Criteria

1. WHEN a user program invokes a system call THEN the CastorOS SHALL use architecture-specific entry mechanisms while maintaining consistent system call numbers
2. WHEN passing system call arguments THEN the CastorOS SHALL follow architecture-specific ABI conventions (registers on x86_64: RDI, RSI, RDX, R10, R8, R9; ARM64: X0-X5)
3. WHEN returning from system calls THEN the CastorOS SHALL place the return value in the architecture-appropriate register (RAX on x86_64, X0 on ARM64)
4. WHEN handling system call errors THEN the CastorOS SHALL return negative errno values consistently across architectures

### Requirement 9: 设备驱动架构适配

**User Story:** As a driver developer, I want to write drivers that can work on multiple architectures where applicable, so that I can maximize code reuse.

#### Acceptance Criteria

1. WHEN accessing device registers THEN the CastorOS SHALL provide architecture-independent MMIO accessor functions with appropriate memory barriers
2. WHEN using port I/O THEN the CastorOS SHALL provide x86-specific port I/O functions that are conditionally compiled only for x86 architectures
3. WHEN discovering devices THEN the CastorOS SHALL support PCI enumeration on x86 and Device Tree parsing on ARM64
4. WHEN handling DMA THEN the CastorOS SHALL provide architecture-appropriate cache coherency operations
5. WHEN implementing platform-specific drivers THEN the CastorOS SHALL place them in architecture-specific directories

### Requirement 10: 用户空间库适配

**User Story:** As an application developer, I want the user-space library to work correctly on all architectures, so that I can write portable applications.

#### Acceptance Criteria

1. WHEN compiling user programs THEN the build system SHALL use the appropriate cross-compiler for the target architecture
2. WHEN making system calls from user space THEN the user library SHALL use architecture-specific inline assembly for the system call instruction
3. WHEN defining data types THEN the user library SHALL use architecture-appropriate sizes (e.g., 64-bit pointers on x86_64/ARM64)
4. WHEN linking user programs THEN the build system SHALL use architecture-appropriate linker scripts and entry points

### Requirement 11: 调试和测试支持

**User Story:** As a developer, I want to debug and test CastorOS on all supported architectures, so that I can ensure correctness across platforms.

#### Acceptance Criteria

1. WHEN debugging on x86_64 THEN the CastorOS SHALL support GDB remote debugging through QEMU
2. WHEN debugging on ARM64 THEN the CastorOS SHALL support GDB remote debugging through QEMU with ARM64 target
3. WHEN running tests THEN the test framework SHALL execute on all supported architectures
4. WHEN a test fails THEN the test framework SHALL report architecture-specific diagnostic information

### Requirement 12: 代码组织和目录结构

**User Story:** As a contributor, I want a clear code organization that separates architecture-specific code from common code, so that I can easily navigate and modify the codebase.

#### Acceptance Criteria

1. WHEN organizing source files THEN the CastorOS SHALL place architecture-specific code under `src/arch/{arch_name}/` directories
2. WHEN organizing header files THEN the CastorOS SHALL place architecture-specific headers under `src/include/arch/{arch_name}/` directories
3. WHEN sharing common code THEN the CastorOS SHALL keep architecture-independent code in existing directories (kernel, mm, fs, drivers, lib)
4. WHEN selecting architecture-specific implementations THEN the build system SHALL use conditional compilation or file selection based on the target architecture
