# Design Document: CastorOS Multi-Architecture Support

## Overview

本设计文档描述了 CastorOS 操作系统多架构兼容性支持的技术设计方案。目标是将当前仅支持 i686 的 CastorOS 扩展为同时支持 x86_64 和 ARM64 架构，同时保持代码的可维护性和可扩展性。

### 设计原则

1. **最小化架构特定代码**: 通过 HAL 抽象层隔离架构差异
2. **保持向后兼容**: 现有 i686 功能不受影响
3. **渐进式实现**: 优先实现核心功能，逐步完善
4. **代码复用最大化**: 共享尽可能多的通用代码

### 架构差异概览

| 特性 | i686 | x86_64 | ARM64 |
|------|------|--------|-------|
| 寄存器宽度 | 32-bit | 64-bit | 64-bit |
| 页表级数 | 2级 | 4级 | 4级 |
| 内核虚拟基址 | 0x80000000 | 0xFFFF800000000000 | 0xFFFF000000000000 |
| 中断控制器 | PIC/APIC | APIC | GIC |
| 系统调用 | INT 0x80 | SYSCALL | SVC |
| 引导协议 | Multiboot | Multiboot2/UEFI | UEFI/U-Boot |


## Architecture

### 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           User Space Applications                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                        User Library (libc-like)                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                          │
│  │   i686      │  │   x86_64    │  │   arm64     │  ← Architecture-specific │
│  │  syscall    │  │  syscall    │  │  syscall    │    system call wrappers  │
│  └─────────────┘  └─────────────┘  └─────────────┘                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                              Kernel Space                                    │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                    Common Kernel Code                                  │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐         │  │
│  │  │  Task   │ │   VFS   │ │   Net   │ │ Syscall │ │  Sync   │         │  │
│  │  │ Manager │ │  Layer  │ │  Stack  │ │ Handler │ │ Prims   │         │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘         │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                Hardware Abstraction Layer (HAL)                        │  │
│  │  ┌─────────────────────────────────────────────────────────────────┐  │  │
│  │  │  hal_cpu_init()  hal_interrupt_init()  hal_mmu_init()           │  │  │
│  │  │  hal_context_switch()  hal_syscall_entry()  hal_timer_init()    │  │  │
│  │  └─────────────────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                          │
│  │   i686      │  │   x86_64    │  │   arm64     │  ← Architecture-specific │
│  │   arch      │  │   arch      │  │   arch      │    implementations       │
│  └─────────────┘  └─────────────┘  └─────────────┘                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                              Hardware                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 目录结构设计

```
src/
├── arch/                          # 架构特定代码
│   ├── i686/                      # 现有 x86 32位代码迁移
│   │   ├── boot/                  # boot.asm, multiboot.asm
│   │   ├── cpu/                   # gdt.c, idt.c, tss.c
│   │   ├── interrupt/             # isr.c, irq.c, pic.c
│   │   ├── mm/                    # paging.c (2级页表)
│   │   ├── task/                  # context_switch.asm
│   │   ├── syscall/               # syscall_entry.asm
│   │   └── include/               # 架构特定头文件
│   ├── x86_64/                    # x86 64位实现
│   │   ├── boot/                  # boot64.asm, multiboot2.asm
│   │   ├── cpu/                   # gdt64.c, idt64.c, tss64.c
│   │   ├── interrupt/             # isr64.c, irq64.c, apic.c
│   │   ├── mm/                    # paging64.c (4级页表)
│   │   ├── task/                  # context_switch64.asm
│   │   ├── syscall/               # syscall_entry64.asm
│   │   └── include/
│   └── arm64/                     # ARM64 实现
│       ├── boot/                  # start.S, uefi_entry.c
│       ├── cpu/                   # exception_level.c
│       ├── interrupt/             # gic.c, vectors.S
│       ├── mm/                    # mmu.c (ARM64 页表)
│       ├── task/                  # context_switch.S
│       ├── syscall/               # svc_entry.S
│       └── include/
├── kernel/                        # 架构无关内核代码
├── mm/                            # 架构无关内存管理
├── fs/                            # 文件系统
├── drivers/                       # 设备驱动
│   ├── common/                    # 通用驱动
│   ├── x86/                       # x86 特定驱动 (PCI, ATA)
│   └── arm/                       # ARM 特定驱动
├── net/                           # 网络栈
├── lib/                           # 内核库
└── include/
    ├── arch/                      # 架构特定头文件
    │   ├── i686/
    │   ├── x86_64/
    │   └── arm64/
    └── hal/                       # HAL 接口定义
```


## Components and Interfaces

### HAL (Hardware Abstraction Layer) 接口

```c
// src/include/hal/hal.h

#ifndef _HAL_H_
#define _HAL_H_

#include <types.h>

/* ============================================================================
 * CPU 初始化
 * ========================================================================== */

/**
 * @brief 初始化 CPU 架构特定功能
 * - i686: GDT, TSS
 * - x86_64: GDT64, TSS64
 * - ARM64: Exception Level 配置
 */
void hal_cpu_init(void);

/**
 * @brief 获取当前 CPU ID (多核支持预留)
 */
uint32_t hal_cpu_id(void);

/* ============================================================================
 * 中断管理
 * ========================================================================== */

/**
 * @brief 初始化中断系统
 * - i686/x86_64: IDT, PIC/APIC
 * - ARM64: Exception vectors, GIC
 */
void hal_interrupt_init(void);

/**
 * @brief 注册中断处理函数
 * @param irq 架构无关的 IRQ 编号
 * @param handler 处理函数
 */
void hal_interrupt_register(uint32_t irq, void (*handler)(void *));

/**
 * @brief 启用/禁用中断
 */
void hal_interrupt_enable(void);
void hal_interrupt_disable(void);

/**
 * @brief 保存并禁用中断，返回之前的状态
 */
uint64_t hal_interrupt_save(void);
void hal_interrupt_restore(uint64_t state);

/* ============================================================================
 * 内存管理
 * ========================================================================== */

/**
 * @brief 初始化 MMU/分页
 */
void hal_mmu_init(void);

/**
 * @brief 创建页表映射
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 */
bool hal_mmu_map(uintptr_t virt, uintptr_t phys, uint32_t flags);

/**
 * @brief 取消页表映射
 */
void hal_mmu_unmap(uintptr_t virt);

/**
 * @brief 刷新 TLB
 */
void hal_mmu_flush_tlb(uintptr_t virt);

/**
 * @brief 切换地址空间
 */
void hal_mmu_switch_space(uintptr_t page_table_phys);

/**
 * @brief 获取页错误地址
 */
uintptr_t hal_mmu_get_fault_addr(void);

/* ============================================================================
 * 上下文切换
 * ========================================================================== */

/**
 * @brief 架构特定的 CPU 上下文
 */
typedef struct hal_context hal_context_t;

/**
 * @brief 初始化任务上下文
 */
void hal_context_init(hal_context_t *ctx, uintptr_t entry, 
                      uintptr_t stack, bool is_user);

/**
 * @brief 执行上下文切换
 */
void hal_context_switch(hal_context_t **old_ctx, hal_context_t *new_ctx);

/* ============================================================================
 * 系统调用
 * ========================================================================== */

/**
 * @brief 初始化系统调用入口
 */
void hal_syscall_init(void);

/* ============================================================================
 * 定时器
 * ========================================================================== */

/**
 * @brief 初始化系统定时器
 * @param freq_hz 定时器频率 (Hz)
 */
void hal_timer_init(uint32_t freq_hz);

/**
 * @brief 获取系统滴答数
 */
uint64_t hal_timer_get_ticks(void);

/* ============================================================================
 * I/O 操作
 * ========================================================================== */

/**
 * @brief MMIO 读写 (所有架构通用)
 */
static inline uint8_t  hal_mmio_read8(volatile void *addr);
static inline uint16_t hal_mmio_read16(volatile void *addr);
static inline uint32_t hal_mmio_read32(volatile void *addr);
static inline uint64_t hal_mmio_read64(volatile void *addr);

static inline void hal_mmio_write8(volatile void *addr, uint8_t val);
static inline void hal_mmio_write16(volatile void *addr, uint16_t val);
static inline void hal_mmio_write32(volatile void *addr, uint32_t val);
static inline void hal_mmio_write64(volatile void *addr, uint64_t val);

/**
 * @brief 内存屏障
 */
static inline void hal_memory_barrier(void);
static inline void hal_read_barrier(void);
static inline void hal_write_barrier(void);

#ifdef ARCH_X86
/**
 * @brief 端口 I/O (仅 x86)
 */
static inline uint8_t  hal_port_read8(uint16_t port);
static inline uint16_t hal_port_read16(uint16_t port);
static inline uint32_t hal_port_read32(uint16_t port);

static inline void hal_port_write8(uint16_t port, uint8_t val);
static inline void hal_port_write16(uint16_t port, uint16_t val);
static inline void hal_port_write32(uint16_t port, uint32_t val);
#endif

#endif // _HAL_H_
```


### 架构特定类型定义

```c
// src/include/arch/i686/arch_types.h
typedef uint32_t uintptr_t;
typedef int32_t  intptr_t;
typedef uint32_t size_t;
#define KERNEL_VIRTUAL_BASE 0x80000000UL
#define PAGE_SIZE 4096
#define PAGE_TABLE_LEVELS 2

// src/include/arch/x86_64/arch_types.h
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;
typedef uint64_t size_t;
#define KERNEL_VIRTUAL_BASE 0xFFFF800000000000ULL
#define PAGE_SIZE 4096
#define PAGE_TABLE_LEVELS 4

// src/include/arch/arm64/arch_types.h
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;
typedef uint64_t size_t;
#define KERNEL_VIRTUAL_BASE 0xFFFF000000000000ULL
#define PAGE_SIZE 4096
#define PAGE_TABLE_LEVELS 4
```

### x86_64 特定组件

```c
// src/arch/x86_64/include/cpu.h

/* 64位 GDT 段选择子 */
#define GDT64_KERNEL_CODE  0x08
#define GDT64_KERNEL_DATA  0x10
#define GDT64_USER_CODE    0x18
#define GDT64_USER_DATA    0x20
#define GDT64_TSS          0x28

/* 64位 TSS 结构 */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          // Ring 0 栈指针
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        // Interrupt Stack Table
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

/* 64位 IDT 门描述符 */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;           // Interrupt Stack Table 索引
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt64_entry_t;

/* 64位页表项 */
typedef uint64_t pml4e_t;   // Page Map Level 4 Entry
typedef uint64_t pdpte_t;   // Page Directory Pointer Table Entry
typedef uint64_t pde_t;     // Page Directory Entry
typedef uint64_t pte_t;     // Page Table Entry

#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITE     (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_NX        (1ULL << 63)  // No Execute
```

### ARM64 特定组件

```c
// src/arch/arm64/include/cpu.h

/* Exception Levels */
#define EL0  0  // User mode
#define EL1  1  // Kernel mode
#define EL2  2  // Hypervisor
#define EL3  3  // Secure Monitor

/* System Registers */
#define SCTLR_EL1   "sctlr_el1"
#define TTBR0_EL1   "ttbr0_el1"   // User space page table
#define TTBR1_EL1   "ttbr1_el1"   // Kernel space page table
#define TCR_EL1     "tcr_el1"     // Translation Control Register
#define MAIR_EL1    "mair_el1"    // Memory Attribute Indirection Register
#define VBAR_EL1    "vbar_el1"    // Vector Base Address Register
#define FAR_EL1     "far_el1"     // Fault Address Register
#define ESR_EL1     "esr_el1"     // Exception Syndrome Register

/* ARM64 页表项 */
typedef uint64_t arm64_pte_t;

#define ARM64_PTE_VALID     (1ULL << 0)
#define ARM64_PTE_TABLE     (1ULL << 1)
#define ARM64_PTE_AF        (1ULL << 10)  // Access Flag
#define ARM64_PTE_AP_RW     (0ULL << 6)   // Read/Write
#define ARM64_PTE_AP_RO     (2ULL << 6)   // Read Only
#define ARM64_PTE_USER      (1ULL << 6)   // User accessible
#define ARM64_PTE_UXN       (1ULL << 54)  // User Execute Never
#define ARM64_PTE_PXN       (1ULL << 53)  // Privileged Execute Never

/* GIC (Generic Interrupt Controller) */
#define GICD_BASE   0x08000000  // Distributor base (QEMU virt)
#define GICC_BASE   0x08010000  // CPU Interface base

/* ARM64 上下文结构 */
typedef struct {
    uint64_t x[31];     // X0-X30
    uint64_t sp;        // Stack Pointer
    uint64_t pc;        // Program Counter (ELR_EL1)
    uint64_t pstate;    // Processor State (SPSR_EL1)
    uint64_t ttbr0;     // User page table
} arm64_context_t;
```


## Data Models

### 统一类型系统

```c
// src/include/types.h (修改后)

#ifndef _TYPES_H_
#define _TYPES_H_

/* 包含架构特定类型 */
#if defined(ARCH_I686)
    #include <arch/i686/arch_types.h>
#elif defined(ARCH_X86_64)
    #include <arch/x86_64/arch_types.h>
#elif defined(ARCH_ARM64)
    #include <arch/arm64/arch_types.h>
#else
    #error "Unknown architecture"
#endif

/* 固定宽度整数类型 (所有架构通用) */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* 布尔类型 */
#ifndef __cplusplus
typedef unsigned char bool;
#define true  1
#define false 0
#endif

/* NULL 定义 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* 架构相关的地址转换宏 */
#define VIRT_TO_PHYS(addr)  ((uintptr_t)(addr) - KERNEL_VIRTUAL_BASE)
#define PHYS_TO_VIRT(addr)  ((uintptr_t)(addr) + KERNEL_VIRTUAL_BASE)
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#endif // _TYPES_H_
```

### 统一任务结构

```c
// src/include/kernel/task.h (修改后)

typedef struct task {
    /* 基本信息 */
    uint32_t pid;
    char name[32];
    task_state_t state;
    
    /* 调度信息 */
    uint32_t priority;
    uint32_t time_slice;
    uint64_t runtime_ms;
    uint64_t sleep_until_ms;
    
    /* CPU 上下文 (架构特定) */
    hal_context_t context;
    
    /* 内核栈 */
    uintptr_t kernel_stack_base;
    uintptr_t kernel_stack;
    
    /* 用户空间 */
    bool is_user_process;
    uintptr_t user_entry;
    uintptr_t user_stack_base;
    uintptr_t user_stack;
    
    /* 内存管理 */
    uintptr_t page_table_phys;  // 页表物理地址
    
    /* 堆管理 */
    uintptr_t heap_start;
    uintptr_t heap_end;
    uintptr_t heap_max;
    
    /* 文件系统 */
    fd_table_t *fd_table;
    char cwd[MAX_CWD_LENGTH];
    
    /* 进程关系 */
    struct task *parent;
    
    /* 退出信息 */
    uint32_t exit_code;
    
    /* 链表指针 */
    struct task *next;
    struct task *prev;
} task_t;
```

### 页表结构对比

```
i686 (2级页表):
┌─────────────────┐
│  Page Directory │ 1024 entries × 4 bytes = 4KB
│    (1024 PDEs)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Page Table    │ 1024 entries × 4 bytes = 4KB
│   (1024 PTEs)   │
└─────────────────┘

x86_64 (4级页表):
┌─────────────────┐
│      PML4       │ 512 entries × 8 bytes = 4KB
│  (512 PML4Es)   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│      PDPT       │ 512 entries × 8 bytes = 4KB
│  (512 PDPTEs)   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│       PD        │ 512 entries × 8 bytes = 4KB
│   (512 PDEs)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│       PT        │ 512 entries × 8 bytes = 4KB
│   (512 PTEs)    │
└─────────────────┘

ARM64 (4级页表, 4KB granule):
┌─────────────────┐
│     Level 0     │ 512 entries × 8 bytes = 4KB
│   (512 L0Ds)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Level 1     │ 512 entries × 8 bytes = 4KB
│   (512 L1Ds)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Level 2     │ 512 entries × 8 bytes = 4KB
│   (512 L2Ds)    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Level 3     │ 512 entries × 8 bytes = 4KB
│   (512 L3Ds)    │
└─────────────────┘
```


## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

Based on the prework analysis, the following correctness properties have been identified:

### Property 1: HAL Initialization Dispatch

*For any* supported architecture, when the kernel initializes, the HAL interface SHALL dispatch to the correct architecture-specific initialization routine, and the initialization SHALL complete successfully.

**Validates: Requirements 1.1**

### Property 2: PMM Page Size Correctness

*For any* physical memory allocation request, the PMM SHALL return addresses that are aligned to the architecture's standard page size (4KB for all currently supported architectures).

**Validates: Requirements 5.1**

### Property 3: VMM Page Table Format Correctness

*For any* virtual-to-physical mapping operation, the VMM SHALL generate page table entries in the correct format for the target architecture (2-level for i686, 4-level for x86_64, 4-level for ARM64).

**Validates: Requirements 5.2**

### Property 4: VMM Kernel Mapping Range Correctness

*For any* kernel virtual address, the address SHALL fall within the architecture-appropriate higher-half range (≥0x80000000 for i686, ≥0xFFFF800000000000 for x86_64, ≥0xFFFF000000000000 for ARM64).

**Validates: Requirements 5.3**

### Property 5: VMM Page Fault Interpretation

*For any* page fault exception, the VMM SHALL correctly interpret the architecture-specific fault information (CR2 and error code on x86, FAR_EL1 and ESR_EL1 on ARM64) to determine the faulting address and fault type.

**Validates: Requirements 5.4**

### Property 6: VMM COW Flag Correctness

*For any* Copy-on-Write page, the VMM SHALL use the correct architecture-specific page table entry flags to mark the page as read-only while preserving the COW indicator.

**Validates: Requirements 5.5**

### Property 7: Interrupt Register State Preservation

*For any* interrupt or exception, the interrupt handler SHALL save all architecture-specific registers before handling and restore them exactly upon return, such that the interrupted code continues execution correctly.

**Validates: Requirements 6.1, 6.2**

### Property 8: Interrupt Handler Registration API Consistency

*For any* interrupt handler registration through the HAL API, the handler SHALL be invoked when the corresponding interrupt occurs, regardless of the underlying architecture-specific interrupt numbering.

**Validates: Requirements 6.4**

### Property 9: Context Switch Register Preservation

*For any* context switch between tasks, all architecture-specific registers (including general-purpose, stack pointer, program counter, and status registers) SHALL be preserved such that switching back to a task resumes execution exactly where it left off.

**Validates: Requirements 7.1, 7.2**

### Property 10: Address Space Switch Correctness

*For any* address space switch during task switching, the correct architecture-specific page table base register (CR3 on x86, TTBR0_EL1 on ARM64) SHALL be updated to point to the new task's page table.

**Validates: Requirements 7.3**

### Property 11: User Mode Transition Correctness

*For any* transition from kernel mode to user mode, the CPU SHALL be in the correct privilege level (Ring 3 on x86, EL0 on ARM64) after the transition, and the user program SHALL execute at the specified entry point.

**Validates: Requirements 7.4**

### Property 12: System Call Round-Trip Correctness

*For any* system call invocation from user space, the system call number and arguments SHALL be correctly passed to the kernel handler, and the return value SHALL be correctly returned to user space in the architecture-appropriate register.

**Validates: Requirements 8.1, 8.2, 8.3**

### Property 13: System Call Error Consistency

*For any* system call that fails, the return value SHALL be a negative errno value that is consistent across all supported architectures for the same error condition.

**Validates: Requirements 8.4**

### Property 14: MMIO Memory Barrier Correctness

*For any* MMIO read or write operation, the appropriate memory barriers SHALL be issued to ensure correct ordering with respect to other memory operations, preventing reordering by the CPU or compiler.

**Validates: Requirements 9.1**

### Property 15: DMA Cache Coherency

*For any* DMA buffer, the appropriate cache operations (invalidate before DMA read, clean before DMA write) SHALL be performed to maintain coherency between CPU cache and device memory access.

**Validates: Requirements 9.4**

### Property 16: User Library System Call Instruction Correctness

*For any* system call from the user library, the correct architecture-specific instruction SHALL be used (INT 0x80 or SYSENTER on i686, SYSCALL on x86_64, SVC on ARM64).

**Validates: Requirements 10.2**

### Property 17: User Library Data Type Size Correctness

*For any* pointer or size_t type in the user library, the size SHALL match the architecture's native word size (32-bit on i686, 64-bit on x86_64 and ARM64).

**Validates: Requirements 10.3**


## Error Handling

### 架构检测错误

```c
// 编译时架构检测
#if !defined(ARCH_I686) && !defined(ARCH_X86_64) && !defined(ARCH_ARM64)
    #error "No architecture defined. Use ARCH=i686|x86_64|arm64"
#endif

// 运行时架构验证
void hal_verify_architecture(void) {
    #if defined(ARCH_X86_64)
    // 验证 CPU 支持长模式
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1 << 29))) {
        panic("CPU does not support long mode (x86_64)");
    }
    #endif
    
    #if defined(ARCH_ARM64)
    // 验证当前在 EL1
    uint64_t current_el;
    asm volatile("mrs %0, CurrentEL" : "=r"(current_el));
    if ((current_el >> 2) != 1) {
        panic("Kernel must run at EL1");
    }
    #endif
}
```

### 页表错误处理

```c
// 统一的页错误处理接口
typedef struct {
    uintptr_t fault_addr;    // 错误地址
    bool is_present;         // 页是否存在
    bool is_write;           // 是否写操作
    bool is_user;            // 是否用户模式
    bool is_exec;            // 是否执行操作
} page_fault_info_t;

// 架构特定的解析函数
page_fault_info_t hal_parse_page_fault(void *arch_specific_info);

// 通用页错误处理
void vmm_handle_page_fault(page_fault_info_t *info) {
    if (info->fault_addr >= KERNEL_VIRTUAL_BASE) {
        // 内核空间错误
        if (vmm_handle_kernel_page_fault(info->fault_addr)) {
            return;  // 成功处理（如同步页目录）
        }
    }
    
    if (info->is_present && info->is_write) {
        // 可能是 COW
        if (vmm_handle_cow_page_fault(info->fault_addr)) {
            return;
        }
    }
    
    // 无法处理的页错误
    panic("Unhandled page fault at 0x%lx", info->fault_addr);
}
```

### 系统调用错误处理

```c
// 统一的错误码定义 (所有架构相同)
#define EPERM           1   // Operation not permitted
#define ENOENT          2   // No such file or directory
#define ESRCH           3   // No such process
#define EINTR           4   // Interrupted system call
#define EIO             5   // I/O error
#define ENOMEM         12   // Out of memory
#define EACCES         13   // Permission denied
#define EFAULT         14   // Bad address
#define EBUSY          16   // Device or resource busy
#define EEXIST         17   // File exists
#define ENODEV         19   // No such device
#define ENOTDIR        20   // Not a directory
#define EISDIR         21   // Is a directory
#define EINVAL         22   // Invalid argument
#define ENFILE         23   // File table overflow
#define EMFILE         24   // Too many open files
#define ENOSPC         28   // No space left on device
#define ENOSYS         38   // Function not implemented

// 系统调用返回错误
static inline long syscall_error(int errno_val) {
    return -errno_val;  // 所有架构统一返回负值
}
```

## Testing Strategy

### 测试框架

本项目使用内核内置测试框架 (ktest) 进行单元测试，并使用 QEMU 进行集成测试。

**单元测试**: 使用现有的 `src/tests/` 框架，扩展支持多架构
**属性测试**: 使用 QuickCheck 风格的属性测试库 (theft 或自定义实现)

### 属性测试库选择

由于 CastorOS 是裸机内核，无法使用标准 C 库，我们将实现一个轻量级的属性测试框架：

```c
// src/tests/pbt/pbt.h - Property-Based Testing Framework

#define PBT_ITERATIONS 100

typedef struct {
    uint64_t seed;
    uint32_t iteration;
} pbt_state_t;

// 随机数生成
uint64_t pbt_random(pbt_state_t *state);
uint64_t pbt_random_range(pbt_state_t *state, uint64_t min, uint64_t max);

// 属性测试宏
#define PBT_PROPERTY(name, generator, property) \
    void pbt_##name(void) { \
        pbt_state_t state = { .seed = 12345, .iteration = 0 }; \
        for (uint32_t i = 0; i < PBT_ITERATIONS; i++) { \
            state.iteration = i; \
            generator; \
            if (!(property)) { \
                kprintf("Property '%s' failed at iteration %u\n", #name, i); \
                return; \
            } \
        } \
        kprintf("Property '%s' passed (%u iterations)\n", #name, PBT_ITERATIONS); \
    }
```

### 测试用例设计

**HAL 初始化测试**:
```c
// 验证 HAL 初始化正确分发到架构特定代码
void test_hal_init_dispatch(void) {
    // 检查架构特定的初始化标志
    ASSERT(hal_cpu_initialized());
    ASSERT(hal_interrupt_initialized());
    ASSERT(hal_mmu_initialized());
}
```

**页表格式测试**:
```c
// 属性测试：页表映射后可以正确读取
PBT_PROPERTY(page_table_roundtrip,
    uintptr_t virt = pbt_random_range(&state, 0x1000, 0x7FFFFFFF) & ~0xFFF;
    uintptr_t phys = pbt_random_range(&state, 0x100000, 0x10000000) & ~0xFFF,
    
    hal_mmu_map(virt, phys, PAGE_PRESENT | PAGE_WRITE);
    uintptr_t result = hal_mmu_virt_to_phys(virt);
    hal_mmu_unmap(virt);
    result == phys
)
```

**上下文切换测试**:
```c
// 属性测试：上下文切换保持寄存器值
PBT_PROPERTY(context_switch_preserves_registers,
    hal_context_t ctx1, ctx2;
    uint64_t test_values[16];
    for (int i = 0; i < 16; i++) test_values[i] = pbt_random(&state),
    
    // 设置测试值到上下文
    set_context_registers(&ctx1, test_values);
    // 切换并切回
    hal_context_switch(&ctx1, &ctx2);
    hal_context_switch(&ctx2, &ctx1);
    // 验证寄存器值保持不变
    verify_context_registers(&ctx1, test_values)
)
```

### 多架构测试执行

```makefile
# Makefile 测试目标
test-all: test-i686 test-x86_64 test-arm64

test-i686:
	$(MAKE) ARCH=i686 clean all
	qemu-system-i386 -kernel $(KERNEL) -nographic -serial mon:stdio \
		-append "test" | tee test-i686.log

test-x86_64:
	$(MAKE) ARCH=x86_64 clean all
	qemu-system-x86_64 -kernel $(KERNEL) -nographic -serial mon:stdio \
		-append "test" | tee test-x86_64.log

test-arm64:
	$(MAKE) ARCH=arm64 clean all
	qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel $(KERNEL) \
		-nographic -serial mon:stdio -append "test" | tee test-arm64.log
```
