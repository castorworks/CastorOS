# Design Document: ARM64 Kernel Integration

## Overview

本设计文档描述了将 CastorOS ARM64 最小化内核扩展为功能完整内核的技术方案。核心目标是将现有的通用内核模块（PMM、VMM、任务管理、系统调用等）集成到 ARM64 架构中。

### 设计原则

1. **最大化代码复用**: 使用现有的架构无关代码，仅实现 ARM64 特定适配
2. **HAL 抽象**: 通过 HAL 接口隔离架构差异
3. **渐进式集成**: 按依赖顺序逐步集成各模块
4. **保持 i686 兼容**: 不破坏现有 i686 功能

### 当前架构状态

```
ARM64 最小化内核 (当前)
┌─────────────────────────────────────────┐
│  kernel_main() [stubs.c]                │
│    ├── hal_cpu_init()      ✅           │
│    ├── hal_interrupt_init() ✅          │
│    └── idle loop           ✅           │
├─────────────────────────────────────────┤
│  PMM: stub (bump allocator) ❌          │
│  VMM: 页表操作完成，未集成  ⚠️          │
│  Task: stub                 ❌          │
│  Syscall: stub              ❌          │
└─────────────────────────────────────────┘

ARM64 完整内核 (目标)
┌─────────────────────────────────────────┐
│  kernel_main() [kernel.c]               │
│    ├── pmm_init()          ✅           │
│    ├── vmm_init()          ✅           │
│    ├── heap_init()         ✅           │
│    ├── task_init()         ✅           │
│    ├── vfs_init()          ✅           │
│    └── scheduler_start()   ✅           │
├─────────────────────────────────────────┤
│  PMM: src/mm/pmm.c         ✅           │
│  VMM: src/mm/vmm.c         ✅           │
│  Task: src/kernel/task.c   ✅           │
│  Syscall: src/kernel/syscall.c ✅       │
└─────────────────────────────────────────┘
```

## Architecture

### 模块集成架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    Common Kernel Code                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐           │
│  │  PMM     │ │  VMM     │ │  Task    │ │ Syscall  │           │
│  │ pmm.c   │ │ vmm.c   │ │ task.c  │ │syscall.c│           │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘           │
│       │            │            │            │                  │
│       ▼            ▼            ▼            ▼                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    HAL Interface                         │   │
│  │  hal_mmu_*()  hal_context_*()  hal_interrupt_*()        │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    ARM64 Architecture Code                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐           │
│  │  mmu.c   │ │context.c │ │  gic.c   │ │  svc.S   │           │
│  │ 页表操作 │ │上下文切换│ │ 中断控制 │ │系统调用  │           │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘           │
└─────────────────────────────────────────────────────────────────┘
```

### ARM64 内存布局

```
虚拟地址空间 (48-bit VA, 4KB granule)
┌────────────────────────────────────────┐ 0xFFFF_FFFF_FFFF_FFFF
│                                        │
│         Kernel Space (TTBR1)           │
│                                        │
│  0xFFFF_0000_4000_0000 - Kernel Heap   │
│  0xFFFF_0000_0010_0000 - Kernel Code   │
│  0xFFFF_0000_0000_0000 - Direct Map    │
│                                        │
├────────────────────────────────────────┤ 0xFFFF_0000_0000_0000
│                                        │
│         (Unused / Hole)                │
│                                        │
├────────────────────────────────────────┤ 0x0000_FFFF_FFFF_FFFF
│                                        │
│         User Space (TTBR0)             │
│                                        │
│  0x0000_0000_7FFF_F000 - User Stack    │
│  0x0000_0000_4000_0000 - User Heap     │
│  0x0000_0000_0040_0000 - User Code     │
│                                        │
└────────────────────────────────────────┘ 0x0000_0000_0000_0000
```

## Components and Interfaces

### 1. PMM 集成接口

```c
// src/mm/pmm.c 需要的 ARM64 适配

/**
 * @brief 从 DTB 获取内存信息
 * 
 * ARM64 使用 DTB 而非 Multiboot 获取内存布局
 */
typedef struct {
    paddr_t base;
    size_t size;
} memory_region_t;

/**
 * @brief ARM64 特定的内存信息获取
 * 在 boot_info.c 中实现
 */
boot_info_t *boot_info_init_dtb(void *dtb_addr);

// PMM 初始化流程
void pmm_init(boot_info_t *boot_info) {
    // 1. 从 boot_info 获取内存区域
    // 2. 初始化位图
    // 3. 标记已使用区域（内核、页表等）
}
```

### 2. VMM 集成接口

```c
// src/mm/vmm.c 使用 HAL MMU 接口

// 已实现的 HAL MMU 接口 (src/arch/arm64/mm/mmu.c)
bool hal_mmu_map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags);
paddr_t hal_mmu_unmap(hal_addr_space_t space, vaddr_t virt);
bool hal_mmu_query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags);
bool hal_mmu_protect(hal_addr_space_t space, vaddr_t virt, uint32_t set, uint32_t clear);
hal_addr_space_t hal_mmu_create_space(void);
void hal_mmu_destroy_space(hal_addr_space_t space);

// VMM 需要调用这些接口而非直接操作页表
```

### 3. 任务管理接口

```c
// src/kernel/task.c 使用 HAL 上下文接口

// 已实现的 HAL 上下文接口 (src/arch/arm64/task/context.c)
void hal_context_init(hal_context_t *ctx, uintptr_t entry, uintptr_t stack, bool is_user);
void hal_context_switch(hal_context_t **old_ctx, hal_context_t *new_ctx);
size_t hal_context_size(void);

// ARM64 上下文结构 (src/arch/arm64/include/context.h)
typedef struct {
    uint64_t x[31];     // X0-X30
    uint64_t sp;        // Stack Pointer
    uint64_t pc;        // Program Counter (ELR_EL1)
    uint64_t pstate;    // Processor State (SPSR_EL1)
    uint64_t ttbr0;     // User page table
} arm64_context_t;
```

### 4. 系统调用接口

```c
// src/kernel/syscall.c 系统调用分发

// ARM64 系统调用入口 (src/arch/arm64/syscall/svc.S)
// X8 = syscall number
// X0-X5 = arguments
// X0 = return value

// 系统调用分发器签名
int64_t syscall_dispatcher(uint64_t num, uint64_t a0, uint64_t a1, 
                           uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
```

## Data Models

### Boot Info 结构

```c
// src/include/kernel/boot_info.h

typedef struct boot_info {
    /* 内存信息 */
    paddr_t mem_lower;          // 低端内存大小
    paddr_t mem_upper;          // 高端内存大小
    uint32_t mmap_count;        // 内存映射条目数
    memory_map_entry_t *mmap;   // 内存映射表
    
    /* 内核位置 */
    paddr_t kernel_phys_start;
    paddr_t kernel_phys_end;
    vaddr_t kernel_virt_start;
    vaddr_t kernel_virt_end;
    
    /* 引导信息来源 */
    enum {
        BOOT_SOURCE_MULTIBOOT,
        BOOT_SOURCE_MULTIBOOT2,
        BOOT_SOURCE_DTB
    } source;
    
    /* DTB 特定 (ARM64) */
    void *dtb_addr;
} boot_info_t;
```

### 任务结构适配

```c
// src/include/kernel/task.h

typedef struct task {
    uint32_t pid;
    char name[32];
    task_state_t state;
    
    /* CPU 上下文 - 使用 HAL 类型 */
    hal_context_t *context;     // 指向架构特定上下文
    
    /* 内存管理 */
    hal_addr_space_t addr_space; // 地址空间句柄
    
    /* 栈 */
    vaddr_t kernel_stack_base;
    vaddr_t kernel_stack_top;
    vaddr_t user_stack_base;
    vaddr_t user_stack_top;
    
    /* ... 其他字段 ... */
} task_t;
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*



Based on the prework analysis, the following correctness properties have been identified:

### Property 1: PMM Frame Alignment

*For any* physical frame allocation on ARM64, the returned physical address SHALL be aligned to 4KB (PAGE_SIZE).

**Validates: Requirements 1.2**

### Property 2: PMM Reference Count Consistency

*For any* sequence of frame allocations, frees, and reference count operations, the reference count for each frame SHALL accurately reflect the number of active references.

**Validates: Requirements 1.3**

### Property 3: VMM Page Fault Interpretation

*For any* page fault on ARM64, the VMM SHALL correctly parse ESR_EL1 and FAR_EL1 to determine the faulting address, fault type (read/write/exec), and privilege level.

**Validates: Requirements 2.3**

### Property 4: VMM COW Bit Correctness

*For any* Copy-on-Write page on ARM64, the page table entry SHALL have the software-defined COW bit (bit 56) set, and the write permission SHALL be cleared.

**Validates: Requirements 2.4**

### Property 5: Heap Allocation Validity

*For any* kmalloc request with size > 0, the returned address SHALL be within the kernel heap region (0xFFFF_0000_4000_0000 - 0xFFFF_0000_8000_0000 on ARM64).

**Validates: Requirements 3.2**

### Property 6: Heap Round-Trip

*For any* memory block allocated with kmalloc and freed with kfree, the memory SHALL be available for subsequent allocations.

**Validates: Requirements 3.3**

### Property 7: Task Context Preservation

*For any* task context switch on ARM64, all general-purpose registers (X0-X30), SP, PC, and PSTATE SHALL be preserved such that switching back to the task resumes execution exactly where it left off.

**Validates: Requirements 4.1, 4.2**

### Property 8: Task Exit Cleanup

*For any* task that calls exit, the task's address space, kernel stack, and other resources SHALL be freed, and the scheduler SHALL select another runnable task.

**Validates: Requirements 4.3**

### Property 9: System Call Round-Trip

*For any* system call invocation via SVC on ARM64, the syscall number from X8 and arguments from X0-X5 SHALL be correctly passed to the handler, and the return value SHALL be placed in X0.

**Validates: Requirements 5.1, 5.2, 5.3**

### Property 10: System Call Error Consistency

*For any* system call that fails on ARM64, the return value SHALL be a negative errno value consistent with the same error on i686.

**Validates: Requirements 5.4**

### Property 11: User Process Address Space Isolation

*For any* user process on ARM64, the process SHALL have a unique TTBR0 value pointing to its own page table, and user addresses SHALL not be accessible from other processes.

**Validates: Requirements 6.1**

### Property 12: User Exception Handling

*For any* exception caused by a user program on ARM64, the kernel SHALL handle the exception in EL1 and return to EL0 with the correct state.

**Validates: Requirements 6.3**

### Property 13: Fork COW Semantics

*For any* fork system call on ARM64, the child process SHALL share physical pages with the parent using COW semantics, and writing to a shared page SHALL trigger a page fault and copy.

**Validates: Requirements 7.1**

### Property 14: File Operation Round-Trip

*For any* file on ARM64, opening the file SHALL return a valid fd, writing data and reading it back SHALL return the same data, and closing SHALL release the fd.

**Validates: Requirements 8.2, 8.3, 8.4**


## Error Handling

### PMM Errors

```c
// 内存不足
paddr_t pmm_alloc_frame(void) {
    if (no_free_frames) {
        LOG_ERROR("PMM: Out of physical memory");
        return PADDR_INVALID;
    }
    // ...
}

// 无效帧释放
void pmm_free_frame(paddr_t frame) {
    if (!is_valid_frame(frame)) {
        LOG_ERROR("PMM: Attempt to free invalid frame 0x%llx", frame);
        return;
    }
    // ...
}
```

### VMM Errors

```c
// 页错误处理
void vmm_handle_page_fault(hal_page_fault_info_t *info) {
    if (info->fault_addr >= KERNEL_VIRTUAL_BASE && !info->is_kernel) {
        // 用户态访问内核地址
        task_signal(current_task, SIGSEGV);
        return;
    }
    
    if (info->is_write && is_cow_page(info->fault_addr)) {
        // COW 页错误
        if (!vmm_handle_cow(info->fault_addr)) {
            task_signal(current_task, SIGSEGV);
        }
        return;
    }
    
    // 无法处理的页错误
    if (info->is_kernel) {
        panic("Kernel page fault at 0x%llx", info->fault_addr);
    } else {
        task_signal(current_task, SIGSEGV);
    }
}
```

### 系统调用错误

```c
// 统一错误返回
int64_t syscall_dispatcher(uint64_t num, ...) {
    if (num >= SYSCALL_COUNT) {
        return -ENOSYS;
    }
    
    syscall_handler_t handler = syscall_table[num];
    if (!handler) {
        return -ENOSYS;
    }
    
    return handler(...);
}
```

## Testing Strategy

### 测试框架

使用现有的 ktest 框架进行单元测试，使用轻量级属性测试框架进行属性测试。

### 单元测试

```c
// PMM 测试
TEST_CASE(test_pmm_alloc_free) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT(frame != PADDR_INVALID);
    ASSERT(IS_PADDR_ALIGNED(frame));
    pmm_free_frame(frame);
}

// VMM 测试
TEST_CASE(test_vmm_map_unmap) {
    vaddr_t virt = 0x1000000;
    paddr_t phys = pmm_alloc_frame();
    ASSERT(hal_mmu_map(HAL_ADDR_SPACE_CURRENT, virt, phys, HAL_PAGE_PRESENT | HAL_PAGE_WRITE));
    
    paddr_t result;
    ASSERT(hal_mmu_query(HAL_ADDR_SPACE_CURRENT, virt, &result, NULL));
    ASSERT(result == phys);
    
    hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, virt);
    pmm_free_frame(phys);
}

// 任务测试
TEST_CASE(test_task_create) {
    task_t *task = task_create_kernel("test", test_func, NULL);
    ASSERT(task != NULL);
    ASSERT(task->context != NULL);
    task_destroy(task);
}
```

### 属性测试

```c
// Property 1: PMM Frame Alignment
// **Feature: arm64-kernel-integration, Property 1: PMM Frame Alignment**
// **Validates: Requirements 1.2**
PBT_PROPERTY(pmm_frame_alignment,
    /* Generator: allocate random number of frames */
    uint32_t count = pbt_random_range(&state, 1, 100);
    paddr_t frames[100];
    for (uint32_t i = 0; i < count; i++) {
        frames[i] = pmm_alloc_frame();
    },
    /* Property: all frames are 4KB aligned */
    bool all_aligned = true;
    for (uint32_t i = 0; i < count; i++) {
        if (frames[i] != PADDR_INVALID && !IS_PADDR_ALIGNED(frames[i])) {
            all_aligned = false;
        }
        if (frames[i] != PADDR_INVALID) pmm_free_frame(frames[i]);
    }
    all_aligned
)

// Property 7: Task Context Preservation
// **Feature: arm64-kernel-integration, Property 7: Task Context Preservation**
// **Validates: Requirements 4.1, 4.2**
PBT_PROPERTY(task_context_preservation,
    /* Generator: create random register values */
    uint64_t test_values[31];
    for (int i = 0; i < 31; i++) {
        test_values[i] = pbt_random(&state);
    },
    /* Property: context switch preserves all registers */
    hal_context_t ctx1, ctx2;
    set_context_registers(&ctx1, test_values);
    hal_context_switch(&ctx1, &ctx2);
    hal_context_switch(&ctx2, &ctx1);
    verify_context_registers(&ctx1, test_values)
)
```

### 集成测试

```c
// 用户程序测试
TEST_CASE(test_user_program) {
    // 加载并运行 helloworld
    task_t *task = task_create_user("/bin/helloworld");
    ASSERT(task != NULL);
    
    // 等待任务完成
    int status;
    task_wait(task->pid, &status);
    ASSERT(status == 0);
}

// fork/exec 测试
TEST_CASE(test_fork_exec) {
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        exec("/bin/helloworld", NULL);
        exit(1);  // exec 失败
    } else {
        // 父进程
        int status;
        wait(&status);
        ASSERT(WEXITSTATUS(status) == 0);
    }
}
```

