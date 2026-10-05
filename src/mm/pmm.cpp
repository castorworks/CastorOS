/**
 * @file pmm.c
 * @brief 物理内存管理器实现（重构版）
 * 
 * 使用位图跟踪物理页帧的分配状态。
 * 支持 64-bit 物理地址，兼容 i686、x86_64 和 ARM64 架构。
 */

#include <mm/pmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/sync/spinlock.h>

static uint32_t *frame_bitmap = NULL;     ///< 页帧位图
static pfn_t bitmap_size = 0;             ///< 位图大小（32位字数量）
static pfn_t total_frames = 0;            ///< 总页帧数
static mm::PmmInfo pmm_info = {};         ///< 物理内存信息
static pfn_t last_free_index = 0;         ///< 上次分配的空闲页帧索引（优化搜索）
static sync::Spinlock pmm_lock;               ///< PMM 自旋锁
static uint16_t *frame_refcount = NULL;   ///< 页帧引用计数数组（每帧2字节，最大65535引用）
static uintptr_t pmm_data_end_virt = 0;   ///< PMM 数据结构结束的虚拟地址（位图+引用计数表）
extern char _kernel_end[];                ///< 内核结束地址

// 堆保留区域：物理地址在此范围内的帧不会被分配，避免与堆虚拟地址重叠

// 固件报告为可用内存的区域（Multiboot 内存映射或设备树）。区域之间的空洞是设备内存或保留区
#define PMM_MAX_RAM_REGIONS 32
static struct {
    paddr_t start;
    paddr_t end;
} ram_regions[PMM_MAX_RAM_REGIONS];
static uint32_t ram_region_count = 0;

static paddr_t heap_reserved_phys_start = 0;  ///< 堆保留区物理起始地址
static paddr_t heap_reserved_phys_end = 0;    ///< 堆保留区物理结束地址

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
 * @brief 初始化物理内存管理器
 * @param regions 可用的物理内存区域（来自 Multiboot 内存映射或设备树）
 *
 * 位图和引用计数表紧跟在内核映像后面。先把所有页帧标记为已用，再把落在可用区域里、
 * 又不属于内核和这两张表的页帧放出来。
 */
void mm::Pmm::init(const mm::MemRegion *regions, uint32_t count) {
    memset(&pmm_info, 0, sizeof(mm::PmmInfo));
    pmm_lock.init();

    paddr_t max_addr = 0;
    ram_region_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        paddr_t start = PADDR_ALIGN_UP(regions[i].start);
        paddr_t end = PADDR_ALIGN_DOWN(regions[i].end);
        if (end <= start || ram_region_count == PMM_MAX_RAM_REGIONS) {
            continue;
        }
        ram_regions[ram_region_count].start = start;
        ram_regions[ram_region_count].end = end;
        ram_region_count++;
        if (end > max_addr) {
            max_addr = end;
        }
    }
    if (max_addr == 0) {
        PANIC("PMM: no usable memory");
    }
    total_frames = max_addr / PAGE_SIZE;
    pmm_info.total_frames = total_frames;

    // 位图：每个页帧一位，先全部标记为已用
    pfn_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 31) / 32 * 4);
    frame_bitmap = (uint32_t *)PAGE_ALIGN_UP((uintptr_t)_kernel_end);
    bitmap_size = bitmap_bytes / 4;
    memset(frame_bitmap, 0xFF, bitmap_bytes);
    paddr_t bitmap_end_phys = PAGE_ALIGN_UP(VIRT_TO_PHYS((uintptr_t)frame_bitmap) + bitmap_bytes);

    // 引用计数表：每个页帧 16 位，紧跟位图
    pfn_t refcount_bytes = PAGE_ALIGN_UP(total_frames * sizeof(uint16_t));
    frame_refcount = (uint16_t *)PHYS_TO_VIRT(bitmap_end_phys);
    memset(frame_refcount, 0, refcount_bytes);
    paddr_t data_end_phys = PAGE_ALIGN_UP(bitmap_end_phys + refcount_bytes);
    pmm_data_end_virt = PHYS_TO_VIRT(data_end_phys);

    // 可用区域里、内核和上面两张表之后的页帧是空闲的
    for (uint32_t i = 0; i < ram_region_count; i++) {
        paddr_t start = ram_regions[i].start < data_end_phys ? data_end_phys : ram_regions[i].start;
        for (pfn_t f = PADDR_TO_PFN(start); f < PADDR_TO_PFN(ram_regions[i].end); f++) {
            clear_frame(f);
            pmm_info.free_frames++;
        }
    }
    pmm_info.used_frames = total_frames - pmm_info.free_frames;

    // 已用的页帧（内核、两张表、区域之间的空洞）引用计数为 1，永远不会被释放
    for (pfn_t i = 0; i < total_frames; i++) {
        frame_refcount[i] = test_frame(i) ? 1 : 0;
    }

    mm::Pmm::print_info();
}

/**
 * @brief 分配一个物理页帧
 * @return 成功返回页帧的物理地址，失败返回 PADDR_INVALID
 * 
 * 分配后会清零页帧内容
 */
paddr_t mm::Pmm::alloc_frame() {
    sync::SpinlockIrqGuard guard(pmm_lock);

    pfn_t idx = find_free_frame();
    if (idx == PFN_INVALID) {
        return PADDR_INVALID;
    }
    
    // 安全检查：确保找到的帧确实是空闲的
    if (test_frame(idx)) {
        LOG_ERROR_MSG("PMM: CRITICAL: find_free_frame returned used frame %llu!\n", 
                     (unsigned long long)idx);
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
        return PADDR_INVALID;
    }
#endif
    
    // 诊断日志：记录页目录区域的分配
    if (addr >= 0x00190000 && addr < 0x001b0000) {
        LOG_WARN_MSG("PMM: Allocated frame 0x%llx in page directory danger zone\n", 
                    (unsigned long long)addr);
    }
    
    // 清零页帧内容
    memset((void*)PHYS_TO_VIRT(addr), 0, PAGE_SIZE);
    
    return addr;
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
 * @brief 释放一个物理页帧
 * @param frame 页帧的物理地址
 * 
 * COW 支持：如果帧的引用计数 > 1，只递减计数，不实际释放
 */
void mm::Pmm::free_frame(paddr_t frame) {
    // 检查无效地址
    if (frame == 0 || frame == PADDR_INVALID) {
        return;
    }
    // 检查页对齐
    if (!IS_PADDR_ALIGNED(frame)) {
        return;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);

    sync::SpinlockIrqGuard guard(pmm_lock);

    // 检查有效性和状态
    if (idx >= total_frames) {
        return;
    }
    
    if (!test_frame(idx)) {
        // 仅在可能是有效内存区域时发出警告
        if (frame > 0x100000) {
            LOG_WARN_MSG("PMM: Double free or freeing unused frame 0x%llx (idx %llu)\n", 
                        (unsigned long long)frame, (unsigned long long)idx);
        }
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
}

/**
 * @brief 获取物理内存信息
 * @return 物理内存信息结构
 */
mm::PmmInfo mm::Pmm::get_info() {
    return pmm_info;
}

/**
 * @brief 获取 PMM 数据结构结束地址（虚拟地址）
 * @return PMM 数据结构结束的虚拟地址（页对齐）
 */
uintptr_t mm::Pmm::get_bitmap_end() {
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
void mm::Pmm::set_heap_reserved_range(uintptr_t heap_virt_start, uintptr_t heap_virt_end) {
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
 *============================================================================*/

/**
 * @brief 打印物理内存使用信息
 */
void mm::Pmm::print_info() {
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
uint32_t mm::Pmm::frame_ref_inc(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID || !IS_PADDR_ALIGNED(frame)) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_inc invalid frame 0x%llx\n", 
                     (unsigned long long)frame);
        return 0;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx >= total_frames) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_inc frame 0x%llx out of range (idx=%llu, total=%llu)\n", 
                     (unsigned long long)frame, (unsigned long long)idx, 
                     (unsigned long long)total_frames);
        return 0;
    }
    
    if (!frame_refcount) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_inc called but frame_refcount not initialized!\n");
        return 0;
    }
    
    sync::SpinlockIrqGuard guard(pmm_lock);
    
    if (frame_refcount[idx] == 0xFFFF) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_inc frame 0x%llx refcount overflow!\n", 
                     (unsigned long long)frame);
        return 0xFFFF;
    }
    
    frame_refcount[idx]++;
    uint32_t new_count = frame_refcount[idx];
    
    return new_count;
}

paddr_t mm::Pmm::alloc_contiguous(size_t count) {
    if (count == 0) {
        return PADDR_INVALID;
    }

    sync::SpinlockIrqGuard guard(pmm_lock);

    // 找一段连续的空闲帧
    pfn_t run_start = 0;
    size_t run = 0;
    for (pfn_t i = 1; i < total_frames && run < count; i++) {
        if (test_frame(i)) {
            run = 0;
            continue;
        }
        if (run == 0) {
            run_start = i;
        }
        run++;
    }
    if (run < count) {
        return PADDR_INVALID;
    }

    for (size_t i = 0; i < count; i++) {
        set_frame(run_start + i);
        frame_refcount[run_start + i] = 1;
        pmm_info.free_frames--;
        pmm_info.used_frames++;
    }

    paddr_t addr = PFN_TO_PADDR(run_start);
    memset((void *)PHYS_TO_VIRT((uintptr_t)addr), 0, count * PAGE_SIZE);
    return addr;
}

void mm::Pmm::frame_ref_share(paddr_t frame) {
    if (PADDR_TO_PFN(frame) < total_frames) {
        mm::Pmm::frame_ref_inc(frame);
    }
}

/* 被钉住的设备帧（在物理内存范围之内的那些），避免同一帧被重复钉住 */
#define PMM_MAX_PINNED_FRAMES 64
static paddr_t pinned_frames[PMM_MAX_PINNED_FRAMES];
static uint32_t pinned_frame_count = 0;

bool mm::Pmm::overlaps_ram(paddr_t start, paddr_t end) {
    for (uint32_t i = 0; i < ram_region_count; i++) {
        if (start < ram_regions[i].end && ram_regions[i].start < end) {
            return true;
        }
    }
    return false;
}

bool mm::Pmm::pin_device_frame(paddr_t frame) {
    if (frame == PADDR_INVALID || !IS_PADDR_ALIGNED(frame)) {
        return false;
    }

    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx >= total_frames) {
        return true;    // 不归 PMM 管：没有什么可回收的
    }

    sync::SpinlockIrqGuard guard(pmm_lock);

    for (uint32_t i = 0; i < pinned_frame_count; i++) {
        if (pinned_frames[i] == frame) {
            return true;
        }
    }

    // 空闲帧是可分配的普通内存，不能当设备内存交出去
    if (!test_frame(idx) || pinned_frame_count >= PMM_MAX_PINNED_FRAMES) {
        return false;
    }

    if (frame_refcount[idx] < 0xFFFF) {
        frame_refcount[idx]++;      // 这一份引用永远不归还
    }
    pinned_frames[pinned_frame_count++] = frame;
    return true;
}

/**
 * @brief 减少物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t mm::Pmm::frame_ref_dec(paddr_t frame) {
    if (frame == 0 || frame == PADDR_INVALID || !IS_PADDR_ALIGNED(frame)) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_dec invalid frame 0x%llx\n", 
                     (unsigned long long)frame);
        return 0;
    }
    
    pfn_t idx = PADDR_TO_PFN(frame);
    if (idx >= total_frames) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_dec frame 0x%llx out of range\n", 
                     (unsigned long long)frame);
        return 0;
    }
    
    if (!frame_refcount) {
        LOG_ERROR_MSG("PMM: mm::Pmm::frame_ref_dec called but frame_refcount not initialized!\n");
        return 0;
    }
    
    sync::SpinlockIrqGuard guard(pmm_lock);
    
    if (frame_refcount[idx] == 0) {
        LOG_WARN_MSG("PMM: mm::Pmm::frame_ref_dec frame 0x%llx already zero!\n", 
                    (unsigned long long)frame);
        return 0;
    }
    
    frame_refcount[idx]--;
    uint32_t new_count = frame_refcount[idx];
    
    return new_count;
}

/**
 * @brief 获取物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 引用计数值
 */
uint32_t mm::Pmm::frame_get_refcount(paddr_t frame) {
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
    
    sync::SpinlockIrqGuard guard(pmm_lock);
    uint32_t count = frame_refcount[idx];
    
    return count;
}


