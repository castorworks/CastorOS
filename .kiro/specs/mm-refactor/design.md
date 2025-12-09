# Design Document: 内存管理子系统重构

## Overview

本设计文档描述了 CastorOS 内存管理子系统的重构方案。通过分析 Linux、FreeBSD、Xv6 等业界方案，结合 CastorOS 作为教学 OS 的特性，设计一个简洁、可扩展的跨架构内存管理系统。

### 业界方案分析

| 方案 | 优点 | 缺点 | 适用场景 |
|------|------|------|---------|
| **Linux pmap** | 功能完整，支持 NUMA/大页 | 复杂度高，宏嵌套深 | 生产系统 |
| **FreeBSD vm** | 模块化好，MI/MD 分离清晰 | 代码量大 | 生产系统 |
| **Xv6** | 简洁易懂 | 功能有限，仅 RISC-V | 教学 |
| **seL4** | 形式化验证 | 学习曲线陡峭 | 安全关键 |

### CastorOS 设计原则

1. **简洁优先** - 代码易读易懂，适合教学
2. **类型安全** - 使用专用类型防止地址混淆
3. **分层清晰** - 通用层 → HAL 层 → 架构层
4. **渐进迁移** - 保持 API 兼容，逐步重构



## Architecture

### 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Kernel Subsystems                                    │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐               │
│  │  Task   │ │   VFS   │ │ Drivers │ │   Net   │ │  Heap   │               │
│  │ Manager │ │  Layer  │ │         │ │  Stack  │ │ (slab)  │               │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘               │
│       │           │           │           │           │                     │
├───────┴───────────┴───────────┴───────────┴───────────┴─────────────────────┤
│                    Memory Management Common Layer                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                         VMM (vmm.c)                                  │    │
│  │  - Address space management                                          │    │
│  │  - Page fault handling (COW, demand paging)                         │    │
│  │  - MMIO mapping                                                      │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                         PMM (pmm.c)                                  │    │
│  │  - Physical frame allocation (bitmap)                                │    │
│  │  - Reference counting (COW support)                                  │    │
│  │  - Zone management (DMA/Normal/High)                                │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────────────────┤
│                    HAL MMU Interface (hal/mmu.h)                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  hal_mmu_map()  hal_mmu_unmap()  hal_mmu_create_space()             │    │
│  │  hal_mmu_clone_space()  hal_mmu_switch_space()  hal_mmu_walk()      │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────────────────┤
│                    Architecture-Specific Implementations                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                          │
│  │   i686      │  │   x86_64    │  │   arm64     │                          │
│  │  paging.c   │  │  paging64.c │  │   mmu.c     │                          │
│  │  2-level PT │  │  4-level PT │  │  4-level TT │                          │
│  └─────────────┘  └─────────────┘  └─────────────┘                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 目录结构

```
src/
├── include/
│   ├── mm/
│   │   ├── types.h          # paddr_t, vaddr_t, pfn_t 定义
│   │   ├── pmm.h            # PMM 接口（使用 paddr_t）
│   │   ├── vmm.h            # VMM 接口（架构无关）
│   │   └── pgtable.h        # 页表抽象宏
│   └── hal/
│       └── mmu.h            # HAL MMU 接口
├── mm/
│   ├── pmm.c                # PMM 实现（通用）
│   ├── vmm.c                # VMM 实现（通用）
│   └── pgtable.c            # 页表辅助函数
└── arch/
    ├── i686/mm/
    │   └── paging.c         # i686 页表操作
    ├── x86_64/mm/
    │   └── paging64.c       # x86_64 页表操作
    └── arm64/mm/
        └── mmu.c            # ARM64 页表操作
```



## Components and Interfaces

### 1. 地址类型系统 (mm/types.h)

```c
/**
 * @file mm/types.h
 * @brief 内存管理类型定义
 * 
 * 提供类型安全的物理/虚拟地址类型，防止地址空间混淆
 */

#ifndef _MM_TYPES_H_
#define _MM_TYPES_H_

#include <types.h>

/*============================================================================
 * 物理地址类型 - 所有架构统一使用 64-bit
 *============================================================================*/

/** 物理地址类型 (64-bit on all architectures) */
typedef uint64_t paddr_t;

/** 页帧号类型 (physical page index) */
typedef uint64_t pfn_t;

/** 无效物理地址 */
#define PADDR_INVALID   ((paddr_t)-1)

/** 无效页帧号 */
#define PFN_INVALID     ((pfn_t)-1)

/*============================================================================
 * 虚拟地址类型 - 架构相关
 *============================================================================*/

/** 虚拟地址类型 (matches pointer size) */
typedef uintptr_t vaddr_t;

/** 无效虚拟地址 */
#define VADDR_INVALID   ((vaddr_t)-1)

/*============================================================================
 * 地址转换宏
 *============================================================================*/

/** 物理地址转页帧号 */
#define PADDR_TO_PFN(pa)    ((pfn_t)((pa) >> PAGE_SHIFT))

/** 页帧号转物理地址 */
#define PFN_TO_PADDR(pfn)   ((paddr_t)((pfn) << PAGE_SHIFT))

/** 物理地址转内核虚拟地址（仅限直接映射区域） */
#define PADDR_TO_KVADDR(pa) ((vaddr_t)((pa) + KERNEL_VIRTUAL_BASE))

/** 内核虚拟地址转物理地址（仅限直接映射区域） */
#define KVADDR_TO_PADDR(va) ((paddr_t)((va) - KERNEL_VIRTUAL_BASE))

/** 页对齐（向下） */
#define PADDR_ALIGN_DOWN(pa) ((pa) & ~((paddr_t)PAGE_SIZE - 1))

/** 页对齐（向上） */
#define PADDR_ALIGN_UP(pa)   (((pa) + PAGE_SIZE - 1) & ~((paddr_t)PAGE_SIZE - 1))

/*============================================================================
 * 架构相关常量
 *============================================================================*/

#if defined(ARCH_X86_64)
    #define PHYS_ADDR_BITS      52      /* x86_64 物理地址位数 */
    #define VIRT_ADDR_BITS      48      /* x86_64 虚拟地址位数 */
    #define PAGE_TABLE_LEVELS   4
#elif defined(ARCH_ARM64)
    #define PHYS_ADDR_BITS      48      /* ARM64 物理地址位数 */
    #define VIRT_ADDR_BITS      48      /* ARM64 虚拟地址位数 */
    #define PAGE_TABLE_LEVELS   4
#else /* i686 */
    #define PHYS_ADDR_BITS      32      /* i686 物理地址位数 */
    #define VIRT_ADDR_BITS      32      /* i686 虚拟地址位数 */
    #define PAGE_TABLE_LEVELS   2
#endif

/** 最大物理地址 */
#define PHYS_ADDR_MAX   ((paddr_t)1 << PHYS_ADDR_BITS)

#endif /* _MM_TYPES_H_ */
```



### 2. PMM 接口重构 (mm/pmm.h)

```c
/**
 * @file pmm.h
 * @brief 物理内存管理器接口（重构版）
 */

#ifndef _MM_PMM_H_
#define _MM_PMM_H_

#include <mm/types.h>

/*============================================================================
 * 内存区域定义
 *============================================================================*/

/** 内存区域类型 */
typedef enum {
    ZONE_DMA,       /**< DMA 区域 (0-16MB on x86) */
    ZONE_NORMAL,    /**< 普通区域 */
    ZONE_HIGH,      /**< 高端内存 (>896MB on i686) */
    ZONE_COUNT
} pmm_zone_t;

/*============================================================================
 * PMM 信息结构
 *============================================================================*/

typedef struct {
    pfn_t total_frames;     /**< 总页帧数 */
    pfn_t free_frames;      /**< 空闲页帧数 */
    pfn_t used_frames;      /**< 已使用页帧数 */
    pfn_t reserved_frames;  /**< 保留页帧数 */
} pmm_info_t;

/*============================================================================
 * PMM 核心接口
 *============================================================================*/

/**
 * @brief 初始化物理内存管理器
 * @param mbi Multiboot 信息（i686/x86_64）或 DTB（ARM64）
 */
void pmm_init(void *boot_info);

/**
 * @brief 分配一个物理页帧
 * @return 成功返回物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_frame(void);

/**
 * @brief 从指定区域分配物理页帧
 * @param zone 内存区域
 * @return 成功返回物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_frame_zone(pmm_zone_t zone);

/**
 * @brief 分配连续物理页帧（用于 DMA）
 * @param count 页帧数量
 * @return 成功返回起始物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_frames(size_t count);

/**
 * @brief 释放物理页帧
 * @param frame 物理地址
 */
void pmm_free_frame(paddr_t frame);

/**
 * @brief 释放连续物理页帧
 * @param frame 起始物理地址
 * @param count 页帧数量
 */
void pmm_free_frames(paddr_t frame, size_t count);

/*============================================================================
 * 引用计数接口（COW 支持）
 *============================================================================*/

/**
 * @brief 增加页帧引用计数
 * @param frame 物理地址
 * @return 新的引用计数
 */
uint32_t pmm_frame_ref_inc(paddr_t frame);

/**
 * @brief 减少页帧引用计数
 * @param frame 物理地址
 * @return 新的引用计数
 */
uint32_t pmm_frame_ref_dec(paddr_t frame);

/**
 * @brief 获取页帧引用计数
 * @param frame 物理地址
 * @return 引用计数
 */
uint32_t pmm_frame_get_refcount(paddr_t frame);

/*============================================================================
 * 信息查询接口
 *============================================================================*/

pmm_info_t pmm_get_info(void);
void pmm_print_info(void);

#endif /* _MM_PMM_H_ */
```



### 3. HAL MMU 接口扩展 (hal/mmu.h)

```c
/**
 * @file hal/mmu.h
 * @brief HAL MMU 接口（扩展版）
 * 
 * 提供架构无关的页表操作接口
 */

#ifndef _HAL_MMU_H_
#define _HAL_MMU_H_

#include <mm/types.h>

/*============================================================================
 * 页表项标志（架构无关）
 *============================================================================*/

#define HAL_PTE_PRESENT     (1 << 0)    /**< 页存在 */
#define HAL_PTE_WRITE       (1 << 1)    /**< 可写 */
#define HAL_PTE_USER        (1 << 2)    /**< 用户可访问 */
#define HAL_PTE_NOCACHE     (1 << 3)    /**< 禁用缓存 */
#define HAL_PTE_WRITECOMB   (1 << 4)    /**< 写合并 */
#define HAL_PTE_EXEC        (1 << 5)    /**< 可执行 */
#define HAL_PTE_COW         (1 << 6)    /**< Copy-on-Write */
#define HAL_PTE_DIRTY       (1 << 7)    /**< 已修改 */
#define HAL_PTE_ACCESSED    (1 << 8)    /**< 已访问 */

/*============================================================================
 * 页错误信息结构
 *============================================================================*/

typedef struct {
    vaddr_t fault_addr;     /**< 错误地址 */
    bool is_present;        /**< 页是否存在 */
    bool is_write;          /**< 是否写操作 */
    bool is_user;           /**< 是否用户模式 */
    bool is_exec;           /**< 是否执行操作 */
    uint32_t raw_error;     /**< 原始错误码 */
} hal_page_fault_info_t;

/*============================================================================
 * 地址空间句柄
 *============================================================================*/

/** 地址空间句柄（物理地址，指向顶级页表） */
typedef paddr_t hal_addr_space_t;

#define HAL_ADDR_SPACE_INVALID  PADDR_INVALID

/*============================================================================
 * HAL MMU 核心接口
 *============================================================================*/

/**
 * @brief 初始化 MMU
 */
void hal_mmu_init(void);

/**
 * @brief 映射虚拟页到物理页
 * @param space 地址空间（0 表示当前）
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 * @return 成功返回 true
 */
bool hal_mmu_map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags);

/**
 * @brief 取消虚拟页映射
 * @param space 地址空间（0 表示当前）
 * @param virt 虚拟地址
 * @return 原物理地址，未映射返回 PADDR_INVALID
 */
paddr_t hal_mmu_unmap(hal_addr_space_t space, vaddr_t virt);

/**
 * @brief 查询虚拟地址映射
 * @param space 地址空间（0 表示当前）
 * @param virt 虚拟地址
 * @param[out] phys 物理地址
 * @param[out] flags 页标志
 * @return 成功返回 true
 */
bool hal_mmu_query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags);

/**
 * @brief 修改页表项标志
 * @param space 地址空间
 * @param virt 虚拟地址
 * @param set_flags 要设置的标志
 * @param clear_flags 要清除的标志
 * @return 成功返回 true
 */
bool hal_mmu_protect(hal_addr_space_t space, vaddr_t virt, 
                     uint32_t set_flags, uint32_t clear_flags);

/*============================================================================
 * 地址空间管理
 *============================================================================*/

/**
 * @brief 创建新地址空间
 * @return 地址空间句柄，失败返回 HAL_ADDR_SPACE_INVALID
 */
hal_addr_space_t hal_mmu_create_space(void);

/**
 * @brief 克隆地址空间（COW 语义）
 * @param src 源地址空间
 * @return 新地址空间句柄
 */
hal_addr_space_t hal_mmu_clone_space(hal_addr_space_t src);

/**
 * @brief 销毁地址空间
 * @param space 地址空间句柄
 */
void hal_mmu_destroy_space(hal_addr_space_t space);

/**
 * @brief 切换当前地址空间
 * @param space 目标地址空间
 */
void hal_mmu_switch_space(hal_addr_space_t space);

/**
 * @brief 获取当前地址空间
 * @return 当前地址空间句柄
 */
hal_addr_space_t hal_mmu_current_space(void);

/*============================================================================
 * TLB 管理
 *============================================================================*/

void hal_mmu_flush_tlb(vaddr_t virt);
void hal_mmu_flush_tlb_all(void);

/*============================================================================
 * 页错误处理
 *============================================================================*/

/**
 * @brief 解析页错误信息
 * @param[out] info 页错误信息结构
 */
void hal_mmu_parse_fault(hal_page_fault_info_t *info);

#endif /* _HAL_MMU_H_ */
```



### 4. 页表抽象层 (mm/pgtable.h)

```c
/**
 * @file pgtable.h
 * @brief 页表抽象宏
 * 
 * 提供架构无关的页表操作宏，隐藏不同架构的页表级数差异
 */

#ifndef _MM_PGTABLE_H_
#define _MM_PGTABLE_H_

#include <mm/types.h>

/*============================================================================
 * 页表项类型（架构相关）
 *============================================================================*/

#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
    typedef uint64_t pte_t;
    typedef uint64_t pde_t;
    #define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL
#else /* i686 */
    typedef uint32_t pte_t;
    typedef uint32_t pde_t;
    #define PTE_ADDR_MASK   0xFFFFF000UL
#endif

/*============================================================================
 * 页表项操作宏
 *============================================================================*/

/** 从页表项提取物理地址 */
#define PTE_ADDR(pte)       ((paddr_t)((pte) & PTE_ADDR_MASK))

/** 从页表项提取标志 */
#define PTE_FLAGS(pte)      ((uint32_t)((pte) & ~PTE_ADDR_MASK))

/** 构造页表项 */
#define MAKE_PTE(addr, flags) ((pte_t)(((paddr_t)(addr) & PTE_ADDR_MASK) | (flags)))

/** 检查页表项是否存在 */
#define PTE_PRESENT(pte)    (((pte) & HAL_PTE_PRESENT) != 0)

/*============================================================================
 * 虚拟地址分解（架构相关）
 *============================================================================*/

#if defined(ARCH_X86_64)
    /* x86_64: 4 级页表，每级 9 位索引 */
    #define VA_PML4_SHIFT   39
    #define VA_PDPT_SHIFT   30
    #define VA_PD_SHIFT     21
    #define VA_PT_SHIFT     12
    #define VA_INDEX_MASK   0x1FF   /* 9 bits */
    
    #define VA_PML4_INDEX(va)   (((va) >> VA_PML4_SHIFT) & VA_INDEX_MASK)
    #define VA_PDPT_INDEX(va)   (((va) >> VA_PDPT_SHIFT) & VA_INDEX_MASK)
    #define VA_PD_INDEX(va)     (((va) >> VA_PD_SHIFT) & VA_INDEX_MASK)
    #define VA_PT_INDEX(va)     (((va) >> VA_PT_SHIFT) & VA_INDEX_MASK)

#elif defined(ARCH_ARM64)
    /* ARM64: 4 级页表，4KB granule */
    #define VA_L0_SHIFT     39
    #define VA_L1_SHIFT     30
    #define VA_L2_SHIFT     21
    #define VA_L3_SHIFT     12
    #define VA_INDEX_MASK   0x1FF   /* 9 bits */
    
    #define VA_L0_INDEX(va)     (((va) >> VA_L0_SHIFT) & VA_INDEX_MASK)
    #define VA_L1_INDEX(va)     (((va) >> VA_L1_SHIFT) & VA_INDEX_MASK)
    #define VA_L2_INDEX(va)     (((va) >> VA_L2_SHIFT) & VA_INDEX_MASK)
    #define VA_L3_INDEX(va)     (((va) >> VA_L3_SHIFT) & VA_INDEX_MASK)

#else /* i686 */
    /* i686: 2 级页表，每级 10 位索引 */
    #define VA_PD_SHIFT     22
    #define VA_PT_SHIFT     12
    #define VA_PD_MASK      0x3FF   /* 10 bits */
    #define VA_PT_MASK      0x3FF   /* 10 bits */
    
    #define VA_PD_INDEX(va)     (((va) >> VA_PD_SHIFT) & VA_PD_MASK)
    #define VA_PT_INDEX(va)     (((va) >> VA_PT_SHIFT) & VA_PT_MASK)
#endif

/*============================================================================
 * 页表遍历回调
 *============================================================================*/

/**
 * @brief 页表遍历回调函数类型
 * @param virt 虚拟地址
 * @param pte 页表项指针
 * @param level 页表级别（0=最低级）
 * @param data 用户数据
 * @return 0 继续遍历，非 0 停止
 */
typedef int (*pgtable_walk_fn)(vaddr_t virt, pte_t *pte, int level, void *data);

#endif /* _MM_PGTABLE_H_ */
```



## Data Models

### 地址空间布局

```
i686 (32-bit, 2-level paging):
┌──────────────────────────────────────┐ 0xFFFFFFFF
│         Kernel Space (2GB)           │
│  - Direct mapping of physical memory │
│  - Kernel code/data                  │
│  - Kernel heap                       │
│  - MMIO mappings                     │
├──────────────────────────────────────┤ 0x80000000
│         User Space (2GB)             │
│  - User code/data                    │
│  - User heap (grows up)              │
│  - User stack (grows down)           │
│  - mmap regions                      │
└──────────────────────────────────────┘ 0x00000000

x86_64 (64-bit, 4-level paging):
┌──────────────────────────────────────┐ 0xFFFFFFFFFFFFFFFF
│         Kernel Space                 │
│  0xFFFF800000000000 - Direct map     │
│  0xFFFFFFFF80000000 - Kernel image   │
├──────────────────────────────────────┤ 0xFFFF800000000000
│         Non-canonical hole           │
├──────────────────────────────────────┤ 0x00007FFFFFFFFFFF
│         User Space (128TB)           │
│  - User code/data                    │
│  - User heap/stack                   │
└──────────────────────────────────────┘ 0x0000000000000000

ARM64 (64-bit, 4-level translation):
┌──────────────────────────────────────┐ 0xFFFFFFFFFFFFFFFF
│         Kernel Space (TTBR1_EL1)     │
│  0xFFFF000000000000 - Direct map     │
├──────────────────────────────────────┤ 0xFFFF000000000000
│         Non-canonical hole           │
├──────────────────────────────────────┤ 0x0000FFFFFFFFFFFF
│         User Space (TTBR0_EL1)       │
└──────────────────────────────────────┘ 0x0000000000000000
```

### PMM 数据结构

```c
/* 物理内存位图 */
typedef struct {
    uint64_t *bitmap;           /* 分配位图 */
    uint16_t *refcount;         /* 引用计数数组 */
    pfn_t total_frames;         /* 总页帧数 */
    pfn_t free_frames;          /* 空闲页帧数 */
    pfn_t search_hint;          /* 搜索起始位置 */
    spinlock_t lock;            /* 保护锁 */
} pmm_state_t;

/* 内存区域描述 */
typedef struct {
    paddr_t base;               /* 区域起始地址 */
    paddr_t end;                /* 区域结束地址 */
    pfn_t free_frames;          /* 区域空闲帧数 */
} pmm_zone_info_t;
```

### 页表结构对比

```
i686 (2-level, 32-bit entries):
┌─────────────────┐
│  Page Directory │ 1024 × 4B = 4KB
│    (CR3)        │
└────────┬────────┘
         │ PDE[0..1023]
         ▼
┌─────────────────┐
│   Page Table    │ 1024 × 4B = 4KB
└─────────────────┘

x86_64 (4-level, 64-bit entries):
┌─────────────────┐
│      PML4       │ 512 × 8B = 4KB
│    (CR3)        │
└────────┬────────┘
         ▼
┌─────────────────┐
│      PDPT       │ 512 × 8B = 4KB
└────────┬────────┘
         ▼
┌─────────────────┐
│       PD        │ 512 × 8B = 4KB
└────────┬────────┘
         ▼
┌─────────────────┐
│       PT        │ 512 × 8B = 4KB
└─────────────────┘

ARM64 (4-level, 64-bit descriptors):
┌─────────────────┐
│     Level 0     │ 512 × 8B = 4KB
│   (TTBR0/1)     │
└────────┬────────┘
         ▼
┌─────────────────┐
│     Level 1     │ 512 × 8B = 4KB
└────────┬────────┘
         ▼
┌─────────────────┐
│     Level 2     │ 512 × 8B = 4KB
└────────┬────────┘
         ▼
┌─────────────────┐
│     Level 3     │ 512 × 8B = 4KB
└─────────────────┘
```



## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Physical Address Type Size

*For any* architecture, `sizeof(paddr_t)` SHALL equal 8 bytes (64-bit).

**Validates: Requirements 1.1**

### Property 2: Virtual Address Type Size

*For any* architecture, `sizeof(vaddr_t)` SHALL equal `sizeof(void*)`.

**Validates: Requirements 1.2**

### Property 3: PFN Conversion Round-Trip

*For any* valid page frame number `pfn`, `PADDR_TO_PFN(PFN_TO_PADDR(pfn))` SHALL equal `pfn`.

**Validates: Requirements 1.5**

### Property 4: Page Alignment Round-Trip

*For any* physical address `pa`, `PADDR_ALIGN_DOWN(pa)` SHALL be less than or equal to `pa`, and `PADDR_ALIGN_UP(pa)` SHALL be greater than or equal to `pa`.

**Validates: Requirements 1.5**

### Property 5: PMM Allocation Returns Page-Aligned Address

*For any* successful call to `pmm_alloc_frame()`, the returned address SHALL be page-aligned (divisible by PAGE_SIZE).

**Validates: Requirements 2.1, 2.2**

### Property 6: PMM Reference Count Consistency

*For any* allocated frame, after `n` calls to `pmm_frame_ref_inc()` and `m` calls to `pmm_frame_ref_dec()` where `n >= m`, `pmm_frame_get_refcount()` SHALL return `1 + n - m`.

**Validates: Requirements 2.3**

### Property 7: PTE Construction Round-Trip

*For any* valid physical address `addr` and flags `f`, `PTE_ADDR(MAKE_PTE(addr, f))` SHALL equal `addr & PTE_ADDR_MASK`, and `PTE_FLAGS(MAKE_PTE(addr, f))` SHALL contain `f`.

**Validates: Requirements 3.3, 3.4**

### Property 8: HAL MMU Map-Query Round-Trip

*For any* valid virtual address `virt`, physical address `phys`, and flags `flags`, after `hal_mmu_map(space, virt, phys, flags)` succeeds, `hal_mmu_query(space, virt, &out_phys, &out_flags)` SHALL return `true` with `out_phys == phys`.

**Validates: Requirements 4.1**

### Property 9: Address Space Switch Consistency

*For any* valid address space `space`, after `hal_mmu_switch_space(space)`, `hal_mmu_current_space()` SHALL return `space`.

**Validates: Requirements 4.5**

### Property 10: COW Clone Shares Physical Pages

*For any* address space with mapped user pages, after `hal_mmu_clone_space()`, both parent and child SHALL map the same virtual addresses to the same physical addresses (until write occurs).

**Validates: Requirements 4.4, 5.3**

### Property 11: COW Write Triggers Copy

*For any* COW-marked page, when a write fault occurs and is handled, the faulting process SHALL have a private copy of the page with write permission restored.

**Validates: Requirements 4.4, 5.3**

### Property 12: Kernel Space Shared Across Address Spaces

*For any* two address spaces, kernel virtual addresses SHALL map to the same physical addresses.

**Validates: Requirements 7.2**

### Property 13: User Mapping Has User Flag

*For any* mapping in user address space (below KERNEL_VIRTUAL_BASE), the page table entry SHALL have HAL_PTE_USER flag set.

**Validates: Requirements 7.3**

### Property 14: MMIO Mapping Has No-Cache Flag

*For any* MMIO mapping created via `vmm_map_mmio()`, the page table entry SHALL have HAL_PTE_NOCACHE flag set.

**Validates: Requirements 9.1**

### Property 15: Address Space Destruction Frees Memory

*For any* address space, after `hal_mmu_destroy_space()`, the PMM free frame count SHALL increase by the number of page table frames used.

**Validates: Requirements 5.5**



## Error Handling

### PMM 错误处理

```c
/* 分配失败 */
paddr_t frame = pmm_alloc_frame();
if (frame == PADDR_INVALID) {
    // 内存不足，返回错误或触发 OOM
    return -ENOMEM;
}

/* 无效帧释放 */
void pmm_free_frame(paddr_t frame) {
    if (frame == PADDR_INVALID) return;
    if (!IS_PAGE_ALIGNED(frame)) {
        LOG_WARN("PMM: Unaligned frame 0x%llx", frame);
        return;
    }
    pfn_t pfn = PADDR_TO_PFN(frame);
    if (pfn >= total_frames) {
        LOG_ERROR("PMM: Frame 0x%llx out of range", frame);
        return;
    }
    // ... 正常释放逻辑
}
```

### VMM 错误处理

```c
/* 映射失败 */
bool hal_mmu_map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    // 参数验证
    if (!IS_PAGE_ALIGNED(virt) || !IS_PAGE_ALIGNED(phys)) {
        return false;
    }
    
    // 分配页表失败
    paddr_t pt_frame = pmm_alloc_frame();
    if (pt_frame == PADDR_INVALID) {
        return false;
    }
    
    // ... 映射逻辑
    return true;
}

/* 页错误处理 */
void vmm_handle_page_fault(hal_page_fault_info_t *info) {
    // 1. 检查是否为 COW 错误
    if (info->is_present && info->is_write) {
        if (vmm_handle_cow_fault(info->fault_addr)) {
            return;  // COW 处理成功
        }
    }
    
    // 2. 检查是否为内核空间同步
    if (info->fault_addr >= KERNEL_VIRTUAL_BASE) {
        if (vmm_sync_kernel_mapping(info->fault_addr)) {
            return;
        }
    }
    
    // 3. 无法处理的错误
    panic("Unhandled page fault at 0x%lx (error=0x%x)", 
          info->fault_addr, info->raw_error);
}
```

## Testing Strategy

### 测试框架

使用 CastorOS 内置的 ktest 框架进行单元测试，使用轻量级属性测试框架进行属性验证。

### 属性测试库

```c
// src/tests/pbt/pbt.h
#define PBT_ITERATIONS 100

typedef struct {
    uint64_t seed;
    uint32_t iteration;
} pbt_state_t;

uint64_t pbt_random(pbt_state_t *state);
uint64_t pbt_random_range(pbt_state_t *state, uint64_t min, uint64_t max);
paddr_t pbt_random_paddr(pbt_state_t *state);
vaddr_t pbt_random_user_vaddr(pbt_state_t *state);
```

### 测试用例设计

**类型系统测试**:
```c
void test_paddr_size(void) {
    ASSERT_EQ(sizeof(paddr_t), 8);
}

void test_vaddr_size(void) {
    ASSERT_EQ(sizeof(vaddr_t), sizeof(void*));
}

void test_pfn_roundtrip(void) {
    for (pfn_t pfn = 0; pfn < 1000; pfn++) {
        ASSERT_EQ(PADDR_TO_PFN(PFN_TO_PADDR(pfn)), pfn);
    }
}
```

**PMM 测试**:
```c
void test_pmm_alloc_aligned(void) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_NE(frame, PADDR_INVALID);
    ASSERT_EQ(frame & (PAGE_SIZE - 1), 0);
    pmm_free_frame(frame);
}

void test_pmm_refcount(void) {
    paddr_t frame = pmm_alloc_frame();
    ASSERT_EQ(pmm_frame_get_refcount(frame), 1);
    pmm_frame_ref_inc(frame);
    ASSERT_EQ(pmm_frame_get_refcount(frame), 2);
    pmm_frame_ref_dec(frame);
    ASSERT_EQ(pmm_frame_get_refcount(frame), 1);
    pmm_free_frame(frame);
}
```

**HAL MMU 测试**:
```c
void test_hal_mmu_map_query_roundtrip(void) {
    paddr_t phys = pmm_alloc_frame();
    vaddr_t virt = 0x10000000;  // User space
    
    ASSERT_TRUE(hal_mmu_map(0, virt, phys, HAL_PTE_PRESENT | HAL_PTE_WRITE | HAL_PTE_USER));
    
    paddr_t out_phys;
    uint32_t out_flags;
    ASSERT_TRUE(hal_mmu_query(0, virt, &out_phys, &out_flags));
    ASSERT_EQ(out_phys, phys);
    
    hal_mmu_unmap(0, virt);
    pmm_free_frame(phys);
}
```

### 多架构测试

```makefile
test-mm-all: test-mm-i686 test-mm-x86_64 test-mm-arm64

test-mm-i686:
	$(MAKE) ARCH=i686 test-mm

test-mm-x86_64:
	$(MAKE) ARCH=x86_64 test-mm

test-mm-arm64:
	$(MAKE) ARCH=arm64 test-mm
```

