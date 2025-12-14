# Requirements Document

## Introduction

本文档定义了 CastorOS ARM64 架构内核功能集成的需求规格。目标是将当前 ARM64 最小化内核扩展为功能完整的内核，集成物理内存管理、虚拟内存管理、任务调度、系统调用等核心功能。

当前 ARM64 实现状态：
- ✅ 引导代码和 HAL 框架
- ✅ 异常处理和 GIC 中断控制器
- ✅ MMU 页表操作（4级页表）
- ✅ 上下文切换汇编代码
- ✅ 系统调用入口（SVC）
- ✅ DTB 解析、串口和定时器驱动
- ❌ PMM/VMM 集成（当前使用 stub）
- ❌ 任务管理集成
- ❌ 系统调用分发集成
- ❌ 用户程序运行验证

## Glossary

- **PMM**: Physical Memory Manager，物理内存管理器
- **VMM**: Virtual Memory Manager，虚拟内存管理器
- **DTB**: Device Tree Blob，设备树二进制文件
- **GIC**: Generic Interrupt Controller，通用中断控制器
- **EL0/EL1**: Exception Level 0/1，ARM64 特权级别（用户态/内核态）
- **TTBR0/TTBR1**: Translation Table Base Register，页表基址寄存器
- **SVC**: Supervisor Call，ARM64 系统调用指令
- **ERET**: Exception Return，ARM64 异常返回指令
- **COW**: Copy-on-Write，写时复制机制

## Requirements

### Requirement 1: 物理内存管理集成

**User Story:** As a kernel developer, I want the ARM64 kernel to use the common PMM implementation, so that physical memory allocation works correctly on ARM64.

#### Acceptance Criteria

1. WHEN the ARM64 kernel boots THEN the PMM SHALL initialize using memory information from DTB
2. WHEN allocating physical frames THEN the PMM SHALL return valid physical addresses aligned to 4KB
3. WHEN freeing physical frames THEN the PMM SHALL correctly track frame reference counts
4. WHEN the PMM initializes THEN the system SHALL remove the stub PMM implementation from ARM64 build

### Requirement 2: 虚拟内存管理集成

**User Story:** As a kernel developer, I want the ARM64 kernel to use the common VMM implementation, so that virtual memory management works correctly on ARM64.

#### Acceptance Criteria

1. WHEN the VMM initializes THEN the system SHALL set up kernel address space using ARM64 4-level page tables
2. WHEN mapping virtual pages THEN the VMM SHALL use the HAL MMU interface for ARM64-specific page table operations
3. WHEN handling page faults THEN the VMM SHALL correctly interpret ARM64 fault information (ESR_EL1, FAR_EL1)
4. WHEN implementing COW THEN the VMM SHALL use ARM64 software-defined page table bits for COW tracking

### Requirement 3: 堆内存管理集成

**User Story:** As a kernel developer, I want the ARM64 kernel to have working heap allocation, so that kernel code can dynamically allocate memory.

#### Acceptance Criteria

1. WHEN the heap initializes THEN the system SHALL allocate a kernel heap region in the ARM64 address space
2. WHEN calling kmalloc THEN the heap manager SHALL return valid kernel virtual addresses
3. WHEN calling kfree THEN the heap manager SHALL correctly free allocated memory

### Requirement 4: 任务管理集成

**User Story:** As a kernel developer, I want the ARM64 kernel to support multitasking, so that multiple tasks can run concurrently.

#### Acceptance Criteria

1. WHEN creating a kernel task THEN the task manager SHALL initialize ARM64-specific context (X0-X30, SP, PC, PSTATE)
2. WHEN switching tasks THEN the scheduler SHALL save and restore ARM64 register state correctly
3. WHEN a task exits THEN the task manager SHALL clean up task resources and schedule another task
4. WHEN the timer interrupt fires THEN the scheduler SHALL perform preemptive task switching

### Requirement 5: 系统调用分发集成

**User Story:** As a kernel developer, I want the ARM64 kernel to handle system calls correctly, so that user programs can request kernel services.

#### Acceptance Criteria

1. WHEN a user program executes SVC THEN the kernel SHALL dispatch to the correct system call handler
2. WHEN passing system call arguments THEN the kernel SHALL read arguments from X0-X5 registers
3. WHEN returning from system calls THEN the kernel SHALL place the return value in X0 register
4. WHEN a system call fails THEN the kernel SHALL return negative errno values consistently

### Requirement 6: 用户模式支持

**User Story:** As a kernel developer, I want the ARM64 kernel to run user programs in EL0, so that user/kernel separation is enforced.

#### Acceptance Criteria

1. WHEN creating a user process THEN the kernel SHALL set up a separate address space with TTBR0
2. WHEN entering user mode THEN the kernel SHALL use ERET to transition to EL0
3. WHEN a user program causes an exception THEN the kernel SHALL handle it in EL1 and return to EL0
4. WHEN loading a user program THEN the kernel SHALL map code, data, and stack in user address space

### Requirement 7: 进程管理系统调用

**User Story:** As a kernel developer, I want fork and exec to work on ARM64, so that user programs can create new processes.

#### Acceptance Criteria

1. WHEN fork is called THEN the kernel SHALL create a child process with COW page sharing
2. WHEN exec is called THEN the kernel SHALL load a new program into the current process
3. WHEN exit is called THEN the kernel SHALL terminate the process and notify the parent
4. WHEN wait is called THEN the kernel SHALL block until a child process exits

### Requirement 8: 文件系统集成

**User Story:** As a kernel developer, I want the ARM64 kernel to support file operations, so that user programs can read and write files.

#### Acceptance Criteria

1. WHEN the VFS initializes THEN the system SHALL mount ramfs as the root filesystem
2. WHEN opening a file THEN the VFS SHALL return a valid file descriptor
3. WHEN reading or writing THEN the VFS SHALL transfer data correctly
4. WHEN closing a file THEN the VFS SHALL release the file descriptor

### Requirement 9: 调试支持

**User Story:** As a developer, I want to debug the ARM64 kernel using GDB, so that I can diagnose issues effectively.

#### Acceptance Criteria

1. WHEN running `make debug ARCH=arm64` THEN QEMU SHALL start with GDB server enabled
2. WHEN connecting GDB THEN the debugger SHALL load ARM64 symbols correctly
3. WHEN setting breakpoints THEN GDB SHALL stop at the specified locations

### Requirement 10: 构建系统完善

**User Story:** As a developer, I want the ARM64 build to include all necessary kernel modules, so that the kernel is fully functional.

#### Acceptance Criteria

1. WHEN building for ARM64 THEN the Makefile SHALL include PMM, VMM, heap, task, and syscall modules
2. WHEN building for ARM64 THEN the Makefile SHALL include VFS and ramfs modules
3. WHEN building for ARM64 THEN the Makefile SHALL exclude x86-specific drivers

