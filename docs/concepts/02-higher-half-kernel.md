# 高半核架构

## 概述

CastorOS 采用高半核（Higher Half Kernel）设计，将内核映射到虚拟地址空间的上半部分（2GB-4GB），用户空间使用下半部分（0-2GB）。

## 地址空间布局

```
虚拟地址空间 (4GB)
+---------------------------+ 0xFFFFFFFF (4GB)
|                           |
|    内核保留区域            |  MMIO、帧缓冲等
|                           |
+---------------------------+ 0xF0000000
|                           |
|    内核堆空间              |  动态分配
|                           |
+---------------------------+ 
|                           |
|    内核代码和数据          |  物理内存的直接映射
|    (物理内存映射)          |  phys + 0x80000000 = virt
|                           |
+---------------------------+ 0x80000000 (2GB) = KERNEL_VIRTUAL_BASE
|                           |
|    用户栈                  |  向下增长
|                           |
+---------------------------+ 
|                           |
|    共享库、mmap            |
|                           |
+---------------------------+
|                           |
|    用户堆                  |  向上增长
|                           |
+---------------------------+
|    用户 BSS               |
|    用户数据段              |
|    用户代码段              |
+---------------------------+ 0x10000000 (用户程序加载地址)
|                           |
|    保留 (NULL 指针检测)    |
|                           |
+---------------------------+ 0x00000000
```

## 关键宏定义

```c
// mm/vmm.h
#define KERNEL_VIRTUAL_BASE  0x80000000  // 内核虚拟地址基址
#define PAGE_SIZE            0x1000      // 4KB
#define PAGE_MASK            0xFFFFF000

// 地址转换
#define VIRT_TO_PHYS(addr)   ((uint32_t)(addr) - KERNEL_VIRTUAL_BASE)
#define PHYS_TO_VIRT(addr)   ((uint32_t)(addr) + KERNEL_VIRTUAL_BASE)

// 页对齐
#define PAGE_ALIGN_DOWN(addr) ((addr) & PAGE_MASK)
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & PAGE_MASK)
```

## 为什么使用高半核？

### 优点

1. **简化内核访问用户空间**
   - 内核可以直接访问用户空间地址
   - 不需要复杂的地址转换

2. **共享内核映射**
   - 所有进程共享相同的内核页表
   - 减少上下文切换开销
   - 减少 TLB 刷新

3. **清晰的地址空间分离**
   - 用户空间和内核空间有明确边界
   - 便于权限检查

4. **简化链接脚本**
   - 内核符号地址固定
   - 便于调试

### 缺点

1. **用户空间减半**
   - 用户程序只能使用 2GB 地址空间
   - 对 32 位系统影响较大

2. **需要引导阶段设置**
   - 启动时需要额外的页表设置
   - 增加引导复杂性

## 页目录结构

x86 使用两级页表：
- **页目录 (Page Directory)**: 1024 个条目，每个映射 4MB
- **页表 (Page Table)**: 1024 个条目，每个映射 4KB

```
虚拟地址 (32位)
+----------+----------+------------+
| PD Index | PT Index |   Offset   |
|  10 bits |  10 bits |   12 bits  |
+----------+----------+------------+
    ↓           ↓           ↓
    页目录     页表       页内偏移
```

### 高半核映射

```c
// 页目录索引计算
#define pde_idx(virt)  ((virt) >> 22)          // 高 10 位
#define pte_idx(virt)  (((virt) >> 12) & 0x3FF) // 中间 10 位

// 高半核从 PDE 512 开始
// 0x80000000 >> 22 = 512
#define KERNEL_PDE_START  512
#define KERNEL_PDE_END    1024
```

### 内核页表共享

所有进程的页目录中，PDE 512-1023 指向相同的页表：

```c
uint32_t vmm_create_page_directory(void) {
    uint32_t dir_phys = pmm_alloc_frame();
    page_directory_t *new_dir = PHYS_TO_VIRT(dir_phys);
    
    // 清空用户空间部分
    memset(new_dir, 0, 512 * sizeof(uint32_t));
    
    // 复制内核空间映射（共享）
    page_directory_t *kernel_dir = boot_page_directory;
    for (int i = 512; i < 1024; i++) {
        new_dir->entries[i] = kernel_dir->entries[i];
    }
    
    return dir_phys;
}
```

## 物理内存映射

高半核设计中，物理内存被直接映射到虚拟地址空间：

```
物理地址          虚拟地址
0x00000000  →   0x80000000
0x00001000  →   0x80001000
...
0x7FFFFFFF  →   0xFFFFFFFF (最大 2GB)
```

这使得内核可以通过简单的偏移访问任何物理地址：

```c
void *phys_to_virt(uint32_t phys) {
    return (void *)(phys + KERNEL_VIRTUAL_BASE);
}

uint32_t virt_to_phys(void *virt) {
    return (uint32_t)virt - KERNEL_VIRTUAL_BASE;
}
```

## VMM 初始化

### 引导阶段（boot.asm）

引导时映射前 16MB 物理内存：

```asm
; PDE 512-515 映射前 16MB
boot_page_directory:
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003  ; PDE 0 (恒等)
    times 511 dd 0
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003  ; PDE 512
    dd (boot_page_table2 - KERNEL_VIRTUAL_BASE) + 0x003  ; PDE 513
    dd (boot_page_table3 - KERNEL_VIRTUAL_BASE) + 0x003  ; PDE 514
    dd (boot_page_table4 - KERNEL_VIRTUAL_BASE) + 0x003  ; PDE 515
```

### VMM 扩展（vmm_init）

内核启动后扩展映射以覆盖所有物理内存：

```c
void vmm_init(void) {
    pmm_info_t info = pmm_get_info();
    uint32_t max_phys = info.total_frames * PAGE_SIZE;
    
    // 从 PDE 516 开始扩展（引导已映射 512-515）
    uint32_t start_pde = 516;
    uint32_t end_pde = 512 + (max_phys >> 22);
    
    for (uint32_t pde = start_pde; pde < end_pde; pde++) {
        uint32_t table_phys = pmm_alloc_frame();
        page_table_t *table = PHYS_TO_VIRT(table_phys);
        
        // 填充页表，映射整个 4MB 区域
        uint32_t phys_base = (pde - 512) << 22;
        for (uint32_t pte = 0; pte < 1024; pte++) {
            table->entries[pte] = (phys_base + (pte << 12)) 
                                | PAGE_PRESENT | PAGE_WRITE;
        }
        
        current_dir->entries[pde] = table_phys 
                                  | PAGE_PRESENT | PAGE_WRITE;
    }
}
```

## 特殊内存区域

### 1. MMIO 区域

设备 MMIO 空间映射到 0xF0000000 以上：

```c
#define MMIO_VIRT_BASE  0xF0000000

uint32_t vmm_map_mmio(uint32_t phys, uint32_t size) {
    // 分配虚拟地址空间
    uint32_t virt = allocate_mmio_region(size);
    
    // 建立映射，使用不可缓存属性
    for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_map_page(virt + offset, phys + offset, 
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    }
    
    return virt;
}
```

### 2. 帧缓冲

图形帧缓冲使用 Write-Combining 模式：

```c
uint32_t vmm_map_framebuffer(uint32_t phys, uint32_t size) {
    uint32_t virt = allocate_mmio_region(size);
    
    // 使用 PAT 设置 Write-Combining
    for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_map_page_wc(virt + offset, phys + offset);
    }
    
    return virt;
}
```

## 注意事项

### 1. 保留内存必须映射

ACPI 表等保留内存也需要映射，不能只映射"可用"内存：

```c
// 错误：只映射可用内存
if (phys_addr < max_available_phys) {
    map_page(virt, phys);
}

// 正确：映射所有物理地址
map_page(virt, phys);
```

### 2. 2GB 物理内存限制

高半核只能直接映射 2GB 物理内存。超过 2GB 的内存需要使用 highmem 技术。

### 3. 内核栈和用户栈分离

每个任务有两个栈：
- **内核栈**: 在内核地址空间，用于系统调用和中断处理
- **用户栈**: 在用户地址空间，用于用户代码执行

### 4. TLB 管理

修改页表后需要刷新 TLB：

```c
static inline void vmm_flush_tlb(uint32_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

// 刷新整个 TLB
static inline void vmm_flush_all(void) {
    __asm__ volatile (
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        ::: "eax", "memory"
    );
}
```

