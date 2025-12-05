/**
 * @file pmm.c
 * @brief 物理内存管理器实现
 * 
 * 使用位图跟踪物理页帧的分配状态
 */

#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/sync/spinlock.h>

#define MAX_PROTECTED_FRAMES 65536

typedef struct {
    uint32_t frame;
    uint32_t refcount;
} protected_frame_t;

static uint32_t *frame_bitmap = NULL;     ///< 页帧位图
static uint32_t bitmap_size = 0;          ///< 位图大小（32位字数量）
static uint32_t total_frames = 0;         ///< 总页帧数
static pmm_info_t pmm_info = {0};         ///< 物理内存信息
static uint32_t last_free_index = 0;      ///< 上次分配的空闲页帧索引（优化搜索）
static spinlock_t pmm_lock;               ///< PMM 自旋锁
static protected_frame_t protected_frames[MAX_PROTECTED_FRAMES];
static uint32_t protected_frame_count = 0;
static uint16_t *frame_refcount = NULL;   ///< 页帧引用计数数组（每帧2字节，最大65535引用）
static uint32_t pmm_data_end_virt = 0;    ///< PMM 数据结构结束的虚拟地址（位图+引用计数表）
extern uint32_t _kernel_end;           ///< 内核结束地址

// 堆保留区域：物理地址在此范围内的帧不会被分配，避免与堆虚拟地址重叠
// 当堆扩展时，它会重新映射 PHYS_TO_VIRT(phys) 地址，这会破坏已分配帧的恒等映射
static uint32_t heap_reserved_phys_start = 0;  ///< 堆保留区物理起始地址
static uint32_t heap_reserved_phys_end = 0;    ///< 堆保留区物理结束地址

static inline protected_frame_t* find_protected_frame_unsafe(uint32_t frame) {
    for (uint32_t i = 0; i < protected_frame_count; i++) {
        if (protected_frames[i].frame == frame) {
            return &protected_frames[i];
        }
    }
    return NULL;
}

/**
 * @brief 标记页帧为已使用
 * @param idx 页帧索引
 */
static inline void set_frame(uint32_t idx) {
    uint32_t bitmap_idx = idx / 32;
    uint32_t bit_idx = idx % 32;
    
    // 【调试】边界检查
    if (bitmap_idx >= bitmap_size) {
        LOG_ERROR_MSG("PMM: set_frame(%u): bitmap_idx %u >= bitmap_size %u!\n",
                     idx, bitmap_idx, bitmap_size);
        return;
    }
    
    frame_bitmap[bitmap_idx] |= (1U << bit_idx);  // 使用 1U 避免有符号整数溢出
}

/**
 * @brief 标记页帧为空闲
 * @param idx 页帧索引
 */
static inline void clear_frame(uint32_t idx) {
    frame_bitmap[idx/32] &= ~(1U << (idx%32));  // 使用 1U 避免有符号整数溢出
}

/**
 * @brief 检查页帧是否已使用
 * @param idx 页帧索引
 * @return 已使用返回 true，空闲返回 false
 */
static inline bool test_frame(uint32_t idx) {
    uint32_t bitmap_idx = idx / 32;
    uint32_t bit_idx = idx % 32;
    
    // 【调试】边界检查
    if (bitmap_idx >= bitmap_size) {
        LOG_ERROR_MSG("PMM: test_frame(%u): bitmap_idx %u >= bitmap_size %u!\n",
                     idx, bitmap_idx, bitmap_size);
        return false;  // 越界索引,认为是空闲
    }
    
    return (frame_bitmap[bitmap_idx] & (1U << bit_idx)) != 0;  // 使用 1U 避免有符号整数溢出
}

/**
 * @brief 查找空闲页帧
 * @return 成功返回页帧索引，失败返回 -1
 */
static int32_t find_free_frame(void) {
    // 从上次分配的位置开始搜索，优化性能
    for (uint32_t i = last_free_index; i < bitmap_size; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t bit = 0; bit < 32; bit++) {
                if (!(frame_bitmap[i] & (1 << bit))) {
                    uint32_t idx = i * 32 + bit;
                    if (idx < total_frames) {
                        last_free_index = i; // 更新搜索游标
                        return idx;
                    }
                }
            }
        }
    }
    
    // 如果后面没有找到，从头开始搜索
    if (last_free_index > 0) {
        for (uint32_t i = 0; i < last_free_index; i++) {
            if (frame_bitmap[i] != 0xFFFFFFFF) {
                for (uint32_t bit = 0; bit < 32; bit++) {
                    if (!(frame_bitmap[i] & (1 << bit))) {
                        uint32_t idx = i * 32 + bit;
                        if (idx < total_frames) {
                            last_free_index = i;
                            return idx;
                        }
                    }
                }
            }
        }
    }
    
    return -1;
}

/**
 * @brief 将物理帧加入保护列表
 */
void pmm_protect_frame(uint32_t frame) {
    if (!frame) {
        return;
    }
    if (frame & (PAGE_SIZE - 1)) {
        LOG_WARN_MSG("PMM: pmm_protect_frame received unaligned frame 0x%x, aligning down\n", frame);
        frame &= ~(PAGE_SIZE - 1);
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    // 【关键修复】确保受保护的帧在位图中被标记为已使用
    // 这防止 find_free_frame 返回一个受保护的帧
    uint32_t idx = frame / PAGE_SIZE;
    if (idx < total_frames && !test_frame(idx)) {
        LOG_WARN_MSG("PMM: Protecting frame 0x%x that was FREE in bitmap! Marking as used.\n", frame);
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
        LOG_ERROR_MSG("PMM: Protected frame table full! Cannot protect 0x%x\n", frame);
    }
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
}

/**
 * @brief 将物理帧从保护列表移除
 */
void pmm_unprotect_frame(uint32_t frame) {
    if (!frame) {
        return;
    }
    if (frame & (PAGE_SIZE - 1)) {
        LOG_WARN_MSG("PMM: pmm_unprotect_frame received unaligned frame 0x%x, aligning down\n", frame);
        frame &= ~(PAGE_SIZE - 1);
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    protected_frame_t *entry = find_protected_frame_unsafe(frame);
    if (!entry) {
        LOG_WARN_MSG("PMM: Attempted to unprotect unknown frame 0x%x\n", frame);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    if (entry->refcount == 0) {
        LOG_ERROR_MSG("PMM: Frame 0x%x has zero refcount while protected!\n", frame);
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
bool pmm_is_frame_protected(uint32_t frame) {
    if (!frame) {
        return false;
    }
    if (frame & (PAGE_SIZE - 1)) {
        frame &= ~(PAGE_SIZE - 1);
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
    uint32_t kernel_start = 0x100000;  // 1MB，内核加载位置
    uint32_t kernel_end = PAGE_ALIGN_UP(VIRT_TO_PHYS((uint32_t)&_kernel_end));
    
    // 计算总内存大小
    multiboot_memory_map_t *mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(mbi->mmap_addr);
    multiboot_memory_map_t *mmap_end = (multiboot_memory_map_t*)
        PHYS_TO_VIRT(mbi->mmap_addr + mbi->mmap_length);
    
    uint32_t max_addr = 0;
    while (mmap < mmap_end) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint32_t end = (uint32_t)(mmap->addr + mmap->len);
            
            // 高半核限制：内核虚拟空间只有 2GB (0x80000000-0xFFFFFFFF)
            // 因此只能直接映射 2GB 物理内存
            // 超过 2GB 的物理内存将被忽略（除非实现 fixmap）
            if (end > 0x80000000) {  // 2GB
                if (max_addr < 0x80000000) {
                    LOG_WARN_MSG("Physical memory exceeds 2GB, truncating to 2GB\n");
                    LOG_WARN_MSG("  Total physical memory: %u MB\n", end / (1024*1024));
                    LOG_WARN_MSG("  Usable physical memory: 2048 MB\n");
                    LOG_WARN_MSG("  Note: Implement fixmap to access > 2GB physical memory\n");
                }
                end = 0x80000000;
            }
            
            if (end > max_addr) max_addr = end;
        }
        mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + 4);
    }
    
    total_frames = max_addr / PAGE_SIZE;
    pmm_info.total_frames = total_frames;
    
    // 初始化 PMM 锁
    spinlock_init(&pmm_lock);
    
    // 初始化位图（默认所有页帧已使用）
    // 每个 uint32_t 有 32 位，所以需要的字节数是 (total_frames + 31) / 32 * 4
    uint32_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 31) / 32 * 4);
    frame_bitmap = (uint32_t*)PAGE_ALIGN_UP((uint32_t)&_kernel_end);
    bitmap_size = bitmap_bytes / 4;
    memset(frame_bitmap, 0xFF, bitmap_bytes);
    
    // 计算位图占用的物理地址范围
    uint32_t bitmap_phys_start = VIRT_TO_PHYS((uint32_t)frame_bitmap);
    uint32_t bitmap_end = PAGE_ALIGN_UP(bitmap_phys_start + bitmap_bytes);
    
    // 初始化引用计数表（紧跟位图之后）
    // 每个页帧用 uint16_t (2字节) 存储引用计数
    uint32_t refcount_bytes = PAGE_ALIGN_UP(total_frames * sizeof(uint16_t));
    frame_refcount = (uint16_t*)PHYS_TO_VIRT(bitmap_end);
    memset(frame_refcount, 0, refcount_bytes);
    uint32_t refcount_end = PAGE_ALIGN_UP(bitmap_end + refcount_bytes);
    
    // 保存 PMM 数据结构结束地址（虚拟地址），供堆初始化使用
    pmm_data_end_virt = PHYS_TO_VIRT(refcount_end);
    
    LOG_DEBUG_MSG("PMM: Frame refcount table at virt=%p, phys=0x%x, size=%u bytes\n",
                 frame_refcount, bitmap_end, refcount_bytes);
    LOG_DEBUG_MSG("PMM: Refcount table ends at phys=0x%x (virt=0x%x)\n", refcount_end, pmm_data_end_virt);
    
    // 【关键修复】标记引用计数表占用的帧为已使用
    uint32_t refcount_start_frame = bitmap_end / PAGE_SIZE;
    uint32_t refcount_end_frame = refcount_end / PAGE_SIZE;
    LOG_DEBUG_MSG("PMM: Marking refcount table frames %u-%u as used\n", 
                 refcount_start_frame, refcount_end_frame - 1);
    for (uint32_t f = refcount_start_frame; f < refcount_end_frame; f++) {
        if (f < total_frames) {
            set_frame(f);  // 标记为已使用
        }
    }
    
    // 标记空闲内存区域
    mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(mbi->mmap_addr);
    
    while (mmap < mmap_end) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint32_t start = PAGE_ALIGN_UP((uint32_t)mmap->addr);
            uint32_t end = PAGE_ALIGN_DOWN((uint32_t)(mmap->addr + mmap->len));
            
            // 强制限制：不处理超过 2GB 的物理内存
            if (end > 0x80000000) {
                end = 0x80000000;
            }
            
            // 跳过内核、位图和引用计数表占用的区域
            if (start < kernel_end) start = kernel_end;
            if (start < refcount_end) start = refcount_end;
            
            if (end > start) {
                for (uint32_t f = start/PAGE_SIZE; f < end/PAGE_SIZE; f++) {
                    clear_frame(f);
                    pmm_info.free_frames++;
                }
            }
        }
        mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + 4);
    }
    
    // 处理 Multiboot 模块（如 initrd），标记为已使用
    if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count > 0) {
        multiboot_module_t *mod = (multiboot_module_t*)PHYS_TO_VIRT(mbi->mods_addr);
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            uint32_t start_frame = PAGE_ALIGN_DOWN(mod[i].mod_start) / PAGE_SIZE;
            uint32_t end_frame = PAGE_ALIGN_UP(mod[i].mod_end) / PAGE_SIZE;
            
            for (uint32_t f = start_frame; f < end_frame; f++) {
                if (f < total_frames && !test_frame(f)) {
                    set_frame(f);
                    pmm_info.free_frames--;
                }
            }
        }
    }

    pmm_info.used_frames = total_frames - pmm_info.free_frames;
    
    // 计算内核占用的页帧数（从 1MB 到 kernel_end）
    pmm_info.kernel_frames = (kernel_end - kernel_start) / PAGE_SIZE;
    
    // 计算位图占用的页帧数
    pmm_info.bitmap_frames = (bitmap_end - bitmap_phys_start) / PAGE_SIZE;
    
    // 计算引用计数表占用的页帧数
    uint32_t refcount_frames = (refcount_end - bitmap_end) / PAGE_SIZE;
    
    // 保留页帧数 = 内核 + 位图 + 引用计数表
    pmm_info.reserved_frames = pmm_info.kernel_frames + pmm_info.bitmap_frames + refcount_frames;
    
    LOG_DEBUG_MSG("PMM: Reserved frames: kernel=%u, bitmap=%u, refcount=%u, total=%u\n",
                 pmm_info.kernel_frames, pmm_info.bitmap_frames, refcount_frames, 
                 pmm_info.reserved_frames);
    
    // 初始化引用计数：所有已使用的帧设置为 1
    for (uint32_t i = 0; i < total_frames; i++) {
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
 * @return 成功返回页帧的物理地址，失败返回 0
 * 
 * 分配后会清零页帧内容
 */
uint32_t pmm_alloc_frame(void) {
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);

    int32_t idx = find_free_frame();
    if (idx < 0) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    // 【安全检查】确保找到的帧确实是空闲的
    if (test_frame(idx)) {
        LOG_ERROR_MSG("PMM: CRITICAL: find_free_frame returned used frame %d!\n", idx);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    set_frame(idx);
    pmm_info.free_frames--;
    pmm_info.used_frames++;
    
    // 设置引用计数为 1（新分配）
    frame_refcount[idx] = 1;
    
    // 【安全检查】验证 set_frame 是否生效
    if (!test_frame(idx)) {
        uint32_t bitmap_idx = idx / 32;
        LOG_ERROR_MSG("PMM: CRITICAL: set_frame(%d) failed! Frame still shows as free!\n", idx);
        LOG_ERROR_MSG("  idx=%u, bitmap_idx=%u, bitmap_size=%u, total_frames=%u\n",
                     idx, bitmap_idx, bitmap_size, total_frames);
        LOG_ERROR_MSG("  frame_bitmap=%p, frame_bitmap[%u]=0x%x\n",
                     frame_bitmap, bitmap_idx, bitmap_idx < bitmap_size ? frame_bitmap[bitmap_idx] : 0);
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    uint32_t addr = idx * PAGE_SIZE;
    
    // 安全检查：确保物理地址在可映射范围内
    if (addr >= 0x80000000) {  // >= 2GB
        LOG_ERROR_MSG("PMM: Allocated frame beyond 2GB (0x%x), this should not happen!\n", addr);
        // 回退分配
        clear_frame(idx);
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    // 【诊断日志】记录页目录区域的分配
    if (addr >= 0x00190000 && addr < 0x001b0000) {
        LOG_WARN_MSG("PMM: Allocated frame 0x%x in page directory danger zone (0x190000-0x1b0000)\n", addr);
        LOG_WARN_MSG("  PMM state: used=%u, free=%u\n", pmm_info.used_frames, pmm_info.free_frames);
    }
    
    // 【关键安全检查】确保我们没有分配一个受保护的帧
    // 这是最后一道防线，确保不会返回活动页目录或页表的帧
    if (find_protected_frame_unsafe(addr)) {
        LOG_ERROR_MSG("PMM: CRITICAL! Allocated frame 0x%x is protected! This should never happen!\n", addr);
        // 回滚分配
        clear_frame(idx);
        frame_refcount[idx] = 0;
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    // 清零页帧内容
    // 注意：vmm_init 扩展映射期间，高端帧（>16MB）可能尚未映射
    // 但 vmm_init 有安全检查确保页表帧在 16MB 以下
    // vmm_init 完成后，所有物理内存都会被映射，后续清零无问题
    memset((void*)PHYS_TO_VIRT(addr), 0, PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return addr;
}

/**
 * @brief 释放一个物理页帧
 * @param frame 页帧的物理地址
 * 
 * 地址必须是页对齐的
 * 
 * COW 支持：如果帧的引用计数 > 1，只递减计数，不实际释放
 * 这允许多个进程通过 COW 共享同一物理页
 */
void pmm_free_frame(uint32_t frame) {
    // 检查页对齐
    if (frame & (PAGE_SIZE-1)) return;
    uint32_t idx = frame / PAGE_SIZE;

    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);

    // 检查有效性和状态
    if (idx >= total_frames) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    if (!test_frame(idx)) {
        // 仅在可能是有效内存区域时发出警告
        // 忽略低端内存（可能被 BIOS/VGA 使用）
        if (frame > 0x100000) {
            LOG_WARN_MSG("PMM: Double free or freeing unused frame 0x%x (idx %u)\n", frame, idx);
        }
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    if (find_protected_frame_unsafe(frame)) {
        LOG_ERROR_MSG("PMM: Attempt to free protected frame 0x%x blocked\n", frame);
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    // 【COW 关键】检查并递减引用计数
    // 只有当引用计数降为 0 时才真正释放帧
    if (frame_refcount[idx] == 0) {
        // 引用计数已经是 0，这是一个错误状态
        // （帧被标记为使用，但引用计数为 0）
        LOG_WARN_MSG("PMM: Frame 0x%x marked as used but refcount is 0, releasing anyway\n", frame);
        // 继续执行释放逻辑
    } else {
        frame_refcount[idx]--;
        if (frame_refcount[idx] > 0) {
            // 引用计数还不为 0，说明还有其他进程通过 COW 共享此帧
            // 不释放，只减少计数
            // LOG_DEBUG_MSG("PMM: Frame 0x%x refcount decreased to %u, not freeing (COW shared)\n", frame, frame_refcount[idx]);
            spinlock_unlock_irqrestore(&pmm_lock, irq_state);
            return;
        }
        // 引用计数降为 0，继续执行释放逻辑
    }
    
    // 引用计数为 0，真正释放
    clear_frame(idx);
    pmm_info.free_frames++;
    pmm_info.used_frames--;
    
    // 【诊断日志】记录页目录区域的释放
    if (frame >= 0x00190000 && frame < 0x001b0000) {
        LOG_WARN_MSG("PMM: Freed frame 0x%x in page directory danger zone\n", frame);
    }
    
    // 更新搜索游标，如果有更小的空闲帧
    // last_free_index 是位图数组索引（uint32_t 索引），不是页帧索引
    uint32_t bitmap_idx = idx / 32;
    if (bitmap_idx < last_free_index) {
        last_free_index = bitmap_idx;
    }
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
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
 * 
 * 返回位图和引用计数表之后的地址。
 * 用于确定堆的起始地址，避免堆与 PMM 数据结构重叠。
 * 
 * 注意：函数名保留为 pmm_get_bitmap_end 以保持 API 兼容性，
 * 但实际返回的是包括引用计数表在内的所有 PMM 数据结构的结束地址。
 */
uint32_t pmm_get_bitmap_end(void) {
    // 返回保存的 PMM 数据结构结束地址（包括位图和引用计数表）
    if (pmm_data_end_virt != 0) {
        return pmm_data_end_virt;
    }
    
    // 回退：如果还没有初始化，使用旧的计算方式（仅位图）
    // 这种情况理论上不应该发生，因为 pmm_init 会在任何调用之前执行
    uint32_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 31) / 32 * 4);
    return PAGE_ALIGN_UP((uint32_t)frame_bitmap + bitmap_bytes);
}

/**
 * @brief 设置堆保留区域的物理地址范围
 * @param heap_virt_start 堆虚拟起始地址
 * @param heap_virt_end 堆虚拟结束地址（最大地址）
 * 
 * 将堆虚拟地址范围转换为物理地址范围，并标记这些物理帧不可分配。
 * 这防止了堆扩展时重新映射已分配帧的恒等映射导致的内存损坏。
 * 
 * 原理：
 * - 堆使用虚拟地址空间 [heap_virt_start, heap_virt_end)
 * - 当堆扩展时，它会将新物理帧映射到这些虚拟地址
 * - 但 PMM 分配的帧通过 PHYS_TO_VIRT 恒等映射访问
 * - 如果物理帧 P 的 PHYS_TO_VIRT(P) 落在堆的虚拟范围内，
 *   堆扩展会覆盖这个映射，导致帧 P 无法正确访问
 * - 因此我们需要避免分配这些"危险"的物理帧
 */
void pmm_set_heap_reserved_range(uint32_t heap_virt_start, uint32_t heap_virt_end) {
    // 将堆虚拟地址转换为对应的物理地址
    // 只有在高半核范围内的地址才需要转换
    if (heap_virt_start >= 0x80000000 && heap_virt_end > heap_virt_start) {
        heap_reserved_phys_start = VIRT_TO_PHYS(heap_virt_start);
        heap_reserved_phys_end = VIRT_TO_PHYS(heap_virt_end);
        
        // 标记这些帧为已使用，确保它们不会被分配
        uint32_t start_frame = heap_reserved_phys_start / PAGE_SIZE;
        uint32_t end_frame = (heap_reserved_phys_end + PAGE_SIZE - 1) / PAGE_SIZE;
        
        uint32_t reserved_count = 0;
        for (uint32_t f = start_frame; f < end_frame && f < total_frames; f++) {
            if (!test_frame(f)) {
                set_frame(f);
                pmm_info.free_frames--;
                pmm_info.used_frames++;
                frame_refcount[f] = 1;  // 设置引用计数防止释放
                reserved_count++;
            }
        }
        
        LOG_INFO_MSG("PMM: Reserved heap range: phys 0x%x - 0x%x (%u frames, %u newly reserved)\n",
                    heap_reserved_phys_start, heap_reserved_phys_end,
                    end_frame - start_frame, reserved_count);
    }
}

/**
 * @brief 打印物理内存使用信息
 */
void pmm_print_info(void) {
    kprintf("\n=============================== Physical Memory ================================\n");
    kprintf("Total: %u MB\n", (pmm_info.total_frames * PAGE_SIZE) / (1024*1024));
    kprintf("Free:  %u MB\n", (pmm_info.free_frames * PAGE_SIZE) / (1024*1024));
    kprintf("Used:  %u MB\n", (pmm_info.used_frames * PAGE_SIZE) / (1024*1024));
    kprintf("================================================================================\n\n");
}

/**
 * @brief 增加物理页帧的引用计数
 * @param frame 页帧的物理地址
 * @return 新的引用计数值
 */
uint32_t pmm_frame_ref_inc(uint32_t frame) {
    if (!frame || (frame & (PAGE_SIZE - 1))) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc invalid frame 0x%x\n", frame);
        return 0;
    }
    
    uint32_t idx = frame / PAGE_SIZE;
    if (idx >= total_frames) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc frame 0x%x out of range (idx=%u, total=%u)\n", 
                     frame, idx, total_frames);
        return 0;
    }
    
    // 【安全检查】确保 frame_refcount 已初始化
    if (!frame_refcount) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc called but frame_refcount not initialized!\n");
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    if (frame_refcount[idx] == 0xFFFF) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_inc frame 0x%x refcount overflow!\n", frame);
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
uint32_t pmm_frame_ref_dec(uint32_t frame) {
    if (!frame || (frame & (PAGE_SIZE - 1))) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_dec invalid frame 0x%x\n", frame);
        return 0;
    }
    
    uint32_t idx = frame / PAGE_SIZE;
    if (idx >= total_frames) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_dec frame 0x%x out of range\n", frame);
        return 0;
    }
    
    // 【安全检查】确保 frame_refcount 已初始化
    if (!frame_refcount) {
        LOG_ERROR_MSG("PMM: pmm_frame_ref_dec called but frame_refcount not initialized!\n");
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    
    if (frame_refcount[idx] == 0) {
        LOG_WARN_MSG("PMM: pmm_frame_ref_dec frame 0x%x already zero!\n", frame);
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
uint32_t pmm_frame_get_refcount(uint32_t frame) {
    if (!frame || (frame & (PAGE_SIZE - 1))) {
        return 0;
    }
    
    uint32_t idx = frame / PAGE_SIZE;
    if (idx >= total_frames) {
        return 0;
    }
    
    // 【安全检查】确保 frame_refcount 已初始化
    if (!frame_refcount) {
        return 1;  // 未初始化时假设引用计数为 1（保守策略）
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);
    uint32_t count = frame_refcount[idx];
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    
    return count;
}
