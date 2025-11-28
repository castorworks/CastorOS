/**
 * @file heap.c
 * @brief 内核堆内存管理器实现
 * 
 * 使用双向链表实现首次适应算法的堆内存管理
 */

#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/sync/spinlock.h>

static uint32_t heap_start;        ///< 堆起始地址
static uint32_t heap_end;          ///< 堆当前结束地址
static uint32_t heap_max;          ///< 堆最大地址
static heap_block_t *first_block = NULL;  ///< 第一个内存块指针
static heap_block_t *last_block = NULL;   ///< 最后一个内存块指针
static spinlock_t heap_lock;       ///< 堆自旋锁，保护堆的内部状态

/**
 * @brief 扩展堆空间
 * @param size 需要扩展的字节数
 * @return 成功返回 true，失败返回 false
 */
static bool expand(size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (heap_end + pages * PAGE_SIZE > heap_max) return false;
    
    uint32_t old_heap_end = heap_end;
    uint32_t current_dir_phys = vmm_get_page_directory();
    
    // 分配物理页并映射到虚拟地址空间
    for (size_t i = 0; i < pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame) {
            // 分配失败：清理已分配的页
            LOG_ERROR_MSG("heap: expand failed at page %u/%u (out of physical memory)\n", 
                         (unsigned int)(i + 1), (unsigned int)pages);
            for (size_t j = 0; j < i; j++) {
                uint32_t virt = old_heap_end + j * PAGE_SIZE;
                uint32_t phys = vmm_unmap_page_in_directory(current_dir_phys, virt);
                if (phys) {
                    pmm_free_frame(phys);
                }
            }
            return false;
        }
        
        if (!vmm_map_page(heap_end + i * PAGE_SIZE, frame, PAGE_PRESENT | PAGE_WRITE)) {
            // 映射失败：清理已分配的页
            LOG_ERROR_MSG("heap: expand failed at mapping page %u/%u\n", 
                         (unsigned int)(i + 1), (unsigned int)pages);
            pmm_free_frame(frame);
            for (size_t j = 0; j < i; j++) {
                uint32_t virt = old_heap_end + j * PAGE_SIZE;
                uint32_t phys = vmm_unmap_page_in_directory(current_dir_phys, virt);
                if (phys) {
                    pmm_free_frame(phys);
                }
            }
            return false;
        }
    }
    heap_end += pages * PAGE_SIZE;
    return true;
}

/**
 * @brief 合并相邻的空闲块
 * @param b 要合并的块指针
 * 
 * 向前和向后合并相邻的空闲块以减少内存碎片
 */
static void coalesce(heap_block_t *b) {
    // 【安全检查】验证块的 magic
    if (b->magic != HEAP_MAGIC) {
        LOG_ERROR_MSG("coalesce: block %p has invalid magic %x!\n", b, b->magic);
        return;
    }
    
    // 向后合并
    if (b->next && b->next->is_free) {
        // 【安全检查】验证 next 块的 magic
        if (b->next->magic != HEAP_MAGIC) {
            LOG_ERROR_MSG("coalesce: next block %p has invalid magic %x!\n", b->next, b->next->magic);
            return;
        }
        
        if (b->next == last_block) {
            last_block = b;
        }
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    // 向前合并
    if (b->prev && b->prev->is_free) {
        // 【安全检查】验证 prev 块的 magic
        if (b->prev->magic != HEAP_MAGIC) {
            LOG_ERROR_MSG("coalesce: prev block %p has invalid magic %x!\n", b->prev, b->prev->magic);
            return;
        }
        
        if (b == last_block) {
            last_block = b->prev;
        }
        b->prev->size += sizeof(heap_block_t) + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

/**
 * @brief 分裂内存块
 * @param b 要分裂的块指针
 * @param size 需要的大小
 * 
 * 如果块足够大，将其分裂成两部分：使用的部分和剩余的空闲部分
 */
static void split(heap_block_t *b, size_t size) {
    // 【安全检查】验证块的 magic
    if (b->magic != HEAP_MAGIC) {
        LOG_ERROR_MSG("split: block %p has invalid magic %x!\n", b, b->magic);
        return;
    }
    
    // 只有当剩余空间足够容纳一个新块时才分裂（至少16字节）
    if (b->size >= size + sizeof(heap_block_t) + 16) {
        heap_block_t *new = (heap_block_t*)((uint32_t)b + sizeof(heap_block_t) + size);
        new->size = b->size - size - sizeof(heap_block_t);
        new->is_free = true;
        new->magic = HEAP_MAGIC;
        new->next = b->next;
        new->prev = b;
        if (b->next) b->next->prev = new;
        b->next = new;
        b->size = size;
    }
}

/**
 * @brief 初始化堆内存管理器
 * @param start 堆起始地址
 * @param size 堆最大大小（字节）
 */
void heap_init(uint32_t start, uint32_t size) {
    heap_start = heap_end = PAGE_ALIGN_UP(start);
    heap_max = heap_start + size;
    
    LOG_INFO_MSG("heap_init: start=%x, max=%x, size=%u\n", heap_start, heap_max, size);
    
    // 初始化堆自旋锁
    spinlock_init(&heap_lock);
    
    // 分配第一页作为初始堆空间
    if (!expand(PAGE_SIZE)) PANIC("Heap init failed");
    
    // 初始化第一个内存块
    first_block = (heap_block_t*)heap_start;
    LOG_INFO_MSG("heap_init: first_block at 0x%p, setting magic...\n", first_block);
    
    first_block->size = PAGE_SIZE - sizeof(heap_block_t);
    first_block->is_free = true;
    first_block->magic = HEAP_MAGIC;
    first_block->next = first_block->prev = NULL;
    last_block = first_block;
    
    LOG_INFO_MSG("heap_init: first_block magic=%x (expected %x)\n", 
                 first_block->magic, HEAP_MAGIC);
}

/**
 * @brief 分配内存
 * @param size 要分配的字节数
 * @return 成功返回分配的内存地址，失败返回 NULL
 * 
 * 使用首次适应算法查找空闲块，如果找不到则扩展堆空间
 */
void* kmalloc(size_t size) {
    if (!size) return NULL;
    
    bool irq_state;
    spinlock_lock_irqsave(&heap_lock, &irq_state);
    
    // 【安全检查】验证 first_block 的有效性
    if (first_block == NULL) {
        LOG_ERROR_MSG("kmalloc: first_block is NULL!\n");
        spinlock_unlock_irqrestore(&heap_lock, irq_state);
        return NULL;
    }
    if ((uint32_t)first_block < 0x80000000 || first_block->magic != HEAP_MAGIC) {
        LOG_ERROR_MSG("kmalloc: first_block corrupted! addr=%p, magic=%x (expected %x)\n", 
                     first_block, (uint32_t)first_block < 0x80000000 ? 0 : first_block->magic, HEAP_MAGIC);
        spinlock_unlock_irqrestore(&heap_lock, irq_state);
        return NULL;
    }
    
    // 对齐到4字节边界
    size = (size + 3) & ~3;
    
    // 查找第一个足够大的空闲块
    for (heap_block_t *b = first_block; b; b = b->next) {
        // 【安全检查】验证当前块的有效性
        if ((uint32_t)b < 0x80000000) {
            LOG_ERROR_MSG("kmalloc: invalid block pointer %p in heap chain!\n", b);
            spinlock_unlock_irqrestore(&heap_lock, irq_state);
            return NULL;
        }
        if (b->magic != HEAP_MAGIC) {
            LOG_ERROR_MSG("kmalloc: block %p has invalid magic %x (expected %x)!\n", b, b->magic, HEAP_MAGIC);
            spinlock_unlock_irqrestore(&heap_lock, irq_state);
            return NULL;
        }
        
        if (b->is_free && b->size >= size) {
            b->is_free = false;
            split(b, size);
            void *ptr = (void*)((uint32_t)b + sizeof(heap_block_t));
            spinlock_unlock_irqrestore(&heap_lock, irq_state);
            return ptr;
        }
    }
    
    // 没有找到空闲块，扩展堆空间
    uint32_t old = heap_end;
    if (!expand(size + sizeof(heap_block_t))) {
        spinlock_unlock_irqrestore(&heap_lock, irq_state);
        return NULL;
    }
    
    // 创建新块
    heap_block_t *new = (heap_block_t*)old;
    new->size = heap_end - old - sizeof(heap_block_t);
    new->is_free = false;
    new->magic = HEAP_MAGIC;
    new->next = NULL;
    new->prev = last_block;
    
    // 链接到末尾
    if (last_block) {
        last_block->next = new;
    }
    last_block = new;
    
    void *ptr = (void*)((uint32_t)new + sizeof(heap_block_t));
    spinlock_unlock_irqrestore(&heap_lock, irq_state);
    return ptr;
}

/**
 * @brief 释放内存
 * @param ptr 要释放的内存指针
 * 
 * 标记块为空闲并合并相邻的空闲块
 */
void kfree(void* ptr) {
    if (!ptr) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&heap_lock, &irq_state);
    
    // 获取块头指针
    heap_block_t *b = (heap_block_t*)((uint32_t)ptr - sizeof(heap_block_t));
    // 验证魔数
    if (b->magic != HEAP_MAGIC) {
        LOG_WARN_MSG("kfree: invalid magic at %p (block %p), magic=%x\n", ptr, b, b->magic);
        spinlock_unlock_irqrestore(&heap_lock, irq_state);
        return;
    }
    b->is_free = true;
    coalesce(b);
    
    spinlock_unlock_irqrestore(&heap_lock, irq_state);
}

/**
 * @brief 重新分配内存
 * @param ptr 原内存指针
 * @param size 新的字节数
 * @return 成功返回新内存地址，失败返回 NULL（原内存仍有效）
 * 
 * 如果新大小小于等于原大小，直接返回原指针；否则分配新内存并复制数据
 */
void* krealloc(void* ptr, size_t size) {
    // kmalloc 和 kfree 内部会处理锁
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }
    
    bool irq_state;
    spinlock_lock_irqsave(&heap_lock, &irq_state);
    
    heap_block_t *b = (heap_block_t*)((uint32_t)ptr - sizeof(heap_block_t));
    if (b->magic != HEAP_MAGIC) {
        spinlock_unlock_irqrestore(&heap_lock, irq_state);
        return NULL;
    }
    
    // 如果新大小小于等于原大小，直接返回
    if (size <= b->size) {
        spinlock_unlock_irqrestore(&heap_lock, irq_state);
        return ptr;
    }
    
    // 保存旧大小用于复制
    size_t old_size = b->size;
    
    spinlock_unlock_irqrestore(&heap_lock, irq_state);
    
    // 分配新内存并复制数据（kmalloc/kfree 会自己获取锁）
    void* new = kmalloc(size);
    if (new) {
        memcpy(new, ptr, old_size);
        kfree(ptr);
    }
    return new;
}

/**
 * @brief 分配并清零内存
 * @param num 元素数量
 * @param size 每个元素的大小
 * @return 成功返回分配的内存地址，失败返回 NULL
 */
void* kcalloc(size_t num, size_t size) {
    // 检查整数溢出
    if (num != 0 && size > (size_t)-1 / num) {
        return NULL;
    }
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

/**
 * @brief 分配对齐内存
 * @param size 要分配的字节数
 * @param alignment 对齐边界（必须是 2 的幂）
 * @return 成功返回对齐的内存地址，失败返回 NULL
 * 
 * 实现方式：分配额外空间以确保可以返回对齐的地址，
 * 并在对齐地址前存储原始指针以便正确释放
 */
void* kmalloc_aligned(size_t size, size_t alignment) {
    if (size == 0 || alignment == 0) {
        return NULL;
    }
    
    // 检查 alignment 是否为 2 的幂
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }
    
    // 确保 alignment 至少为 sizeof(void*)
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    
    // 分配额外空间：alignment - 1 用于对齐，sizeof(void*) 用于存储原始指针
    size_t extra = alignment - 1 + sizeof(void*);
    void* raw = kmalloc(size + extra);
    if (!raw) {
        return NULL;
    }
    
    // 计算对齐后的地址
    // 先预留 sizeof(void*) 空间存储原始指针，然后对齐
    uint32_t raw_addr = (uint32_t)raw + sizeof(void*);
    uint32_t aligned_addr = (raw_addr + alignment - 1) & ~(alignment - 1);
    
    // 在对齐地址前存储原始指针
    void** ptr_store = (void**)(aligned_addr - sizeof(void*));
    *ptr_store = raw;
    
    return (void*)aligned_addr;
}

/**
 * @brief 释放对齐内存
 * @param ptr 由 kmalloc_aligned 返回的指针
 * 
 * 从对齐地址前读取原始指针并释放
 */
void kfree_aligned(void* ptr) {
    if (!ptr) {
        return;
    }
    
    // 从对齐地址前读取原始指针
    void** ptr_store = (void**)((uint32_t)ptr - sizeof(void*));
    void* raw = *ptr_store;
    
    // 释放原始分配
    kfree(raw);
}

/**
 * @brief 获取堆使用统计信息
 * @param info 输出参数，用于存储堆统计信息
 * @return 成功返回 0，失败返回 -1
 */
int heap_get_info(heap_info_t *info) {
    if (!info) {
        return -1;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&heap_lock, &irq_state);
    
    size_t total = heap_end - heap_start;
    size_t used = 0, free = 0;
    size_t used_metadata = 0, free_metadata = 0;
    uint32_t block_count = 0;
    uint32_t free_block_count = 0;
    
    // 遍历所有块统计使用情况
    for (heap_block_t *b = first_block; b; b = b->next) {
        block_count++;
        size_t metadata_size = sizeof(heap_block_t);
        
        if (b->is_free) {
            free += b->size;
            free_metadata += metadata_size;
            free_block_count++;
        } else {
            used += b->size;
            used_metadata += metadata_size;
        }
    }
    
    spinlock_unlock_irqrestore(&heap_lock, irq_state);
    
    info->total = total;
    info->used = used + used_metadata;   // 已使用块的数据 + 元数据
    info->free = free + free_metadata;   // 空闲块的数据 + 元数据
    info->max = heap_max - heap_start;
    info->block_count = block_count;
    info->free_block_count = free_block_count;
    
    return 0;
}

/**
 * @brief 打印堆使用信息
 * 
 * 统计并打印堆的总大小、已使用和空闲内存
 */
void heap_print_info(void) {
    heap_info_t info;
    if (heap_get_info(&info) != 0) {
        kprintf("Error: Failed to get heap info\n");
        return;
    }
    
    kprintf("\n===================================== Heap =====================================\n");
    kprintf("Total: %u KB\n", info.total/1024);
    kprintf("Used:  %u KB\n", info.used/1024);
    kprintf("Free:  %u KB\n", info.free/1024);
    kprintf("================================================================================\n\n");
}
