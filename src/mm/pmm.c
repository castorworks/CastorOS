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

/*============================================================================
 * DMA 区域定义
 *============================================================================*/

/** DMA 区域上限 (16MB for ISA DMA) */
#define DMA_ZONE_LIMIT      0x01000000ULL   /* 16 MB */

/** 普通区域上限 (896MB for i686, unlimited for 64-bit) */
#if defined(ARCH_I686)
#define NORMAL_ZONE_LIMIT   0x38000000ULL   /* 896 MB */
#else
#define NORMAL_ZONE_LIMIT   PADDR_INVALID   /* No limit on 64-bit */
#endif

/**
 * @brief 获取内存区域的物理地址范围
 * @param zone 内存区域
 * @param[out] start 区域起始地址
 * @param[out] end 区域结束地址
 */
static void get_zone_range(pmm_zone_t zone, paddr_t *start, paddr_t *end) {
    switch (zone) {
        case ZONE_DMA:
            *start = 0;
            *end = DMA_ZONE_LIMIT;
            break;
        case ZONE_NORMAL:
            *start = DMA_ZONE_LIMIT;
            *end = NORMAL_ZONE_LIMIT;
            break;
        case ZONE_HIGH:
#if defined(ARCH_I686)
            *start = NORMAL_ZONE_LIMIT;
            *end = 0x80000000ULL;  /* 2GB limit for i686 */
#else
            *start = 0;
            *end = PADDR_INVALID;  /* No high zone on 64-bit */
#endif
            break;
        default:
            *start = 0;
            *end = PFN_TO_PADDR(total_frames);
            break;
    }
    
    /* Clamp to actual memory size */
    paddr_t max_addr = PFN_TO_PADDR(total_frames);
    if (*end > max_addr || *end == PADDR_INVALID) {
        *end = max_addr;
    }
}

/**
 * @brief 从指定区域分配连续物理页帧（用于 DMA）
 * @param count 页帧数量
 * @param zone 内存区域 (ZONE_DMA 用于 DMA 缓冲区)
 * @return 成功返回起始物理地址，失败返回 PADDR_INVALID
 * 
 * DMA 区域 (ZONE_DMA) 限制在 0-16MB 范围内，适用于 ISA DMA。
 * 
 * @see Requirements 10.1
 */
paddr_t pmm_alloc_frames_zone(size_t count, pmm_zone_t zone) {
    if (count == 0) {
        return PADDR_INVALID;
    }
    
    /* Get zone boundaries */
    paddr_t zone_start, zone_end;
    get_zone_range(zone, &zone_start, &zone_end);
    
    pfn_t start_frame = PADDR_TO_PFN(zone_start);
    pfn_t end_frame = PADDR_TO_PFN(zone_end);
    
    /* Ensure we don't exceed total frames */
    if (end_frame > total_frames) {
        end_frame = total_frames;
    }
    
    if (start_frame >= end_frame) {
        LOG_WARN_MSG("PMM: Zone %d has no available frames\n", zone);
        return PADDR_INVALID;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    /* Search for consecutive free frames within zone */
    pfn_t found_start = PFN_INVALID;
    pfn_t consecutive = 0;
    
    for (pfn_t i = start_frame; i < end_frame; i++) {
        if (!test_frame(i)) {
            if (consecutive == 0) {
                found_start = i;
            }
            consecutive++;
            
            /* Check if we found enough consecutive frames */
            if (consecutive >= count) {
                /* Verify all frames fit within zone */
                paddr_t alloc_end = PFN_TO_PADDR(found_start + count);
                if (alloc_end <= zone_end) {
                    break;
                }
            }
        } else {
            consecutive = 0;
            found_start = PFN_INVALID;
        }
    }
    
    if (consecutive < count || found_start == PFN_INVALID) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        LOG_DEBUG_MSG("PMM: Failed to allocate %zu contiguous frames in zone %d\n", 
                     count, zone);
        return PADDR_INVALID;
    }
    
    /* Mark all frames as used */
    for (size_t i = 0; i < count; i++) {
        pfn_t idx = found_start + i;
        set_frame(idx);
        frame_refcount[idx] = 1;
        pmm_info.free_frames--;
        pmm_info.used_frames++;
    }
    
    paddr_t addr = PFN_TO_PADDR(found_start);
    
    /* Clear all frames */
    memset((void*)PHYS_TO_VIRT(addr), 0, count * PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    
    LOG_DEBUG_MSG("PMM: Allocated %zu contiguous frames at 0x%llx (zone %d)\n",
                 count, (unsigned long long)addr, zone);
    
    return addr;
}

/**
 * @brief 分配连续物理页帧（用于 DMA）
 * @param count 页帧数量
 * @return 成功返回起始物理地址，失败返回 PADDR_INVALID
 * 
 * 此函数从任意可用区域分配连续帧。
 * 如需 DMA 区域分配，请使用 pmm_alloc_frames_zone(count, ZONE_DMA)。
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

/*============================================================================
 * 大页分配实现 (2MB 对齐)
 * @see Requirements 8.1
 *============================================================================*/

/**
 * @brief 查找 2MB 对齐的连续空闲帧
 * @param zone_start 区域起始 PFN
 * @param zone_end 区域结束 PFN
 * @return 成功返回起始 PFN，失败返回 PFN_INVALID
 * 
 * 搜索 512 个连续空闲帧，起始地址必须 2MB 对齐
 */
static pfn_t find_huge_page_frames(pfn_t zone_start, pfn_t zone_end) {
    /* 2MB = 512 * 4KB pages */
    const pfn_t frames_needed = HUGE_PAGE_SIZE / PAGE_SIZE;
    
    /* Align zone_start to 2MB boundary */
    pfn_t aligned_start = (zone_start + frames_needed - 1) & ~(frames_needed - 1);
    
    /* Search for consecutive free frames starting at 2MB boundaries */
    for (pfn_t start = aligned_start; start + frames_needed <= zone_end; 
         start += frames_needed) {
        
        /* Check if all 512 frames are free */
        bool all_free = true;
        for (pfn_t i = 0; i < frames_needed; i++) {
            if (test_frame(start + i)) {
                all_free = false;
                break;
            }
        }
        
        if (all_free) {
            return start;
        }
    }
    
    return PFN_INVALID;
}

/**
 * @brief 分配一个 2MB 大页
 * @return 成功返回 2MB 对齐的物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_huge_page(void) {
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    /* Search for 2MB-aligned consecutive frames */
    pfn_t start_pfn = find_huge_page_frames(0, total_frames);
    
    if (start_pfn == PFN_INVALID) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        LOG_DEBUG_MSG("PMM: Failed to allocate 2MB huge page (no contiguous 2MB-aligned region)\n");
        return PADDR_INVALID;
    }
    
    /* Mark all 512 frames as used */
    const pfn_t frames_count = HUGE_PAGE_SIZE / PAGE_SIZE;
    for (pfn_t i = 0; i < frames_count; i++) {
        pfn_t idx = start_pfn + i;
        set_frame(idx);
        frame_refcount[idx] = 1;
        pmm_info.free_frames--;
        pmm_info.used_frames++;
    }
    
    paddr_t addr = PFN_TO_PADDR(start_pfn);
    
    /* Clear all frames */
    memset((void*)PHYS_TO_VIRT(addr), 0, HUGE_PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    
    LOG_DEBUG_MSG("PMM: Allocated 2MB huge page at 0x%llx\n", (unsigned long long)addr);
    
    return addr;
}

/**
 * @brief 从指定区域分配一个 2MB 大页
 * @param zone 内存区域
 * @return 成功返回 2MB 对齐的物理地址，失败返回 PADDR_INVALID
 */
paddr_t pmm_alloc_huge_page_zone(pmm_zone_t zone) {
    /* Get zone boundaries */
    paddr_t zone_start, zone_end;
    get_zone_range(zone, &zone_start, &zone_end);
    
    pfn_t start_frame = PADDR_TO_PFN(zone_start);
    pfn_t end_frame = PADDR_TO_PFN(zone_end);
    
    /* Ensure we don't exceed total frames */
    if (end_frame > total_frames) {
        end_frame = total_frames;
    }
    
    if (start_frame >= end_frame) {
        LOG_WARN_MSG("PMM: Zone %d has no available frames for huge page\n", zone);
        return PADDR_INVALID;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    /* Search for 2MB-aligned consecutive frames within zone */
    pfn_t start_pfn = find_huge_page_frames(start_frame, end_frame);
    
    if (start_pfn == PFN_INVALID) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        LOG_DEBUG_MSG("PMM: Failed to allocate 2MB huge page in zone %d\n", zone);
        return PADDR_INVALID;
    }
    
    /* Mark all 512 frames as used */
    const pfn_t frames_count = HUGE_PAGE_SIZE / PAGE_SIZE;
    for (pfn_t i = 0; i < frames_count; i++) {
        pfn_t idx = start_pfn + i;
        set_frame(idx);
        frame_refcount[idx] = 1;
        pmm_info.free_frames--;
        pmm_info.used_frames++;
    }
    
    paddr_t addr = PFN_TO_PADDR(start_pfn);
    
    /* Clear all frames */
    memset((void*)PHYS_TO_VIRT(addr), 0, HUGE_PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    
    LOG_DEBUG_MSG("PMM: Allocated 2MB huge page at 0x%llx (zone %d)\n", 
                  (unsigned long long)addr, zone);
    
    return addr;
}

/**
 * @brief 释放一个 2MB 大页
 * @param huge_page 大页的物理地址（必须 2MB 对齐）
 */
void pmm_free_huge_page(paddr_t huge_page) {
    /* Validate alignment */
    if (!pmm_is_huge_page_aligned(huge_page)) {
        LOG_ERROR_MSG("PMM: pmm_free_huge_page: address 0x%llx is not 2MB aligned\n",
                     (unsigned long long)huge_page);
        return;
    }
    
    if (huge_page == 0 || huge_page == PADDR_INVALID) {
        return;
    }
    
    pfn_t start_pfn = PADDR_TO_PFN(huge_page);
    const pfn_t frames_count = HUGE_PAGE_SIZE / PAGE_SIZE;
    
    if (start_pfn + frames_count > total_frames) {
        LOG_ERROR_MSG("PMM: pmm_free_huge_page: address 0x%llx out of range\n",
                     (unsigned long long)huge_page);
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    /* Free all 512 frames */
    for (pfn_t i = 0; i < frames_count; i++) {
        pfn_t idx = start_pfn + i;
        
        if (!test_frame(idx)) {
            LOG_WARN_MSG("PMM: pmm_free_huge_page: frame %llu already free\n",
                        (unsigned long long)idx);
            continue;
        }
        
        /* Check reference count */
        if (frame_refcount[idx] > 1) {
            frame_refcount[idx]--;
            /* Frame still has references, don't free */
            continue;
        }
        
        frame_refcount[idx] = 0;
        clear_frame(idx);
        pmm_info.free_frames++;
        pmm_info.used_frames--;
    }
    
    /* Update search hint */
    pfn_t bitmap_idx = start_pfn / 32;
    if (bitmap_idx < last_free_index) {
        last_free_index = bitmap_idx;
    }
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    
    LOG_DEBUG_MSG("PMM: Freed 2MB huge page at 0x%llx\n", (unsigned long long)huge_page);
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


/*============================================================================
 * 调试功能：PMM 一致性检查
 * @see Requirements 11.2
 *============================================================================*/

/**
 * @brief 验证 PMM 内部数据结构一致性
 * @return 一致性检查通过返回 true，发现问题返回 false
 */
bool pmm_verify_consistency(void) {
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    bool consistent = true;
    pfn_t counted_free = 0;
    pfn_t counted_used = 0;
    pfn_t bitmap_refcount_mismatch = 0;
    pfn_t zero_refcount_used = 0;
    pfn_t nonzero_refcount_free = 0;
    
    kprintf("\n==================== PMM Consistency Check ====================\n");
    
    // 检查 1: 遍历位图，统计空闲和已使用帧数
    for (pfn_t i = 0; i < total_frames; i++) {
        bool is_used = test_frame(i);
        uint16_t refcount = frame_refcount ? frame_refcount[i] : 0;
        
        if (is_used) {
            counted_used++;
            
            // 检查：已使用帧的引用计数应该 >= 1
            if (refcount == 0) {
                zero_refcount_used++;
                if (zero_refcount_used <= 5) {
                    kprintf("  WARNING: Frame %llu is marked USED but refcount=0\n",
                           (unsigned long long)i);
                }
            }
        } else {
            counted_free++;
            
            // 检查：空闲帧的引用计数应该 == 0
            if (refcount != 0) {
                nonzero_refcount_free++;
                if (nonzero_refcount_free <= 5) {
                    kprintf("  WARNING: Frame %llu is marked FREE but refcount=%u\n",
                           (unsigned long long)i, refcount);
                }
            }
        }
    }
    
    // 检查 2: 验证统计信息
    kprintf("Bitmap scan results:\n");
    kprintf("  Counted free frames:  %llu\n", (unsigned long long)counted_free);
    kprintf("  Counted used frames:  %llu\n", (unsigned long long)counted_used);
    kprintf("  Recorded free frames: %llu\n", (unsigned long long)pmm_info.free_frames);
    kprintf("  Recorded used frames: %llu\n", (unsigned long long)pmm_info.used_frames);
    kprintf("  Total frames:         %llu\n", (unsigned long long)total_frames);
    
    if (counted_free != pmm_info.free_frames) {
        kprintf("  ERROR: Free frame count mismatch! (counted=%llu, recorded=%llu)\n",
               (unsigned long long)counted_free, (unsigned long long)pmm_info.free_frames);
        consistent = false;
    }
    
    if (counted_used != pmm_info.used_frames) {
        kprintf("  ERROR: Used frame count mismatch! (counted=%llu, recorded=%llu)\n",
               (unsigned long long)counted_used, (unsigned long long)pmm_info.used_frames);
        consistent = false;
    }
    
    if (counted_free + counted_used != total_frames) {
        kprintf("  ERROR: Total frame count mismatch! (free+used=%llu, total=%llu)\n",
               (unsigned long long)(counted_free + counted_used), 
               (unsigned long long)total_frames);
        consistent = false;
    }
    
    // 检查 3: 位图和引用计数一致性
    kprintf("\nBitmap/Refcount consistency:\n");
    kprintf("  Used frames with refcount=0:    %llu\n", (unsigned long long)zero_refcount_used);
    kprintf("  Free frames with refcount!=0:   %llu\n", (unsigned long long)nonzero_refcount_free);
    
    bitmap_refcount_mismatch = zero_refcount_used + nonzero_refcount_free;
    if (bitmap_refcount_mismatch > 0) {
        kprintf("  WARNING: %llu bitmap/refcount mismatches detected\n",
               (unsigned long long)bitmap_refcount_mismatch);
        // 这不一定是错误，可能是正常的 COW 操作中间状态
    }
    
    // 检查 4: 保护帧列表
    kprintf("\nProtected frames:\n");
    kprintf("  Protected frame count: %u\n", protected_frame_count);
    
    uint32_t invalid_protected = 0;
    for (uint32_t i = 0; i < protected_frame_count; i++) {
        paddr_t frame = protected_frames[i].frame;
        pfn_t idx = PADDR_TO_PFN(frame);
        
        if (idx >= total_frames) {
            kprintf("  ERROR: Protected frame 0x%llx is out of range\n",
                   (unsigned long long)frame);
            invalid_protected++;
            consistent = false;
        } else if (!test_frame(idx)) {
            kprintf("  WARNING: Protected frame 0x%llx is marked FREE in bitmap\n",
                   (unsigned long long)frame);
            invalid_protected++;
        }
    }
    
    if (invalid_protected > 0) {
        kprintf("  Found %u invalid protected frames\n", invalid_protected);
    }
    
    // 检查 5: 堆保留区域
    if (heap_reserved_phys_start != 0 || heap_reserved_phys_end != 0) {
        kprintf("\nHeap reserved range:\n");
        kprintf("  Physical: 0x%llx - 0x%llx\n",
               (unsigned long long)heap_reserved_phys_start,
               (unsigned long long)heap_reserved_phys_end);
        
        pfn_t start_frame = PADDR_TO_PFN(heap_reserved_phys_start);
        pfn_t end_frame = PADDR_TO_PFN(PADDR_ALIGN_UP(heap_reserved_phys_end));
        uint32_t unreserved_heap_frames = 0;
        
        for (pfn_t f = start_frame; f < end_frame && f < total_frames; f++) {
            if (!test_frame(f)) {
                unreserved_heap_frames++;
            }
        }
        
        if (unreserved_heap_frames > 0) {
            kprintf("  WARNING: %u frames in heap range are not marked as used\n",
                   unreserved_heap_frames);
        }
    }
    
    // 最终结果
    kprintf("\n--------------------------------------------------------------\n");
    if (consistent) {
        kprintf("PMM Consistency Check: PASSED\n");
    } else {
        kprintf("PMM Consistency Check: FAILED\n");
    }
    kprintf("==============================================================\n\n");
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return consistent;
}

/**
 * @brief 打印 PMM 详细诊断信息
 */
void pmm_print_diagnostics(void) {
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    kprintf("\n==================== PMM Diagnostics ====================\n");
    
    // 基本信息
    kprintf("Memory Configuration:\n");
    kprintf("  Total frames:     %llu (%llu MB)\n", 
           (unsigned long long)total_frames,
           (unsigned long long)(total_frames * PAGE_SIZE / (1024*1024)));
    kprintf("  Free frames:      %llu (%llu MB)\n",
           (unsigned long long)pmm_info.free_frames,
           (unsigned long long)(pmm_info.free_frames * PAGE_SIZE / (1024*1024)));
    kprintf("  Used frames:      %llu (%llu MB)\n",
           (unsigned long long)pmm_info.used_frames,
           (unsigned long long)(pmm_info.used_frames * PAGE_SIZE / (1024*1024)));
    kprintf("  Reserved frames:  %llu\n", (unsigned long long)pmm_info.reserved_frames);
    kprintf("  Kernel frames:    %llu\n", (unsigned long long)pmm_info.kernel_frames);
    kprintf("  Bitmap frames:    %llu\n", (unsigned long long)pmm_info.bitmap_frames);
    
    // 位图信息
    kprintf("\nBitmap Information:\n");
    kprintf("  Bitmap address:   %p\n", frame_bitmap);
    kprintf("  Bitmap size:      %llu words (%llu bytes)\n",
           (unsigned long long)bitmap_size,
           (unsigned long long)(bitmap_size * 4));
    kprintf("  Search hint:      %llu\n", (unsigned long long)last_free_index);
    
    // 引用计数信息
    kprintf("\nReference Count Information:\n");
    kprintf("  Refcount address: %p\n", frame_refcount);
    
    // 统计引用计数分布
    uint32_t refcount_dist[5] = {0};  // 0, 1, 2, 3, 4+
    pfn_t max_refcount = 0;
    pfn_t max_refcount_frame = 0;
    
    for (pfn_t i = 0; i < total_frames; i++) {
        uint16_t rc = frame_refcount ? frame_refcount[i] : 0;
        if (rc < 4) {
            refcount_dist[rc]++;
        } else {
            refcount_dist[4]++;
        }
        if (rc > max_refcount) {
            max_refcount = rc;
            max_refcount_frame = i;
        }
    }
    
    kprintf("  Refcount distribution:\n");
    kprintf("    refcount=0: %u frames\n", refcount_dist[0]);
    kprintf("    refcount=1: %u frames\n", refcount_dist[1]);
    kprintf("    refcount=2: %u frames\n", refcount_dist[2]);
    kprintf("    refcount=3: %u frames\n", refcount_dist[3]);
    kprintf("    refcount>=4: %u frames\n", refcount_dist[4]);
    kprintf("  Max refcount: %llu (frame %llu, phys 0x%llx)\n",
           (unsigned long long)max_refcount,
           (unsigned long long)max_refcount_frame,
           (unsigned long long)PFN_TO_PADDR(max_refcount_frame));
    
    // 保护帧信息
    kprintf("\nProtected Frames:\n");
    kprintf("  Count: %u / %u\n", protected_frame_count, MAX_PROTECTED_FRAMES);
    if (protected_frame_count > 0 && protected_frame_count <= 10) {
        for (uint32_t i = 0; i < protected_frame_count; i++) {
            kprintf("    [%u] phys=0x%llx, protect_refcount=%u\n",
                   i, (unsigned long long)protected_frames[i].frame,
                   protected_frames[i].refcount);
        }
    } else if (protected_frame_count > 10) {
        kprintf("    (showing first 10)\n");
        for (uint32_t i = 0; i < 10; i++) {
            kprintf("    [%u] phys=0x%llx, protect_refcount=%u\n",
                   i, (unsigned long long)protected_frames[i].frame,
                   protected_frames[i].refcount);
        }
    }
    
    // 堆保留区域
    if (heap_reserved_phys_start != 0 || heap_reserved_phys_end != 0) {
        kprintf("\nHeap Reserved Range:\n");
        kprintf("  Physical: 0x%llx - 0x%llx\n",
               (unsigned long long)heap_reserved_phys_start,
               (unsigned long long)heap_reserved_phys_end);
        kprintf("  Size: %llu KB\n",
               (unsigned long long)((heap_reserved_phys_end - heap_reserved_phys_start) / 1024));
    }
    
    kprintf("=========================================================\n\n");
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
}
