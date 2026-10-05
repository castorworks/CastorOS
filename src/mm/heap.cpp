/**
 * @file heap.c
 * @brief 内核堆内存管理器实现
 * 
 * 使用双向链表实现首次适应算法的堆内存管理
 */

#include <mm/heap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/sync/spinlock.h>
#include <hal/hal.h>

static uintptr_t heap_start;        ///< 堆起始地址
static uintptr_t heap_end;          ///< 堆当前结束地址
static uintptr_t heap_max;          ///< 堆最大地址
static heap_block_t *first_block = NULL;  ///< 第一个内存块指针
static heap_block_t *last_block = NULL;   ///< 最后一个内存块指针
static sync::Spinlock heap_lock;       ///< 堆自旋锁，保护堆的内部状态

/**
 * @brief 扩展堆空间
 * @param size 需要扩展的字节数
 * @return 成功返回 true，失败返回 false
 * 
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 3.1**
 */
static bool expand(size_t size) {
    // 先和剩余空间比较，避免 size 取整或 heap_end 相加时回绕
    if (size > heap_max - heap_end) return false;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages * PAGE_SIZE > heap_max - heap_end) return false;
    
#if defined(ARCH_X86_64)
    // x86_64: 引导时已经映射了前 1GB 物理内存到高半核
    // 堆虚拟地址 = KERNEL_VIRTUAL_BASE + 物理地址
    // 所以堆扩展只需要确保对应的物理地址在已映射范围内
    // 
    // 堆地址布局：
    //   heap_start = PHYS_TO_VIRT(pmm_data_end_phys)
    //   heap_end 对应的物理地址 = VIRT_TO_PHYS(heap_end)
    //
    // 由于引导时使用恒等映射（物理地址 X 映射到虚拟地址 KERNEL_VIRTUAL_BASE + X），
    // 我们只需要验证堆扩展后的物理地址仍在已映射范围内（< 1GB）
    
    uintptr_t new_heap_end = heap_end + pages * PAGE_SIZE;
    uintptr_t new_heap_end_phys = VIRT_TO_PHYS(new_heap_end);
    
    // 验证扩展后的堆仍在已映射范围内（前 1GB）
    if (new_heap_end_phys > 0x40000000) {  // 1GB
        LOG_ERROR_MSG("heap: expand would exceed boot mapping (phys 0x%lx > 1GB)\n", 
                     (unsigned long)new_heap_end_phys);
        return false;
    }
    
    // 清零新扩展的堆空间（已经通过引导时的恒等映射可访问）
    memset((void*)heap_end, 0, pages * PAGE_SIZE);
    heap_end = new_heap_end;
    return true;
#elif defined(ARCH_ARM64)
    // ARM64: 引导时已经使用 1GB 块映射了物理内存到高半核
    // 堆虚拟地址 = KERNEL_VIRTUAL_BASE + 物理地址
    // 所以堆扩展只需要确保对应的物理地址在已映射范围内
    //
    // 堆地址布局：
    //   heap_start = PHYS_TO_VIRT(pmm_data_end_phys)
    //   heap_end 对应的物理地址 = VIRT_TO_PHYS(heap_end)
    //
    // 由于引导时使用 1GB 块映射（物理地址 X 映射到虚拟地址 KERNEL_VIRTUAL_BASE + X），
    // 我们只需要验证堆扩展后的物理地址仍在已映射范围内
    
    uintptr_t new_heap_end = heap_end + pages * PAGE_SIZE;
    uintptr_t new_heap_end_phys = VIRT_TO_PHYS(new_heap_end);
    
    // 验证扩展后的堆仍在物理内存范围内
    // ARM64 QEMU virt machine: RAM at 0x40000000 - 0x60000000 (512MB with -m 512M)
    // 但引导代码映射了 4GB，所以只需检查不超过 4GB
    if (new_heap_end_phys > 0x100000000ULL) {  // 4GB
        LOG_ERROR_MSG("heap: expand would exceed boot mapping (phys 0x%llx > 4GB)\n", 
                     (unsigned long long)new_heap_end_phys);
        return false;
    }
    
    // 清零新扩展的堆空间（已经通过引导时的块映射可访问）
    memset((void*)heap_end, 0, pages * PAGE_SIZE);
    heap_end = new_heap_end;
    return true;
#else
    // i686: 原有实现
    uintptr_t old_heap_end = heap_end;
    uintptr_t current_dir_phys = mm::Vmm::get_page_directory();
    
    // 分配物理页并映射到虚拟地址空间
    for (size_t i = 0; i < pages; i++) {
        paddr_t frame = mm::Pmm::alloc_frame();
        if (frame == PADDR_INVALID) {
            // 分配失败：清理已分配的页
            LOG_ERROR_MSG("heap: expand failed at page %u/%u (out of physical memory)\n", 
                         (unsigned int)(i + 1), (unsigned int)pages);
            for (size_t j = 0; j < i; j++) {
                uintptr_t virt = old_heap_end + j * PAGE_SIZE;
                uintptr_t phys = mm::Vmm::unmap_page_in_directory(current_dir_phys, virt);
                if (phys) {
                    mm::Pmm::free_frame((paddr_t)phys);
                }
            }
            return false;
        }
        
        if (!mm::Vmm::map_page(heap_end + i * PAGE_SIZE, (uintptr_t)frame, PAGE_PRESENT | PAGE_WRITE)) {
            // 映射失败：清理已分配的页
            LOG_ERROR_MSG("heap: expand failed at mapping page %u/%u\n", 
                         (unsigned int)(i + 1), (unsigned int)pages);
            mm::Pmm::free_frame(frame);
            for (size_t j = 0; j < i; j++) {
                uintptr_t virt = old_heap_end + j * PAGE_SIZE;
                uintptr_t phys = mm::Vmm::unmap_page_in_directory(current_dir_phys, virt);
                if (phys) {
                    mm::Pmm::free_frame((paddr_t)phys);
                }
            }
            return false;
        }
    }
    heap_end += pages * PAGE_SIZE;
    return true;
#endif
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
        LOG_ERROR_MSG("coalesce: block %p has invalid magic 0x%x!\n", b, b->magic);
        return;
    }
    
    // 向后合并
    if (b->next && b->next->is_free) {
        // 【安全检查】验证 next 块的 magic
        if (b->next->magic != HEAP_MAGIC) {
            LOG_ERROR_MSG("coalesce: next block %p has invalid magic 0x%x!\n", b->next, b->next->magic);
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
            LOG_ERROR_MSG("coalesce: prev block %p has invalid magic 0x%x!\n", b->prev, b->prev->magic);
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
        LOG_ERROR_MSG("split: block %p has invalid magic 0x%x!\n", b, b->magic);
        return;
    }
    
    // 只有当剩余空间足够容纳一个新块时才分裂（至少16字节）
    if (b->size >= size + sizeof(heap_block_t) + 16) {
        heap_block_t *new_block = (heap_block_t*)((uintptr_t)b + sizeof(heap_block_t) + size);
        new_block->size = b->size - size - sizeof(heap_block_t);
        new_block->is_free = true;
        new_block->magic = HEAP_MAGIC;
        new_block->next = b->next;
        new_block->prev = b;
        if (b->next) b->next->prev = new_block;
        b->next = new_block;
        b->size = size;
        // 分裂的是尾块时，余下的空闲块才是新的尾块；否则下次扩展堆时
        // kmalloc 会把扩展块直接接在 b 后面，余块及其后的块就从链表里丢失
        if (b == last_block) {
            last_block = new_block;
        }
    }
}

/**
 * @brief 初始化堆内存管理器
 * @param start 堆起始地址
 * @param size 堆最大大小（字节）
 */
void mm::Heap::init(uintptr_t start, uint32_t size) {
    heap_start = heap_end = PAGE_ALIGN_UP(start);
    heap_max = heap_start + size;

#if defined(ARCH_I686)
    // i686 的堆把直接映射区的虚拟页改映射到新分配的帧上，于是物理帧
    // VIRT_TO_PHYS(堆页) 就失去了 PHYS_TO_VIRT 别名。堆初始化之前分配出去的帧
    // （mm::Vmm::init 为扩展直接映射分配的内核页表）正好落在这个范围的开头，
    // 而内核始终通过 PHYS_TO_VIRT 访问页表，所以堆必须从这些帧之后开始。
    // 之后的分配由 mm::Pmm::set_heap_reserved_range 挡在范围之外。
    uintptr_t first_usable = heap_start;
    for (uintptr_t v = heap_start; v < heap_max; v += PAGE_SIZE) {
        if (mm::Pmm::frame_get_refcount((paddr_t)VIRT_TO_PHYS(v)) != 0) {
            first_usable = v + PAGE_SIZE;
        }
    }
    if (first_usable != heap_start) {
        LOG_INFO_MSG("mm::Heap::init: skipping %u pages whose frames are already in use\n",
                     (unsigned int)((first_usable - heap_start) / PAGE_SIZE));
        if (first_usable >= heap_max) PANIC("Heap init failed: no usable range");
        heap_start = heap_end = first_usable;
    }
#endif
    
    LOG_INFO_MSG("mm::Heap::init: start=0x%llx, max=0x%llx, size=%u\n", (unsigned long long)heap_start, (unsigned long long)heap_max, size);
    
    // 初始化堆自旋锁
    heap_lock.init();
    
    // 分配第一页作为初始堆空间
    if (!expand(PAGE_SIZE)) PANIC("Heap init failed");
    
    // 初始化第一个内存块
    first_block = (heap_block_t*)heap_start;
    LOG_INFO_MSG("mm::Heap::init: first_block at 0x%llx, setting magic...\n", (unsigned long long)(uintptr_t)first_block);
    
    first_block->size = PAGE_SIZE - sizeof(heap_block_t);
    first_block->is_free = true;
    first_block->magic = HEAP_MAGIC;
    first_block->next = first_block->prev = NULL;
    last_block = first_block;
    
    LOG_INFO_MSG("mm::Heap::init: first_block magic=0x%x (expected 0x%x)\n", 
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
    
    sync::SpinlockIrqGuard guard(heap_lock);
    
    // 【安全检查】验证 first_block 的有效性
    if (first_block == NULL) {
        LOG_ERROR_MSG("kmalloc: first_block is NULL!\n");
        return NULL;
    }
    if ((uintptr_t)first_block < KERNEL_VIRTUAL_BASE || first_block->magic != HEAP_MAGIC) {
        LOG_ERROR_MSG("kmalloc: first_block corrupted! addr=0x%llx, magic=0x%x (expected 0x%x)\n", 
                     (unsigned long long)(uintptr_t)first_block, 
                     (uintptr_t)first_block < KERNEL_VIRTUAL_BASE ? 0 : first_block->magic, HEAP_MAGIC);
        LOG_ERROR_MSG("kmalloc: heap_start=0x%llx, heap_end=0x%llx\n",
                     (unsigned long long)heap_start, (unsigned long long)heap_end);
        return NULL;
    }
    
    // 请求不可能超过整个堆；同时避免下面的对齐和块头相加回绕
    if (size > heap_max - heap_start) {
        return NULL;
    }
    
    // 对齐到4字节边界
    size = (size + 3) & ~3;
    
    // 查找第一个足够大的空闲块
    for (heap_block_t *b = first_block; b; b = b->next) {
        // 【安全检查】验证当前块的有效性
        if ((uintptr_t)b < KERNEL_VIRTUAL_BASE) {
            LOG_ERROR_MSG("kmalloc: invalid block pointer 0x%lx in heap chain!\n", (unsigned long)b);
            return NULL;
        }
        if (b->magic != HEAP_MAGIC) {
            LOG_ERROR_MSG("kmalloc: block 0x%lx has invalid magic 0x%x (expected 0x%x)!\n", (unsigned long)b, b->magic, HEAP_MAGIC);
            return NULL;
        }
        
        if (b->is_free && b->size >= size) {
            b->is_free = false;
            split(b, size);
            void *ptr = (void*)((uintptr_t)b + sizeof(heap_block_t));
            return ptr;
        }
    }
    
    // 没有找到空闲块，扩展堆空间
    uintptr_t old = heap_end;
    if (!expand(size + sizeof(heap_block_t))) {
        return NULL;
    }
    
    // 创建新块
    heap_block_t *new_block = (heap_block_t*)old;
    new_block->size = heap_end - old - sizeof(heap_block_t);
    new_block->is_free = false;
    new_block->magic = HEAP_MAGIC;
    new_block->next = NULL;
    new_block->prev = last_block;
    
    // 链接到末尾
    if (last_block) {
        last_block->next = new_block;
    }
    last_block = new_block;
    
    void *ptr = (void*)((uintptr_t)new_block + sizeof(heap_block_t));
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
    
    sync::SpinlockIrqGuard guard(heap_lock);
    
    // 获取块头指针
    heap_block_t *b = (heap_block_t*)((uintptr_t)ptr - sizeof(heap_block_t));
    // 验证魔数
    if (b->magic != HEAP_MAGIC) {
        LOG_WARN_MSG("kfree: invalid magic at 0x%lx (block 0x%lx), magic=0x%x\n", (unsigned long)ptr, (unsigned long)b, b->magic);
        return;
    }
    b->is_free = true;
    coalesce(b);
}

