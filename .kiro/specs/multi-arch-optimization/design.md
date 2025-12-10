# Design Document: Multi-Architecture Optimization

## Overview

本设计文档描述 CastorOS 多架构支持优化的技术方案。核心目标是通过增强 HAL 抽象层、统一内存管理接口、标准化设备驱动模型，减少架构相关的条件编译，提升代码可维护性和可扩展性。

### 设计原则

1. **抽象优于条件编译**：用函数调用替代 `#ifdef`
2. **能力查询优于假设**：运行时查询而非编译时假设
3. **统一接口优于特化实现**：一套 API 适配所有架构
4. **显式错误优于静默失败**：明确的错误码和错误处理

## Architecture

### 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
├─────────────────────────────────────────────────────────────┤
│                    System Call Interface                     │
│                  (hal_syscall_args_t)                        │
├─────────────────────────────────────────────────────────────┤
│                      Kernel Core                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │   VFS    │  │ Scheduler│  │   IPC    │  │  Network │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
├─────────────────────────────────────────────────────────────┤
│                   Memory Management                          │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                      VMM                              │   │
│  │  (Architecture-independent, uses pgtable abstraction)│   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                   Platform Driver Model                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  platform_device / platform_driver abstraction        │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│              Hardware Abstraction Layer (HAL)                │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐              │
│  │ Capability │ │  pgtable   │ │  Logical   │              │
│  │   Query    │ │ Abstraction│ │    IRQ     │              │
│  └────────────┘ └────────────┘ └────────────┘              │
├─────────────────────────────────────────────────────────────┤
│              Architecture Implementations                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │   i686   │  │  x86_64  │  │  arm64   │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘
```


## Components and Interfaces

### 1. HAL 能力查询接口

**文件位置**: `src/include/hal/hal_caps.h`

```c
/**
 * @brief HAL 能力结构体
 * 
 * 描述当前架构支持的硬件特性和限制
 */
typedef struct hal_capabilities {
    /* 硬件特性 */
    bool has_huge_pages;        /**< 支持 2MB/1GB 大页 */
    bool has_nx_bit;            /**< 支持不可执行位 */
    bool has_port_io;           /**< 支持端口 I/O (x86 only) */
    bool cache_coherent_dma;    /**< DMA 缓存一致性 */
    bool has_iommu;             /**< 支持 IOMMU */
    
    /* 页表配置 */
    uint32_t page_table_levels; /**< 页表级数 (2 or 4) */
    uint32_t page_sizes[4];     /**< 支持的页大小数组 */
    uint32_t page_size_count;   /**< 支持的页大小数量 */
    
    /* 地址空间限制 */
    uint64_t phys_addr_max;     /**< 最大物理地址 */
    uint64_t virt_addr_max;     /**< 最大虚拟地址 */
    uint64_t kernel_base;       /**< 内核虚拟地址基址 */
    uint64_t user_space_end;    /**< 用户空间结束地址 */
    
    /* 寄存器信息 */
    uint32_t gpr_count;         /**< 通用寄存器数量 */
    uint32_t gpr_size;          /**< 通用寄存器大小 (bytes) */
    uint32_t context_size;      /**< 上下文结构大小 */
} hal_capabilities_t;

/**
 * @brief 获取 HAL 能力信息
 * @param[out] caps 能力结构体指针
 */
void hal_get_capabilities(hal_capabilities_t *caps);

/**
 * @brief 检查特定能力是否支持
 * @param cap 能力标识
 * @return true 如果支持
 */
bool hal_has_capability(hal_cap_id_t cap);
```

### 2. 页表抽象层

**文件位置**: `src/include/hal/pgtable.h`

```c
/**
 * @brief 架构无关的页表项类型
 */
typedef uint64_t pte_t;

/**
 * @brief 页表项标志 (架构无关)
 */
typedef enum {
    PTE_PRESENT     = (1 << 0),
    PTE_WRITE       = (1 << 1),
    PTE_USER        = (1 << 2),
    PTE_NOCACHE     = (1 << 3),
    PTE_EXEC        = (1 << 4),
    PTE_COW         = (1 << 5),
    PTE_DIRTY       = (1 << 6),
    PTE_ACCESSED    = (1 << 7),
    PTE_HUGE        = (1 << 8),
} pte_flags_t;

/**
 * @brief 创建页表项
 * @param phys 物理地址
 * @param flags 页表项标志
 * @return 架构特定格式的页表项
 */
pte_t pgtable_make_entry(paddr_t phys, uint32_t flags);

/**
 * @brief 从页表项提取物理地址
 * @param entry 页表项
 * @return 物理地址
 */
paddr_t pgtable_get_phys(pte_t entry);

/**
 * @brief 从页表项提取标志
 * @param entry 页表项
 * @return 架构无关的标志
 */
uint32_t pgtable_get_flags(pte_t entry);

/**
 * @brief 检查页表项是否存在
 * @param entry 页表项
 * @return true 如果存在
 */
bool pgtable_is_present(pte_t entry);

/**
 * @brief 修改页表项标志
 * @param entry 原页表项
 * @param set_flags 要设置的标志
 * @param clear_flags 要清除的标志
 * @return 修改后的页表项
 */
pte_t pgtable_modify_flags(pte_t entry, uint32_t set_flags, uint32_t clear_flags);
```

### 3. 逻辑中断号

**文件位置**: `src/include/hal/hal_irq.h`

```c
/**
 * @brief 逻辑中断类型
 */
typedef enum {
    HAL_IRQ_TIMER = 0,      /**< 系统定时器 */
    HAL_IRQ_KEYBOARD,       /**< 键盘 */
    HAL_IRQ_SERIAL0,        /**< 串口 0 */
    HAL_IRQ_SERIAL1,        /**< 串口 1 */
    HAL_IRQ_DISK_PRIMARY,   /**< 主磁盘控制器 */
    HAL_IRQ_DISK_SECONDARY, /**< 从磁盘控制器 */
    HAL_IRQ_NETWORK,        /**< 网络设备 */
    HAL_IRQ_USB,            /**< USB 控制器 */
    HAL_IRQ_RTC,            /**< 实时时钟 */
    HAL_IRQ_MAX
} hal_irq_type_t;

/**
 * @brief 获取逻辑中断对应的物理 IRQ 号
 * @param type 逻辑中断类型
 * @param instance 设备实例号 (多个同类设备时使用)
 * @return 物理 IRQ 号，失败返回 -1
 */
int32_t hal_irq_get_number(hal_irq_type_t type, uint32_t instance);

/**
 * @brief 注册逻辑中断处理程序
 * @param type 逻辑中断类型
 * @param instance 设备实例号
 * @param handler 处理函数
 * @param data 用户数据
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t hal_irq_register_logical(hal_irq_type_t type, uint32_t instance,
                                      hal_interrupt_handler_t handler, void *data);
```


### 4. 平台设备模型

**文件位置**: `src/include/drivers/platform.h`

```c
/**
 * @brief 平台设备资源类型
 */
typedef enum {
    PLATFORM_RES_MEM,   /**< 内存映射区域 */
    PLATFORM_RES_IRQ,   /**< 中断资源 */
    PLATFORM_RES_DMA,   /**< DMA 通道 */
} platform_res_type_t;

/**
 * @brief 平台设备资源
 */
typedef struct platform_resource {
    platform_res_type_t type;
    uint64_t start;
    uint64_t end;
    uint32_t flags;
} platform_resource_t;

/**
 * @brief 平台设备结构
 */
typedef struct platform_device {
    const char *name;
    uint32_t id;
    
    /* 设备资源 */
    platform_resource_t *resources;
    uint32_t num_resources;
    
    /* 设备发现来源 */
    enum { PLATFORM_SRC_PCI, PLATFORM_SRC_DTB, PLATFORM_SRC_ACPI } source;
    
    /* PCI 信息 (如果来自 PCI) */
    struct {
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t bus, slot, func;
    } pci;
    
    /* 驱动私有数据 */
    void *driver_data;
} platform_device_t;

/**
 * @brief 平台驱动结构
 */
typedef struct platform_driver {
    const char *name;
    
    /* 匹配信息 */
    const uint16_t *pci_ids;        /**< PCI vendor:device ID 对 */
    const char **compatible;         /**< DTB compatible 字符串 */
    
    /* 驱动回调 */
    int (*probe)(platform_device_t *dev);
    void (*remove)(platform_device_t *dev);
    int (*suspend)(platform_device_t *dev);
    int (*resume)(platform_device_t *dev);
} platform_driver_t;

/**
 * @brief 注册平台驱动
 */
int platform_driver_register(platform_driver_t *drv);

/**
 * @brief 获取设备资源
 */
platform_resource_t *platform_get_resource(platform_device_t *dev, 
                                            platform_res_type_t type, 
                                            uint32_t index);
```

### 5. 系统调用参数统一

**文件位置**: `src/include/hal/hal_syscall.h`

```c
/**
 * @brief 统一的系统调用参数结构
 */
typedef struct hal_syscall_args {
    uint64_t syscall_nr;    /**< 系统调用号 */
    uint64_t args[6];       /**< 参数 0-5 */
    void *extra_args;       /**< 额外参数指针 (>6 个参数时使用) */
} hal_syscall_args_t;

/**
 * @brief 从上下文提取系统调用参数
 * @param ctx CPU 上下文
 * @param[out] args 参数结构
 */
void hal_syscall_get_args(hal_context_t *ctx, hal_syscall_args_t *args);

/**
 * @brief 设置系统调用返回值
 * @param ctx CPU 上下文
 * @param ret 返回值
 */
void hal_syscall_set_return(hal_context_t *ctx, int64_t ret);

/**
 * @brief 设置系统调用错误码
 * @param ctx CPU 上下文
 * @param errno 错误码
 */
void hal_syscall_set_errno(hal_context_t *ctx, int32_t errno);
```

### 6. 启动信息结构

**文件位置**: `src/include/boot/boot_info.h`

```c
/**
 * @brief 内存区域类型
 */
typedef enum {
    BOOT_MEM_USABLE = 1,
    BOOT_MEM_RESERVED,
    BOOT_MEM_ACPI_RECLAIMABLE,
    BOOT_MEM_ACPI_NVS,
    BOOT_MEM_BAD,
} boot_mem_type_t;

/**
 * @brief 内存映射条目
 */
typedef struct boot_mmap_entry {
    uint64_t base;
    uint64_t length;
    boot_mem_type_t type;
} boot_mmap_entry_t;

/**
 * @brief 帧缓冲信息
 */
typedef struct boot_framebuffer {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t type;  /* 0=indexed, 1=RGB, 2=text */
} boot_framebuffer_t;

/**
 * @brief 统一的启动信息结构
 */
typedef struct boot_info {
    /* 内存信息 */
    uint64_t mem_lower;         /**< 低端内存大小 (KB) */
    uint64_t mem_upper;         /**< 高端内存大小 (KB) */
    boot_mmap_entry_t *mmap;    /**< 内存映射数组 */
    uint32_t mmap_count;        /**< 内存映射条目数 */
    
    /* 命令行 */
    const char *cmdline;
    
    /* 帧缓冲 */
    boot_framebuffer_t *framebuffer;
    
    /* 模块信息 */
    struct {
        uint64_t start;
        uint64_t end;
        const char *cmdline;
    } *modules;
    uint32_t module_count;
    
    /* 架构特定信息 */
    void *arch_info;            /**< ACPI RSDP, DTB 指针等 */
} boot_info_t;

/**
 * @brief 内核主入口点
 * @param info 启动信息
 */
void kernel_main(boot_info_t *info);
```

### 7. HAL 错误码

**文件位置**: `src/include/hal/hal_error.h`

```c
/**
 * @brief HAL 错误码
 */
typedef enum hal_error {
    HAL_OK = 0,                 /**< 成功 */
    HAL_ERR_INVALID_PARAM = -1, /**< 无效参数 */
    HAL_ERR_NO_MEMORY = -2,     /**< 内存不足 */
    HAL_ERR_NOT_SUPPORTED = -3, /**< 不支持的操作 */
    HAL_ERR_NOT_FOUND = -4,     /**< 未找到 */
    HAL_ERR_BUSY = -5,          /**< 资源忙 */
    HAL_ERR_TIMEOUT = -6,       /**< 超时 */
    HAL_ERR_IO = -7,            /**< I/O 错误 */
    HAL_ERR_PERMISSION = -8,    /**< 权限不足 */
} hal_error_t;

/**
 * @brief 检查 HAL 操作是否成功
 */
#define HAL_SUCCESS(err) ((err) == HAL_OK)

/**
 * @brief 检查 HAL 操作是否失败
 */
#define HAL_FAILED(err) ((err) != HAL_OK)
```


## Data Models

### 架构能力数据流

```
┌─────────────────┐
│  Architecture   │
│  Implementation │
│  (i686/x86_64/  │
│   arm64)        │
└────────┬────────┘
         │ hal_get_capabilities()
         ▼
┌─────────────────┐
│ hal_capabilities│
│     _t          │
│  ┌───────────┐  │
│  │has_huge_  │  │
│  │pages=true │  │
│  │has_nx_bit │  │
│  │=true      │  │
│  │...        │  │
│  └───────────┘  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Kernel Core    │
│  (VMM, Drivers) │
│                 │
│  if (caps.has_  │
│  huge_pages)    │
│    use_huge()   │
└─────────────────┘
```

### 页表项转换流程

```
Architecture-Independent          Architecture-Specific
        Flags                           Format

┌─────────────────┐              ┌─────────────────┐
│  PTE_PRESENT    │              │  i686:          │
│  PTE_WRITE      │  pgtable_    │  bit 0: P       │
│  PTE_USER       │  make_entry  │  bit 1: R/W     │
│  PTE_NOCACHE    │ ──────────►  │  bit 2: U/S     │
│  PTE_EXEC       │              │  bit 4: PCD     │
│  PTE_COW        │              │  bit 9: AVL(COW)│
└─────────────────┘              └─────────────────┘

                                 ┌─────────────────┐
                                 │  x86_64:        │
                                 │  bit 0: P       │
                                 │  bit 1: R/W     │
                                 │  bit 2: U/S     │
                                 │  bit 4: PCD     │
                                 │  bit 63: NX     │
                                 │  bit 9: AVL(COW)│
                                 └─────────────────┘

                                 ┌─────────────────┐
                                 │  ARM64:         │
                                 │  bit 0: Valid   │
                                 │  bit 6: AP[1]   │
                                 │  bit 7: AP[2]   │
                                 │  bit 53: PXN    │
                                 │  bit 54: UXN    │
                                 │  bit 55: SW(COW)│
                                 └─────────────────┘
```

### 平台设备匹配流程

```
┌─────────────────┐     ┌─────────────────┐
│  PCI Enumeration│     │  DTB Parsing    │
│  (x86)          │     │  (ARM64)        │
└────────┬────────┘     └────────┬────────┘
         │                       │
         ▼                       ▼
┌─────────────────────────────────────────┐
│           platform_device_t              │
│  ┌─────────────────────────────────┐    │
│  │ name: "e1000"                   │    │
│  │ resources: [MEM: 0xF0000000,    │    │
│  │             IRQ: 11]            │    │
│  │ source: PLATFORM_SRC_PCI        │    │
│  │ pci: {vendor=0x8086,            │    │
│  │       device=0x100E}            │    │
│  └─────────────────────────────────┘    │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│           Driver Matching                │
│  for each registered platform_driver:   │
│    if (match_pci_ids() ||               │
│        match_compatible())              │
│      driver->probe(dev)                 │
└─────────────────────────────────────────┘
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: 页表项往返一致性 (Round-Trip Consistency)

*For any* valid physical address and flag combination, creating a page table entry with `pgtable_make_entry()` and then extracting the physical address and flags with `pgtable_get_phys()` and `pgtable_get_flags()` should return the original values.

**Validates: Requirements 3.1, 3.2**

### Property 2: HAL 能力查询一致性

*For any* architecture, the values returned by `hal_get_capabilities()` should be consistent with the architecture's actual hardware capabilities and match the values defined in the architecture-specific headers.

**Validates: Requirements 1.1, 1.3**

### Property 3: 上下文初始化正确性

*For any* entry point address, stack pointer, and privilege level, `hal_context_init()` should produce a context that, when switched to, begins execution at the specified entry point with the correct stack and privilege level.

**Validates: Requirements 2.3**

### Property 4: COW 页面复制正确性

*For any* page marked as COW, when a write fault occurs, the VMM should allocate a new physical page, copy the original content, and update the mapping so that subsequent reads return the same data as before the fault.

**Validates: Requirements 4.1**

### Property 5: 逻辑 IRQ 映射有效性

*For any* logical IRQ type that is supported on the current architecture, `hal_irq_get_number()` should return a valid physical IRQ number that can be used with the interrupt controller.

**Validates: Requirements 5.1**

### Property 6: 平台设备资源完整性

*For any* device discovered via PCI or Device Tree, the created `platform_device_t` should contain all MMIO regions and IRQ numbers that the device requires for operation.

**Validates: Requirements 6.2, 6.3**

### Property 7: 系统调用参数往返一致性

*For any* system call invocation, the arguments extracted by `hal_syscall_get_args()` should match the arguments passed by the user program, and the return value set by `hal_syscall_set_return()` should be received by the user program.

**Validates: Requirements 7.1, 7.2**

### Property 8: 启动信息内存映射有效性

*For any* boot process, the `boot_info_t.mmap` should contain entries that cover all usable physical memory, and the sum of usable memory should match `mem_upper`.

**Validates: Requirements 8.1**

### Property 9: 页表映射一致性

*For any* sequence of `hal_mmu_map()` and `hal_mmu_unmap()` operations, `hal_mmu_query()` should return results consistent with the current state of the page tables.

**Validates: Requirements 9.3**

### Property 10: HAL 错误码一致性

*For any* HAL function that can fail, the function should return one of the defined `hal_error_t` values, and the same error condition should produce the same error code across all architectures.

**Validates: Requirements 12.1, 12.2, 12.4**


## Error Handling

### 错误处理策略

1. **HAL 函数错误返回**
   - 所有可能失败的 HAL 函数返回 `hal_error_t`
   - 成功返回 `HAL_OK`，失败返回具体错误码
   - 调用者必须检查返回值

2. **不支持的操作**
   - 当架构不支持某操作时，返回 `HAL_ERR_NOT_SUPPORTED`
   - 调用者可以通过能力查询预先检查支持情况

3. **资源分配失败**
   - 内存分配失败返回 `HAL_ERR_NO_MEMORY`
   - 调用者负责清理已分配的资源

4. **参数验证**
   - 无效参数返回 `HAL_ERR_INVALID_PARAM`
   - 在 DEBUG 模式下记录详细错误信息

### 错误处理示例

```c
hal_error_t err;

/* 检查能力 */
hal_capabilities_t caps;
hal_get_capabilities(&caps);

if (!caps.has_huge_pages) {
    /* 回退到普通页 */
    err = hal_mmu_map(space, virt, phys, flags);
} else {
    err = hal_mmu_map_huge(space, virt, phys, flags);
    if (err == HAL_ERR_NOT_SUPPORTED) {
        /* 回退到普通页 */
        err = hal_mmu_map(space, virt, phys, flags);
    }
}

if (HAL_FAILED(err)) {
    LOG_ERROR("Failed to map page: %d\n", err);
    return err;
}
```

## Testing Strategy

### 双重测试方法

本设计采用单元测试和属性测试相结合的方法：

- **单元测试**：验证特定示例和边界条件
- **属性测试**：验证跨所有输入的通用属性

### 属性测试框架

使用 **theft** (C 语言的属性测试库) 进行属性测试。每个属性测试配置为运行至少 100 次迭代。

### 测试用例

#### 单元测试

1. **HAL 能力查询测试**
   - 验证 `hal_arch_name()` 返回正确的架构名称
   - 验证 `hal_context_size()` 返回正确的大小
   - 验证已知不支持的特性返回 false

2. **页表抽象测试**
   - 验证各种标志组合的转换正确性
   - 验证边界物理地址的处理

3. **平台设备测试**
   - 验证 PCI 设备发现和资源提取
   - 验证 DTB 设备发现和资源提取

#### 属性测试

每个属性测试必须标注对应的正确性属性：

```c
/**
 * **Feature: multi-arch-optimization, Property 1: 页表项往返一致性**
 * **Validates: Requirements 3.1, 3.2**
 */
static enum theft_trial_res
prop_pte_roundtrip(struct theft *t, void *arg1, void *arg2) {
    paddr_t phys = *(paddr_t *)arg1;
    uint32_t flags = *(uint32_t *)arg2;
    
    /* 过滤无效输入 */
    if (!IS_PADDR_ALIGNED(phys) || phys > PHYS_ADDR_MAX) {
        return THEFT_TRIAL_SKIP;
    }
    flags &= (PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NOCACHE | PTE_EXEC | PTE_COW);
    
    /* 创建页表项 */
    pte_t entry = pgtable_make_entry(phys, flags);
    
    /* 提取并验证 */
    paddr_t extracted_phys = pgtable_get_phys(entry);
    uint32_t extracted_flags = pgtable_get_flags(entry);
    
    if (extracted_phys != phys || extracted_flags != flags) {
        return THEFT_TRIAL_FAIL;
    }
    return THEFT_TRIAL_PASS;
}
```

### 架构特定测试

每个架构实现必须通过以下测试：

1. **HAL 接口完整性测试**
   - 所有 HAL 函数都有实现（即使是 stub）
   - 能力查询返回有效值

2. **内存管理测试**
   - 页表映射/取消映射正确
   - COW 机制正常工作
   - TLB 刷新有效

3. **中断测试**
   - 中断注册和触发正确
   - EOI 信号正确发送

4. **上下文切换测试**
   - 上下文保存/恢复完整
   - 特权级切换正确

## Implementation Notes

### 迁移策略

1. **阶段 1：添加新接口**
   - 添加 `hal_caps.h`、`pgtable.h`、`hal_error.h`
   - 实现各架构的能力查询
   - 不修改现有代码

2. **阶段 2：逐步迁移**
   - 将 VMM 中的条件编译替换为 HAL 调用
   - 将驱动中的直接 IRQ 号替换为逻辑 IRQ
   - 添加平台设备模型

3. **阶段 3：清理**
   - 移除不再需要的条件编译
   - 统一错误处理
   - 完善文档

### 向后兼容性

- 现有 HAL 接口保持不变
- 新接口作为补充而非替代
- 逐步废弃旧接口，提供迁移指南

