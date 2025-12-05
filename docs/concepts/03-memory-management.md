# 内存管理

## 概述

CastorOS 内存管理分为三个层次：
1. **PMM (Physical Memory Manager)**: 管理物理页帧
2. **VMM (Virtual Memory Manager)**: 管理虚拟地址空间和页表
3. **堆分配器 (Heap)**: 提供任意大小的内存分配

```
+------------------+
|   kmalloc/kfree  |  ← 应用层接口
+------------------+
|      Heap        |  ← 堆分配器
+------------------+
|      VMM         |  ← 虚拟内存管理
+------------------+
|      PMM         |  ← 物理内存管理
+------------------+
|    Hardware      |  ← 物理内存
+------------------+
```

## 物理内存管理 (PMM)

### 设计

PMM 使用位图管理物理页帧，支持引用计数和帧保护。

```c
// 每个位代表一个 4KB 页帧
// 0 = 空闲, 1 = 已使用
static uint32_t *frame_bitmap;
static uint32_t bitmap_size;
static uint32_t total_frames;

// 引用计数（COW 支持）
static uint32_t *frame_refcount;
```

### 核心操作

```c
// 分配一个物理页帧
uint32_t pmm_alloc_frame(void) {
    int idx = find_free_frame();
    if (idx < 0) return 0;
    
    set_frame(idx);
    frame_refcount[idx] = 1;
    
    // 清零页帧
    memset(PHYS_TO_VIRT(idx * PAGE_SIZE), 0, PAGE_SIZE);
    
    return idx * PAGE_SIZE;
}

// 释放一个物理页帧
void pmm_free_frame(uint32_t frame) {
    uint32_t idx = frame / PAGE_SIZE;
    
    // 减少引用计数
    if (--frame_refcount[idx] == 0) {
        clear_frame(idx);
    }
}

// 增加引用计数（COW）
uint32_t pmm_frame_ref_inc(uint32_t frame) {
    return ++frame_refcount[frame / PAGE_SIZE];
}
```

### 位图操作

```c
static inline void set_frame(uint32_t idx) {
    frame_bitmap[idx / 32] |= (1 << (idx % 32));
}

static inline void clear_frame(uint32_t idx) {
    frame_bitmap[idx / 32] &= ~(1 << (idx % 32));
}

static inline bool test_frame(uint32_t idx) {
    return frame_bitmap[idx / 32] & (1 << (idx % 32));
}
```

### 帧保护

某些关键页帧（如页目录、页表）需要保护，防止被意外释放：

```c
void pmm_protect_frame(uint32_t frame);
void pmm_unprotect_frame(uint32_t frame);
bool pmm_is_frame_protected(uint32_t frame);
```

## 虚拟内存管理 (VMM)

### x86 分页机制

x86 使用两级页表：

```
CR3 → Page Directory (1024 entries)
           ↓
      Page Table (1024 entries each)
           ↓
      Physical Page (4KB)

虚拟地址解析：
[PD Index: 10 bits][PT Index: 10 bits][Offset: 12 bits]
```

### 页表项格式

```
+------------------+----+---+---+---+---+---+---+---+---+
|   Physical Addr  |AVL |G  |0  |D  |A  |PCD|PWT|U/S|R/W|P  |
|     20 bits      |3bit|   |   |   |   |   |   |   |   |   |
+------------------+----+---+---+---+---+---+---+---+---+---+
 31              12  11   9   8   7   6   5   4   3   2   1  0

P   = Present（存在位）
R/W = Read/Write（读写位）
U/S = User/Supervisor（用户/特权级）
PWT = Page Write-Through
PCD = Page Cache Disable
A   = Accessed（已访问）
D   = Dirty（已修改）
G   = Global（全局页）
```

### 核心操作

```c
// 映射虚拟页到物理页
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    
    // 确保页表存在
    if (!(page_dir->entries[pd_idx] & PAGE_PRESENT)) {
        uint32_t table_phys = pmm_alloc_frame();
        page_dir->entries[pd_idx] = table_phys | PAGE_PRESENT | PAGE_WRITE;
    }
    
    // 设置页表项
    page_table_t *table = get_page_table(pd_idx);
    table->entries[pt_idx] = (phys & PAGE_MASK) | flags;
    
    vmm_flush_tlb(virt);
}

// 取消映射
void vmm_unmap_page(uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    
    page_table_t *table = get_page_table(pd_idx);
    table->entries[pt_idx] = 0;
    
    vmm_flush_tlb(virt);
}

// 查询物理地址
uint32_t vmm_get_phys(uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    
    if (!(page_dir->entries[pd_idx] & PAGE_PRESENT))
        return 0;
    
    page_table_t *table = get_page_table(pd_idx);
    if (!(table->entries[pt_idx] & PAGE_PRESENT))
        return 0;
    
    return (table->entries[pt_idx] & PAGE_MASK) | (virt & 0xFFF);
}
```

### Copy-on-Write (COW)

fork() 时使用 COW 避免立即复制所有页面：

```c
uint32_t vmm_clone_page_directory(uint32_t src_dir_phys) {
    uint32_t new_dir_phys = pmm_alloc_frame();
    // ...
    
    for (每个用户空间页表项) {
        if (是可写页面) {
            // 标记为只读 + COW
            src_pte &= ~PAGE_WRITE;
            src_pte |= PAGE_COW;
            new_pte = src_pte;
            
            // 增加物理页引用计数
            pmm_frame_ref_inc(get_frame(src_pte));
        }
    }
    
    return new_dir_phys;
}
```

### COW 缺页处理

```c
bool vmm_handle_cow_fault(uint32_t fault_addr) {
    uint32_t pte = get_page_table_entry(fault_addr);
    
    if (!(pte & PAGE_COW))
        return false;  // 不是 COW 页
    
    uint32_t old_frame = get_frame(pte);
    uint32_t refcount = pmm_frame_get_refcount(old_frame);
    
    if (refcount == 1) {
        // 唯一引用，直接修改权限
        set_page_writable(fault_addr);
    } else {
        // 复制页面
        uint32_t new_frame = pmm_alloc_frame();
        memcpy(PHYS_TO_VIRT(new_frame), PHYS_TO_VIRT(old_frame), PAGE_SIZE);
        
        // 更新页表
        update_page_table(fault_addr, new_frame, PAGE_WRITE);
        
        // 减少旧页引用
        pmm_frame_ref_dec(old_frame);
    }
    
    return true;
}
```

## 堆分配器

### 设计

堆分配器基于空闲链表，支持动态扩展：

```c
typedef struct heap_block {
    uint32_t magic;           // 魔数，用于检测损坏
    uint32_t size;            // 块大小（包含头部）
    bool is_free;             // 是否空闲
    struct heap_block *next;  // 下一个块
    struct heap_block *prev;  // 上一个块
} heap_block_t;

#define HEAP_MAGIC 0xDEADBEEF
```

### 分配算法（First-Fit）

```c
void *kmalloc(size_t size) {
    size_t total = size + sizeof(heap_block_t);
    total = ALIGN_UP(total, 16);  // 16 字节对齐
    
    // 查找足够大的空闲块
    heap_block_t *block = heap_start;
    while (block) {
        if (block->is_free && block->size >= total) {
            // 分割大块
            if (block->size > total + MIN_BLOCK_SIZE) {
                split_block(block, total);
            }
            
            block->is_free = false;
            return (void *)((uint32_t)block + sizeof(heap_block_t));
        }
        block = block->next;
    }
    
    // 扩展堆
    if (!expand_heap(total))
        return NULL;
    
    return kmalloc(size);  // 重试
}
```

### 释放与合并

```c
void kfree(void *ptr) {
    if (!ptr) return;
    
    heap_block_t *block = (heap_block_t *)((uint32_t)ptr - sizeof(heap_block_t));
    
    // 验证魔数
    if (block->magic != HEAP_MAGIC) {
        panic("Heap corruption detected!");
    }
    
    block->is_free = true;
    
    // 与前一个块合并
    if (block->prev && block->prev->is_free) {
        block = merge_blocks(block->prev, block);
    }
    
    // 与后一个块合并
    if (block->next && block->next->is_free) {
        merge_blocks(block, block->next);
    }
}
```

### 堆扩展

```c
static bool expand_heap(size_t needed) {
    // 按页分配
    size_t pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) return false;
        
        vmm_map_page(heap_end, phys, PAGE_PRESENT | PAGE_WRITE);
        heap_end += PAGE_SIZE;
    }
    
    // 创建新的空闲块
    heap_block_t *new_block = (heap_block_t *)(heap_end - pages * PAGE_SIZE);
    new_block->magic = HEAP_MAGIC;
    new_block->size = pages * PAGE_SIZE;
    new_block->is_free = true;
    
    // 链接到链表
    // ...
    
    return true;
}
```

## 用户空间内存管理

### brk/sbrk 系统调用

```c
// 扩展数据段
void *sys_sbrk(int32_t increment) {
    task_t *task = current_task;
    uint32_t old_brk = task->brk;
    uint32_t new_brk = old_brk + increment;
    
    if (increment > 0) {
        // 分配新页面
        for (uint32_t addr = PAGE_ALIGN_UP(old_brk); 
             addr < new_brk; 
             addr += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_frame();
            vmm_map_page_user(task->page_dir, addr, phys);
        }
    } else if (increment < 0) {
        // 释放页面
        for (uint32_t addr = PAGE_ALIGN_DOWN(new_brk); 
             addr < old_brk; 
             addr += PAGE_SIZE) {
            vmm_unmap_page(addr);
        }
    }
    
    task->brk = new_brk;
    return (void *)old_brk;
}
```

### mmap 系统调用

```c
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    task_t *task = current_task;
    
    // 查找空闲虚拟地址区域
    uint32_t virt = find_free_region(task, length);
    if (!virt) return MAP_FAILED;
    
    // 建立映射
    for (size_t i = 0; i < length; i += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_frame();
        uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (prot & PROT_WRITE) page_flags |= PAGE_WRITE;
        
        vmm_map_page(virt + i, phys, page_flags);
    }
    
    return (void *)virt;
}
```

## 内存布局总结

```
内核虚拟地址空间：
0xFFFFFFFF ┌─────────────────────┐
           │     保留区域         │
0xF0020000 ├─────────────────────┤
           │     帧缓冲          │ MMIO
0xF0000000 ├─────────────────────┤
           │     E1000 MMIO      │
           ├─────────────────────┤
           │     内核堆          │ 动态增长
           ├─────────────────────┤
           │     物理内存映射     │ 恒等映射
0x80000000 └─────────────────────┘

用户虚拟地址空间：
0x80000000 ┌─────────────────────┐
           │     用户栈          │ 向下增长
0x70000000 ├─────────────────────┤
           │     共享内存/mmap   │
           ├─────────────────────┤
           │     用户堆          │ 向上增长
           ├─────────────────────┤
           │     BSS             │
           ├─────────────────────┤
           │     数据段          │
           ├─────────────────────┤
           │     代码段          │
0x10000000 ├─────────────────────┤
           │     保留 (NULL)     │
0x00000000 └─────────────────────┘
```

