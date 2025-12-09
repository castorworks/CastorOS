/**
 * @file pmm.c
 * @brief 物理内存管理器实现（重构版）
 * 
 * 使用位图跟踪物理页帧的分配状态。
 * 支持 64-bit 物理地址，兼容 i686、x86_64 和 ARM64 架构。
 * 
 * @see Requirements 2.2, 2.3, 2.4, 2.5
 */

#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/sync/spinlock.h>

#define MAX_PROTECTED_FRAMES 65536

typedef struct {
    paddr_t frame;
    uint32_t refcount;
} protected_frame_t;

static uint32_t *frame_bitmap = NULL;     ///< 页帧位图
static pfn_t bitmap_size = 0;             ///< 位图大小（32位字数量）
static pfn_t total_frames = 0;            ///< 总页帧数
static pmm_info_t pmm_info = {0};         ///< 物理内存信息
static pfn_t last_free_index = 0;         ///< 上次分配的空闲页帧索引（优化搜索）
static spinlock_t pmm_lock;               ///< PMM 自旋锁
static protected_frame_t protected_frames[MAX_PROTECTED_FRAMES];
static uint32_t protected_frame_count = 0;
static uint16_t *frame_refcount = NULL;   ///< 页帧引用计数数组（每帧2字节，最大65535引用）
static uintptr_t pmm_data_end_virt = 0;   ///< PMM 数据结构结束的虚拟地址（位图+引用计数表）
extern char _kernel_end[];                ///< 内核结束地址

// 堆保留区域：物理地址在此范围内的帧不会被分配，避免与堆虚拟地址重叠
static paddr_t heap_reserved_phys_start = 0;  ///< 堆保留区物理起始地址
static paddr_t heap_reserved_phys_end = 0;    ///< 堆保留区物理结束地址

static inline protected_frame_t* find_protected_frame_unsafe(paddr_t frame) {
    for (uint32_t i = 0; i < protected_frame_count; i++) {
        if (protected_frames[i].frame == frame) {
            return &protected_frames[i];
        }
    }
    return NULL;
}

/**
 * @brief 标记页帧为已使用
 * @param idx 页帧索引 (pfn_t)
 */
static inline void set_frame(pfn_t idx) {
    pfn_t bitmap_idx = idx / 32;
    uint32_t bit_idx = idx % 32;
    
    // 边界检查
    if (bitmap_idx >= bitmap_size) {
        LOG_ERROR_MSG("PMM: set_frame(%llu): bitmap_idx %llu >= bitmap_size %llu!\n",
                     (unsigned long long)idx, (unsigned long long)bitmap_idx, 
                     (unsigned long long)bitmap_size);
        return;
    }
    
    frame_bitmap[bitmap_idx] |= (1U << bit_idx);
}

/**
 * @brief 标记页帧为空闲
 * @param idx 页帧索引 (pfn_t)
 */
static inline void clear_frame(pfn_t idx) {
    frame_bitmap[idx/32] &= ~(1U << (idx%32));
}

/**
 * @brief 检查页帧是否已使用
 * @param idx 页帧索引 (pfn_t)
 * @return 已使用返回 true，空闲返回 false
 */
static inline bool test_frame(pfn_t idx) {
    pfn_t bitmap_idx = idx / 32;
    uint32_t bit_idx = idx % 32;
    
    // 边界检查
    if (bitmap_idx >= bitmap_size) {
        LOG_ERROR_MSG("PMM: test_frame(%llu): bitmap_idx %llu >= bitmap_size %llu!\n",
                     (unsigned long long)idx, (unsigned long long)bitmap_idx, 
                     (unsigned long long)bitmap_size);
        return false;
    }
    
    return (frame_bitmap[bitmap_idx] & (1U << bit_idx)) != 0;
}

/**
 * @brief 查找空闲页帧
 * @return 成功返回页帧索引，失败返回 PFN_INVALID
 */
static pfn_t find_free_frame(void) {
    // 从上次分配的位置开始搜索，优化性能
    for (pfn_t i = last_free_index; i < bitmap_size; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t bit = 0; bit < 32; bit++) {
                if (!(frame_bitmap[i] & (1 << bit))) {
                    pfn_t idx = i * 32 + bit;
                    if (idx < total_frames) {
                        last_free_index = i;
                        return idx;
                    }
                }
            }
        }
    }
    
    // 如果后面没有找到，从头开始搜索
    if (last_free_index > 0) {
        for (pfn_t i = 0; i < last_free_index; i++) {
            if (frame_bitmap[i] != 0xFFFFFFFF) {
                for (uint32_t bit = 0; bit < 32; bit++) {
                    if (!(frame_bitmap[i] & (1 << bit))) {
                        pfn_t idx = i * 32 + bit;
                        if (idx < total_frames) {
                            last_free_index = i;
                            return idx;
                        }
                    }
                }
            }
        }
    }
    
    return PFN_INVALID;
}

/**
 * @brief 将物理帧加入保护列表
 */
void pmm_protect_frame(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID) {
        return;
    }
    if (!IS_PADDR_ALIGNED(frame)) {
        LOG_WARN_MSG("PMM: pmm_protect_frame received unaligned frame 0x%llx, aligning down\n", 
                    (unsigned long long)frame);
        frame = PADDR_ALIGN_DOWN(frame);
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    // 确保受保护的帧在位图中被标记为已使用
    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx < total_frames && !test_frame(idx)) {
        LOG_WARN_MSG("PMM: Protecting frame 0x%llx that was FREE in bitmap! Marking as used.\n", 
                    (unsigned long long)frame);
        set_frame(idx);
        pmm_info.free_frames--;
        pmm_info.used_frames++;
        frame_refcount[idx] = 1;
    }
    
    protected_frame_t *entry = find_protected_frame_unsafe(frame);
    if (entry) {
        entry->refcount++;
    } else if (protected_frame_count < MAX_PROTECTED_FRAMES) {
        protected_frames[protected_frame_count].frame = frame;
        protected_frames[protected_frame_count].refcount = 1;
        protected_frame_count++;
    } else {
        LOG_ERROR_MSG("PMM: Protected frame table full! Cannot protect 0x%llx\n", 
                     (unsigned long long)frame);
    }
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
}

/**
 * @brief 将物理帧从保护列表移除
 */
void pmm_unprotect_frame(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID) {
        return;
    }
    if (!IS_PADDR_ALIGNED(frame)) {
        LOG_WARN_MSG("PMM: pmm_unprotect_frame received unaligned frame 0x%llx, aligning down\n", 
                    (unsigned long long)frame);
        frame = PADDR_ALIGN_DOWN(frame);
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    protected_frame_t *entry = find_protected_frame_unsafe(frame);
    if (!entry) {
        LOG_WARN_MSG("PMM: Attempted to unprotect unknown frame 0x%llx\n", 
                    (unsigned long long)frame);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    if (entry->refcount == 0) {
        LOG_ERROR_MSG("PMM: Frame 0x%llx has zero refcount while protected!\n", 
                     (unsigned long long)frame);
    } else {
        entry->refcount--;
    }
    
    if (entry->refcount == 0) {
        protected_frame_count--;
        *entry = protected_frames[protected_frame_count];
    }
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
}

/**
 * @brief 查询物理帧是否处于保护状态
 */
bool pmm_is_frame_protected(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID) {
        return false;
    }
    if (!IS_PADDR_ALIGNED(frame)) {
        frame = PADDR_ALIGN_DOWN(frame);
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    bool protected_flag = (find_protected_frame_unsafe(frame) != NULL);
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return protected_flag;
}

/**
 * @brief 初始化物理内存管理器
 * @param mbi Multiboot信息结构指针
 * 
 * 解析内存映射，初始化位图，标记已使用和空闲的页帧
 */
void pmm_init(multiboot_info_t *mbi) {
    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
        PANIC("No memory map");
    
    memset(&pmm_info, 0, sizeof(pmm_info_t));
    paddr_t kernel_start = 0x100000;  // 1MB，内核加载位置
    paddr_t kernel_end = PAGE_ALIGN_UP(VIRT_TO_PHYS((uintptr_t)_kernel_end));
    
    // 计算总内存大小
    multiboot_memory_map_t *mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(mbi->mmap_addr);
    multiboot_memory_map_t *mmap_end = (multiboot_memory_map_t*)
        PHYS_TO_VIRT(mbi->mmap_addr + mbi->mmap_length);
    
    paddr_t max_addr = 0;
    while (mmap < mmap_end) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            paddr_t end = mmap->addr + mmap->len;
            
#if defined(ARCH_I686)
            // i686: 限制物理内存到 2GB（高半核设计限制）
            if (end > 0x80000000ULL) {
                if (max_addr < 0x80000000ULL) {
                    LOG_WARN_MSG("Physical memory exceeds 2GB, truncating to 2GB\n");
                    LOG_WARN_MSG("  Total physical memory: %llu MB\n", 
                                (unsigned long long)(end / (1024*1024)));
                    LOG_WARN_MSG("  Usable physical memory: 2048 MB\n");
                }
                end = 0x80000000ULL;
            }
#elif defined(ARCH_X86_64)
            // x86_64: 支持完整物理地址空间（最大 4PB）
            // 但实际受限于可用内存映射
#endif
            
            if (end > max_addr) max_addr = end;
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }
    
    total_frames = max_addr / PAGE_SIZE;
    pmm_info.total_frames = total_frames;
    
    // 初始化 PMM 锁
    spinlock_init(&pmm_lock);
    
    // 初始化位图（默认所有页帧已使用）
    pfn_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 31) / 32 * 4);
    uintptr_t kernel_end_virt = (uintptr_t)_kernel_end;
    uintptr_t bitmap_virt = PAGE_ALIGN_UP(kernel_end_virt);
    LOG_INFO_MSG("PMM: kernel_end_virt = 0x%llx\n", (unsigned long long)kernel_end_virt);
    LOG_INFO_MSG("PMM: bitmap_virt (after PAGE_ALIGN_UP) = 0x%llx\n", (unsigned long long)bitmap_virt);
    frame_bitmap = (uint32_t*)bitmap_virt;
    bitmap_size = bitmap_bytes / 4;
    memset(frame_bitmap, 0xFF, bitmap_bytes);

    // 计算位图占用的物理地址范围
    paddr_t bitmap_phys_start = VIRT_TO_PHYS((uintptr_t)frame_bitmap);
    paddr_t bitmap_end_phys = PAGE_ALIGN_UP(bitmap_phys_start + bitmap_bytes);
    
    // 初始化引用计数表（紧跟位图之后）
    pfn_t refcount_bytes = PAGE_ALIGN_UP(total_frames * sizeof(uint16_t));
    frame_refcount = (uint16_t*)PHYS_TO_VIRT(bitmap_end_phys);
    memset(frame_refcount, 0, refcount_bytes);
    paddr_t refcount_end_phys = PAGE_ALIGN_UP(bitmap_end_phys + refcount_bytes);
    
    // 保存 PMM 数据结构结束地址（虚拟地址），供堆初始化使用
    pmm_data_end_virt = PHYS_TO_VIRT(refcount_end_phys);
    LOG_INFO_MSG("PMM: DEBUG refcount_end_phys=0x%llx, KERNEL_VIRTUAL_BASE=0x%llx\n",
                 (unsigned long long)refcount_end_phys, (unsigned long long)KERNEL_VIRTUAL_BASE);
    LOG_INFO_MSG("PMM: DEBUG PHYS_TO_VIRT result=0x%llx\n", 
                 (unsigned long long)PHYS_TO_VIRT(refcount_end_phys));
    
    LOG_INFO_MSG("PMM: _kernel_end = %p (0x%llx)\n", _kernel_end, (unsigned long long)(uintptr_t)_kernel_end);
    LOG_INFO_MSG("PMM: frame_bitmap = %p (virt)\n", frame_bitmap);
    LOG_INFO_MSG("PMM: bitmap_phys_start = 0x%llx\n", (unsigned long long)bitmap_phys_start);
    LOG_INFO_MSG("PMM: bitmap_end_phys = 0x%llx\n", (unsigned long long)bitmap_end_phys);
    LOG_INFO_MSG("PMM: frame_refcount = %p (virt)\n", frame_refcount);
    LOG_INFO_MSG("PMM: refcount_end_phys = 0x%llx\n", (unsigned long long)refcount_end_phys);
    LOG_INFO_MSG("PMM: pmm_data_end_virt = 0x%llx\n", (unsigned long long)pmm_data_end_virt);
    LOG_INFO_MSG("PMM: KERNEL_VIRTUAL_BASE = 0x%llx\n", (unsigned long long)KERNEL_VIRTUAL_BASE);
    
    LOG_DEBUG_MSG("PMM: Frame refcount table at virt=%p, phys=0x%llx, size=%llu bytes\n",
                 frame_refcount, (unsigned long long)bitmap_end_phys, (unsigned long long)refcount_bytes);
    LOG_DEBUG_MSG("PMM: Refcount table ends at phys=0x%llx (virt=0x%llx)\n", 
                 (unsigned long long)refcount_end_phys, (unsigned long long)pmm_data_end_virt);
    
    // 标记引用计数表占用的帧为已使用
    pfn_t refcount_start_frame = PADDR_TO_PFN(bitmap_end_phys);
    pfn_t refcount_end_frame = PADDR_TO_PFN(refcount_end_phys);
    LOG_DEBUG_MSG("PMM: Marking refcount table frames %llu-%llu as used\n", 
                 (unsigned long long)refcount_start_frame, (unsigned long long)(refcount_end_frame - 1));
    for (pfn_t f = refcount_start_frame; f < refcount_end_frame; f++) {
        if (f < total_frames) {
            set_frame(f);
        }
    }

    // 标记空闲内存区域
    mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(mbi->mmap_addr);
    
    while (mmap < mmap_end) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            paddr_t start = PADDR_ALIGN_UP(mmap->addr);
            paddr_t end = PADDR_ALIGN_DOWN(mmap->addr + mmap->len);
            
#if defined(ARCH_I686)
            // i686: 强制限制不处理超过 2GB 的物理内存
            if (end > 0x80000000ULL) {
                end = 0x80000000ULL;
            }
#endif
            
            // 跳过内核、位图和引用计数表占用的区域
            if (start < kernel_end) start = kernel_end;
            if (start < refcount_end_phys) start = refcount_end_phys;
            
            if (end > start) {
                for (pfn_t f = PADDR_TO_PFN(start); f < PADDR_TO_PFN(end); f++) {
                    clear_frame(f);
                    pmm_info.free_frames++;
                }
            }
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }
    
    // 处理 Multiboot 模块（如 initrd），标记为已使用
    if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count > 0) {
        multiboot_module_t *mod = (multiboot_module_t*)PHYS_TO_VIRT(mbi->mods_addr);
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            pfn_t start_frame = PADDR_TO_PFN(PADDR_ALIGN_DOWN(mod[i].mod_start));
            pfn_t end_frame = PADDR_TO_PFN(PADDR_ALIGN_UP(mod[i].mod_end));
            
            for (pfn_t f = start_frame; f < end_frame; f++) {
                if (f < total_frames && !test_frame(f)) {
                    set_frame(f);
                    pmm_info.free_frames--;
                }
            }
        }
    }

    pmm_info.used_frames = total_frames - pmm_info.free_frames;
    
    // 计算内核占用的页帧数（从 1MB 到 kernel_end）
    pmm_info.kernel_frames = PADDR_TO_PFN(kernel_end - kernel_start);
    
    // 计算位图占用的页帧数
    pmm_info.bitmap_frames = PADDR_TO_PFN(bitmap_end_phys - bitmap_phys_start);
    
    // 计算引用计数表占用的页帧数
    pfn_t refcount_frames = PADDR_TO_PFN(refcount_end_phys - bitmap_end_phys);
    
    // 保留页帧数 = 内核 + 位图 + 引用计数表
    pmm_info.reserved_frames = pmm_info.kernel_frames + pmm_info.bitmap_frames + refcount_frames;
    
    LOG_DEBUG_MSG("PMM: Reserved frames: kernel=%llu, bitmap=%llu, refcount=%llu, total=%llu\n",
                 (unsigned long long)pmm_info.kernel_frames, 
                 (unsigned long long)pmm_info.bitmap_frames, 
                 (unsigned long long)refcount_frames, 
                 (unsigned long long)pmm_info.reserved_frames);
    
    // 初始化引用计数：所有已使用的帧设置为 1
    for (pfn_t i = 0; i < total_frames; i++) {
        if (test_frame(i)) {
            frame_refcount[i] = 1;
        } else {
            frame_refcount[i] = 0;
        }
    }
    
    pmm_print_info();
}

/**
 * @brief 分配一个物理页帧
 * @return 成功返回页帧的物理地址，失败返回 PADDR_INVALID
 * 
 * 分配后会清零页帧内容
 */
paddr_t pmm_alloc_frame(void) {
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);

    pfn_t idx = find_free_frame();
    if (idx == PFN_INVALID) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return PADDR_INVALID;
    }
    
    // 安全检查：确保找到的帧确实是空闲的
    if (test_frame(idx)) {
        LOG_ERROR_MSG("PMM: CRITICAL: find_free_frame returned used frame %llu!\n", 
                     (unsigned long long)idx);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return PADDR_INVALID;
    }
    
    set_frame(idx);
    pmm_info.free_frames--;
    pmm_info.used_frames++;
    
    // 设置引用计数为 1（新分配）
    frame_refcount[idx] = 1;
    
    // 安全检查：验证 set_frame 是否生效
    if (!test_frame(idx)) {
        pfn_t bitmap_idx = idx / 32;
        LOG_ERROR_MSG("PMM: CRITICAL: set_frame(%llu) failed! Frame still shows as free!\n", 
                     (unsigned long long)idx);
        LOG_ERROR_MSG("  idx=%llu, bitmap_idx=%llu, bitmap_size=%llu, total_frames=%llu\n",
                     (unsigned long long)idx, (unsigned long long)bitmap_idx, 
                     (unsigned long long)bitmap_size, (unsigned long long)total_frames);
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return PADDR_INVALID;
    }
    
    paddr_t addr = PFN_TO_PADDR(idx);
    
#if defined(ARCH_I686)
    // i686: 安全检查确保物理地址在可映射范围内
    if (addr >= 0x80000000ULL) {
        LOG_ERROR_MSG("PMM: Allocated frame beyond 2GB (0x%llx), this should not happen!\n", 
                     (unsigned long long)addr);
        clear_frame(idx);
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return PADDR_INVALID;
    }
#endif
    
    // 诊断日志：记录页目录区域的分配
    if (addr >= 0x00190000 && addr < 0x001b0000) {
        LOG_WARN_MSG("PMM: Allocated frame 0x%llx in page directory danger zone\n", 
                    (unsigned long long)addr);
    }
    
    // 关键安全检查：确保我们没有分配一个受保护的帧
    if (find_protected_frame_unsafe(addr)) {
        LOG_ERROR_MSG("PMM: CRITICAL! Allocated frame 0x%llx is protected!\n", 
                     (unsigned long long)addr);
        clear_frame(idx);
        frame_refcount[idx] = 0;
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return PADDR_INVALID;
    }
    
    // 清零页帧内容
    memset((void*)PHYS_TO_VIRT(addr), 0, PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return addr;
}

/**
 * @brief 从指定区域分配物理页帧
 * @param zone 内存区域
 * @return 成功返回物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_frame_zone(pmm_zone_t zone) {
    // TODO: 实现区域分配
    // 目前简单地调用普通分配
    (void)zone;
    return pmm_alloc_frame();
}

/**
 * @brief 分配连续物理页帧（用于 DMA）
 * @param count 页帧数量
 * @return 成功返回起始物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_frames(size_t count) {
    if (count == 0) {
        return PADDR_INVALID;
    }
    if (count == 1) {
        return pmm_alloc_frame();
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    // 搜索连续的空闲帧
    pfn_t start_pfn = PFN_INVALID;
    pfn_t consecutive = 0;
    
    for (pfn_t i = 0; i < total_frames; i++) {
        if (!test_frame(i)) {
            if (consecutive == 0) {
                start_pfn = i;
            }
            consecutive++;
            if (consecutive >= count) {
                // 找到足够的连续帧
                break;
            }
        } else {
            consecutive = 0;
            start_pfn = PFN_INVALID;
        }
    }
    
    if (consecutive < count) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return PADDR_INVALID;
    }
    
    // 标记所有帧为已使用
    for (pfn_t i = 0; i < count; i++) {
        pfn_t idx = start_pfn + i;
        set_frame(idx);
        frame_refcount[idx] = 1;
        pmm_info.free_frames--;
        pmm_info.used_frames++;
    }
    
    paddr_t addr = PFN_TO_PADDR(start_pfn);
    
    // 清零所有页帧
    memset((void*)PHYS_TO_VIRT(addr), 0, count * PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return addr;
}

/**
 * @brief 释放一个物理页帧
 * @param frame 页帧的物理地址
 * 
 * COW 支持：如果帧的引用计数 > 1，只递减计数，不实际释放
 */
void pmm_free_frame(paddr_t frame) {
    // 检查无效地址
    if (frame == 0 || frame == PADDR_INVALID) {
        return;
    }
    // 检查页对齐
    if (!IS_PADDR_ALIGNED(frame)) {
        return;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);

    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);

    // 检查有效性和状态
    if (idx >= total_frames) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    if (!test_frame(idx)) {
        // 仅在可能是有效内存区域时发出警告
        if (frame > 0x100000) {
            LOG_WARN_MSG("PMM: Double free or freeing unused frame 0x%llx (idx %llu)\n", 
                        (unsigned long long)frame, (unsigned long long)idx);
        }
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    if (find_protected_frame_unsafe(frame)) {
        LOG_ERROR_MSG("PMM: Attempt to free protected frame 0x%llx blocked\n", 
                     (unsigned long long)frame);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    // COW: 检查并递减引用计数
    if (frame_refcount[idx] == 0) {
        LOG_WARN_MSG("PMM: Frame 0x%llx marked as used but refcount is 0\n", 
                    (unsigned long long)frame);
    } else {
        frame_refcount[idx]--;
        if (frame_refcount[idx] > 0) {
            // 引用计数还不为 0，说明还有其他进程通过 COW 共享此帧
            spinlock_unlock_irqrestore(&pmm_lock, irq_state);
            return;
        }
    }
    
    // 引用计数为 0，真正释放
    clear_frame(idx);
    pmm_info.free_frames++;
    pmm_info.used_frames--;
    
    // 更新搜索游标
    pfn_t bitmap_idx = idx / 32;
    if (bitmap_idx < last_free_index) {
        last_free_index = bitmap_idx;
    }
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
}

/**
 * @brief 释放连续物理页帧
 * @param frame 起始物理地址
 * @param count 页帧数量
 */
void pmm_free_frames(paddr_t frame, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_frame(frame + i * PAGE_SIZE);
    }
}

/**
 * @brief 获取物理内存信息
 * @return 物理内存信息结构
 */
pmm_info_t pmm_get_info(void) {
    return pmm_info;
}

/**
 * @brief 获取 PMM 数据结构结束地址（虚拟地址）
 * @return PMM 数据结构结束的虚拟地址（页对齐）
 */
uintptr_t pmm_get_bitmap_end(void) {
    if (pmm_data_end_virt != 0) {
        return pmm_data_end_virt;
    }
    
    // 回退：如果还没有初始化，使用旧的计算方式
    pfn_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 31) / 32 * 4);
    return PAGE_ALIGN_UP((uintptr_t)frame_bitmap + bitmap_bytes);
}

/**
 * @brief 设置堆保留区域的物理地址范围
 */
void pmm_set_heap_reserved_range(uintptr_t heap_virt_start, uintptr_t heap_virt_end) {
    if (heap_virt_start >= KERNEL_VIRTUAL_BASE && heap_virt_end > heap_virt_start) {
        heap_reserved_phys_start = VIRT_TO_PHYS(heap_virt_start);
        heap_reserved_phys_end = VIRT_TO_PHYS(heap_virt_end);
        
        pfn_t start_frame = PADDR_TO_PFN(heap_reserved_phys_start);
        pfn_t end_frame = PADDR_TO_PFN(PADDR_ALIGN_UP(heap_reserved_phys_end));
        
        pfn_t reserved_count = 0;
        for (pfn_t f = start_frame; f < end_frame && f < total_frames; f++) {
            if (!test_frame(f)) {
                set_frame(f);
                pmm_info.free_frames--;
                pmm_info.used_frames++;
                frame_refcount[f] = 1;
                reserved_count++;
            }
        }
        
        LOG_INFO_MSG("PMM: Reserved heap range: phys 0x%llx - 0x%llx (%llu frames, %llu newly reserved)\n",
                    (unsigned long long)heap_reserved_phys_start, 
                    (unsigned long long)heap_reserved_phys_end,
                    (unsigned long long)(end_frame - start_frame), 
                    (unsigned long long)reserved_count);
    }
}

/**
 * @brief 打印物理内存使用信息
 */
void pmm_print_info(void) {
    kprintf("\n=============================== Physical Memory ================================\n");
    kprintf("Total: %llu MB\n", (unsigned long long)((pmm_info.total_frames * PAGE_SIZE) / (1024*1024)));
    kprintf("Free:  %llu MB\n", (unsigned long long)((pmm_info.free_frames * PAGE_SIZE) / (1024*1024)));
    kprintf("Used:  %llu MB\n", (unsigned long long)((pmm_info.used_frames * PAGE_SIZE) / (1024*1024)));
    kprintf("================================================================================\n\n");
}

/**
 * @brief 增加物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_inc(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID || !IS_PADDR_ALIGNED(frame)) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc invalid frame 0x%llx\n", 
                     (unsigned long long)frame);
        return 0;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx >= total_frames) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc frame 0x%llx out of range (idx=%llu, total=%llu)\n", 
                     (unsigned long long)frame, (unsigned long long)idx, 
                     (unsigned long long)total_frames);
        return 0;
    }
    
    if (!frame_refcount) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc called but frame_refcount not initialized!\n");
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    if (frame_refcount[idx] == 0xFFFF) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc frame 0x%llx refcount overflow!\n", 
                     (unsigned long long)frame);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0xFFFF;
    }
    
    frame_refcount[idx]++;
    uint32_t new_count = frame_refcount[idx];
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return new_count;
}

/**
 * @brief 减少物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_dec(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID || !IS_PADDR_ALIGNED(frame)) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_dec invalid frame 0x%llx\n", 
                     (unsigned long long)frame);
        return 0;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx >= total_frames) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_dec frame 0x%llx out of range\n", 
                     (unsigned long long)frame);
        return 0;
    }
    
    if (!frame_refcount) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_dec called but frame_refcount not initialized!\n");
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    if (frame_refcount[idx] == 0) {
        LOG_WARN_MSG("PMM: pmm_frame_ref_dec frame 0x%llx already zero!\n", 
                    (unsigned long long)frame);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    frame_refcount[idx]--;
    uint32_t new_count = frame_refcount[idx];
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return new_count;
}

/**
 * @brief 获取物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 引用计数值
 */
uint32_t pmm_frame_get_refcount(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID || !IS_PADDR_ALIGNED(frame)) {
        return 0;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx >= total_frames) {
        return 0;
    }
    
    if (!frame_refcount) {
        return 1;  // 未初始化时假设引用计数为 1
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    uint32_t count = frame_refcount[idx];
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    
    return count;
}
