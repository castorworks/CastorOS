# Requirements Document: 内存管理子系统重构

## Introduction

本文档定义了 CastorOS 内存管理子系统的重构需求规格。目标是将当前以 i686 为中心的内存管理实现重构为真正的跨架构设计，支持 i686、x86_64 和 ARM64 三种架构。

### 背景

当前 CastorOS 的内存管理存在以下问题：
1. **PMM 使用 32-bit 类型** - `pmm_alloc_frame()` 返回 `uint32_t`，限制了 64 位架构的物理地址空间
2. **VMM 架构耦合严重** - `vmm.c` 中大量 `#if defined(ARCH_X86_64)` 条件编译，x86_64 的动态操作是 stub
3. **缺少统一的页表抽象** - 每个架构的页表操作分散在不同文件中，没有统一接口
4. **COW 实现仅支持 i686** - x86_64 和 ARM64 的 COW 机制未实现

### 业界参考

| 操作系统 | 物理地址类型 | 页表抽象 | 特点 |
|---------|-------------|---------|------|
| Linux | `phys_addr_t` (64-bit) | `pgd_t/pud_t/pmd_t/pte_t` | 多级页表宏抽象，支持大页 |
| FreeBSD | `vm_paddr_t` (64-bit) | `pmap` 模块 | 机器无关/机器相关分离 |
| Xv6 | `uint64` | 简单 3 级页表 | 教学用，简洁清晰 |
| seL4 | `paddr_t` | Capability-based | 形式化验证 |

### 设计目标

1. **类型安全** - 使用专用类型区分物理地址和虚拟地址
2. **架构解耦** - 通用代码与架构特定代码完全分离
3. **接口统一** - 所有架构使用相同的 API
4. **渐进迁移** - 保持向后兼容，逐步重构

## Glossary

- **PMM**: Physical Memory Manager，物理内存管理器
- **VMM**: Virtual Memory Manager，虚拟内存管理器
- **paddr_t**: Physical Address Type，物理地址类型
- **vaddr_t**: Virtual Address Type，虚拟地址类型
- **PFN**: Page Frame Number，页帧号
- **PTE**: Page Table Entry，页表项
- **PDE**: Page Directory Entry，页目录项
- **PML4**: Page Map Level 4，x86_64 四级页表的最高级
- **TTBR**: Translation Table Base Register，ARM64 页表基址寄存器
- **COW**: Copy-on-Write，写时复制
- **TLB**: Translation Lookaside Buffer，页表缓存
- **HAL**: Hardware Abstraction Layer，硬件抽象层

## Requirements

### Requirement 1: 物理地址类型系统

**User Story:** As a kernel developer, I want type-safe physical and virtual address types, so that I can avoid mixing up address spaces and catch errors at compile time.

#### Acceptance Criteria

1. WHEN defining address types THEN the CastorOS SHALL provide `paddr_t` for physical addresses as a 64-bit unsigned integer on all architectures
2. WHEN defining address types THEN the CastorOS SHALL provide `vaddr_t` for virtual addresses matching the architecture's pointer size
3. WHEN converting between address types THEN the CastorOS SHALL require explicit conversion macros (`PADDR_TO_VADDR`, `VADDR_TO_PADDR`)
4. WHEN defining page frame numbers THEN the CastorOS SHALL provide `pfn_t` as a 64-bit type representing physical page indices
5. WHEN performing address arithmetic THEN the CastorOS SHALL provide type-safe macros for page alignment and PFN conversion

### Requirement 2: PMM 64-bit 物理地址支持

**User Story:** As a kernel developer, I want the PMM to support 64-bit physical addresses, so that I can utilize more than 4GB of physical memory on 64-bit architectures.

#### Acceptance Criteria

1. WHEN allocating physical frames THEN the PMM SHALL return `paddr_t` type instead of `uint32_t`
2. WHEN tracking frame allocation THEN the PMM SHALL use a bitmap that can address up to 2^52 bytes of physical memory (x86_64 limit)
3. WHEN managing reference counts THEN the PMM SHALL support frames beyond the 4GB boundary
4. WHEN initializing on i686 THEN the PMM SHALL limit physical memory to 4GB and use 32-bit optimized paths
5. WHEN initializing on x86_64 or ARM64 THEN the PMM SHALL support the full physical address space of the architecture

### Requirement 3: 统一页表抽象层

**User Story:** As a kernel developer, I want a unified page table abstraction, so that I can write architecture-independent VMM code.

#### Acceptance Criteria

1. WHEN defining page table types THEN the CastorOS SHALL provide generic `pte_t` and `pde_t` types that adapt to architecture-specific sizes
2. WHEN traversing page tables THEN the CastorOS SHALL provide architecture-independent walker functions
3. WHEN creating page table entries THEN the CastorOS SHALL provide macros to construct entries with portable flags
4. WHEN querying page table entries THEN the CastorOS SHALL provide macros to extract physical address and flags
5. WHEN handling different page table depths THEN the CastorOS SHALL abstract 2-level (i686), 4-level (x86_64), and 4-level (ARM64) structures

### Requirement 4: VMM 架构解耦

**User Story:** As a kernel developer, I want the VMM to be cleanly separated into architecture-independent and architecture-specific parts, so that adding new architectures requires minimal changes to core code.

#### Acceptance Criteria

1. WHEN mapping virtual pages THEN the VMM SHALL call architecture-specific page table manipulation through HAL interfaces
2. WHEN creating address spaces THEN the VMM SHALL use architecture-specific page table creation functions
3. WHEN handling page faults THEN the VMM SHALL receive architecture-independent fault information from HAL
4. WHEN implementing COW THEN the VMM SHALL use architecture-independent logic with architecture-specific PTE flag manipulation
5. WHEN switching address spaces THEN the VMM SHALL call HAL functions that update architecture-specific registers (CR3/TTBR)

### Requirement 5: x86_64 完整 VMM 实现

**User Story:** As a kernel developer, I want full VMM functionality on x86_64, so that I can run user processes with proper memory isolation.

#### Acceptance Criteria

1. WHEN mapping pages on x86_64 THEN the VMM SHALL correctly manipulate 4-level page tables (PML4→PDPT→PD→PT)
2. WHEN creating new address spaces on x86_64 THEN the VMM SHALL allocate and initialize PML4 with kernel mappings
3. WHEN cloning address spaces on x86_64 THEN the VMM SHALL implement COW by sharing physical pages and setting read-only flags
4. WHEN handling page faults on x86_64 THEN the VMM SHALL correctly interpret CR2 and error code for COW handling
5. WHEN freeing address spaces on x86_64 THEN the VMM SHALL properly deallocate all page table levels

### Requirement 6: ARM64 MMU 实现

**User Story:** As a kernel developer, I want ARM64 MMU support, so that CastorOS can run on ARM-based systems.

#### Acceptance Criteria

1. WHEN initializing MMU on ARM64 THEN the CastorOS SHALL configure TCR_EL1 for 4KB granule and 48-bit virtual addresses
2. WHEN mapping pages on ARM64 THEN the VMM SHALL correctly manipulate 4-level translation tables
3. WHEN setting page attributes on ARM64 THEN the VMM SHALL use MAIR_EL1 indices for memory type configuration
4. WHEN handling page faults on ARM64 THEN the VMM SHALL interpret FAR_EL1 and ESR_EL1 for fault information
5. WHEN switching address spaces on ARM64 THEN the VMM SHALL update TTBR0_EL1 for user space and issue appropriate barriers

### Requirement 7: 内核/用户空间分离

**User Story:** As a kernel developer, I want clear separation between kernel and user address spaces, so that user processes cannot access kernel memory.

#### Acceptance Criteria

1. WHEN defining address space layout THEN the CastorOS SHALL use higher-half kernel design on all architectures
2. WHEN creating user address spaces THEN the VMM SHALL share kernel page tables across all processes
3. WHEN mapping user pages THEN the VMM SHALL set appropriate user-accessible flags
4. WHEN handling kernel page faults THEN the VMM SHALL synchronize kernel mappings across all address spaces
5. WHEN validating user pointers THEN the kernel SHALL verify addresses are within user space range

### Requirement 8: 大页支持（可选）

**User Story:** As a kernel developer, I want optional huge page support, so that I can reduce TLB pressure for large memory regions.

#### Acceptance Criteria

1. WHEN allocating large contiguous regions THEN the PMM SHALL optionally return 2MB-aligned frames
2. WHEN mapping kernel identity regions THEN the VMM SHALL optionally use 2MB pages on x86_64 or 2MB blocks on ARM64
3. WHEN querying page size THEN the VMM SHALL report whether a mapping uses standard or huge pages
4. WHEN the system lacks huge page support THEN the VMM SHALL fall back to 4KB pages transparently

### Requirement 9: 内存映射 I/O (MMIO)

**User Story:** As a driver developer, I want to map device memory into kernel address space, so that I can access hardware registers.

#### Acceptance Criteria

1. WHEN mapping MMIO regions THEN the VMM SHALL set appropriate cache-disable flags for the architecture
2. WHEN mapping framebuffers THEN the VMM SHALL optionally use write-combining memory type
3. WHEN unmapping MMIO regions THEN the VMM SHALL properly invalidate TLB entries
4. WHEN allocating MMIO virtual addresses THEN the VMM SHALL use a dedicated kernel address range

### Requirement 10: DMA 内存管理

**User Story:** As a driver developer, I want to allocate DMA-capable memory, so that devices can directly access memory buffers.

#### Acceptance Criteria

1. WHEN allocating DMA buffers THEN the PMM SHALL return physically contiguous memory
2. WHEN preparing DMA buffers on ARM64 THEN the VMM SHALL perform cache maintenance operations
3. WHEN translating DMA addresses THEN the VMM SHALL provide physical addresses suitable for device programming
4. WHEN freeing DMA buffers THEN the PMM SHALL properly release the contiguous region

### Requirement 11: 调试和诊断

**User Story:** As a kernel developer, I want memory management debugging tools, so that I can diagnose memory-related issues.

#### Acceptance Criteria

1. WHEN dumping page tables THEN the VMM SHALL provide human-readable output of mappings
2. WHEN detecting memory corruption THEN the PMM SHALL validate bitmap and refcount consistency
3. WHEN tracking memory usage THEN the PMM SHALL provide per-zone statistics
4. WHEN a page fault occurs THEN the VMM SHALL log detailed fault information including address and access type

