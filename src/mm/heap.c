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
#include <kernel/sync/mutex.h>

static uint32_t heap_start;        ///< 堆起始地址
static uint32_t heap_end;          ///< 堆当前结束地址
static uint32_t heap_max;          ///< 堆最大地址
static heap_block_t *first_block = NULL;  ///< 第一个内存块指针
static heap_block_t *last_block = NULL;   ///< 最后一个内存块指针
static mutex_t heap_mutex;         ///< 堆互斥锁，保护堆的内部状态

/**
 * @brief 扩展堆空间
 * @param size 需要扩展的字节数
 * @return 成功返回 true，失败返回 false
 */
static bool expand(size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (heap_end + pages * PAGE_SIZE > heap_max) return false;
    
    // 分配物理页并映射到虚拟地址空间
    for (size_t i = 0; i < pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame || !vmm_map_page(heap_end + i * PAGE_SIZE, frame, 
            PAGE_PRESENT | PAGE_WRITE)) {
            if (frame) pmm_free_frame(frame);
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
    // 向后合并
    if (b->next && b->next->is_free) {
        if (b->next == last_block) {
            last_block = b;
        }
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    // 向前合并
    if (b->prev && b->prev->is_free) {
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
    
    // 初始化堆互斥锁
    mutex_init(&heap_mutex);
    
    // 分配第一页作为初始堆空间
    if (!expand(PAGE_SIZE)) PANIC("Heap init failed");
    
    // 初始化第一个内存块
    first_block = (heap_block_t*)heap_start;
    first_block->size = PAGE_SIZE - sizeof(heap_block_t);
    first_block->is_free = true;
    first_block->magic = HEAP_MAGIC;
    first_block->next = first_block->prev = NULL;
    last_block = first_block;
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
    
    mutex_lock(&heap_mutex);
    
    // 对齐到4字节边界
    size = (size + 3) & ~3;
    
    // 查找第一个足够大的空闲块
    for (heap_block_t *b = first_block; b; b = b->next) {
        if (b->is_free && b->size >= size) {
            b->is_free = false;
            split(b, size);
            void *ptr = (void*)((uint32_t)b + sizeof(heap_block_t));
            mutex_unlock(&heap_mutex);
            return ptr;
        }
    }
    
    // 没有找到空闲块，扩展堆空间
    uint32_t old = heap_end;
    if (!expand(size + sizeof(heap_block_t))) {
        mutex_unlock(&heap_mutex);
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
    mutex_unlock(&heap_mutex);
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
    
    mutex_lock(&heap_mutex);
    
    // 获取块头指针
    heap_block_t *b = (heap_block_t*)((uint32_t)ptr - sizeof(heap_block_t));
    // 验证魔数
    if (b->magic != HEAP_MAGIC) {
        mutex_unlock(&heap_mutex);
        return;
    }
    b->is_free = true;
    coalesce(b);
    
    mutex_unlock(&heap_mutex);
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
    
    mutex_lock(&heap_mutex);
    
    heap_block_t *b = (heap_block_t*)((uint32_t)ptr - sizeof(heap_block_t));
    if (b->magic != HEAP_MAGIC) {
        mutex_unlock(&heap_mutex);
        return NULL;
    }
    
    // 如果新大小小于等于原大小，直接返回
    if (size <= b->size) {
        mutex_unlock(&heap_mutex);
        return ptr;
    }
    
    // 保存旧大小用于复制
    size_t old_size = b->size;
    
    mutex_unlock(&heap_mutex);
    
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
 * @brief 打印堆使用信息
 * 
 * 统计并打印堆的总大小、已使用和空闲内存
 */
void heap_print_info(void) {
    mutex_lock(&heap_mutex);
    
    size_t total = heap_end - heap_start;
    size_t used = 0, free = 0;
    
    // 遍历所有块统计使用情况
    for (heap_block_t *b = first_block; b; b = b->next) {
        if (b->is_free) free += b->size;
        else used += b->size;
    }
    
    mutex_unlock(&heap_mutex);
    
    kprintf("\n===================================== Heap =====================================\n");
    kprintf("Total: %u KB\n", total/1024);
    kprintf("Used:  %u KB\n", used/1024);
    kprintf("Free:  %u KB\n", free/1024);
    kprintf("================================================================================\n\n");
}
