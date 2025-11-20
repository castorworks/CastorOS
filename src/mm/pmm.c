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

static uint32_t *frame_bitmap = NULL;  ///< 页帧位图
static uint32_t bitmap_size = 0;       ///< 位图大小（32位字数量）
static uint32_t total_frames = 0;      ///< 总页帧数
static pmm_info_t pmm_info = {0};      ///< 物理内存信息
static uint32_t last_free_index = 0;   ///< 上次分配的空闲页帧索引（优化搜索）
static spinlock_t pmm_lock;            ///< PMM 自旋锁
extern uint32_t _kernel_end;           ///< 内核结束地址

/**
 * @brief 标记页帧为已使用
 * @param idx 页帧索引
 */
static inline void set_frame(uint32_t idx) {
    frame_bitmap[idx/32] |= (1 << (idx%32));
}

/**
 * @brief 标记页帧为空闲
 * @param idx 页帧索引
 */
static inline void clear_frame(uint32_t idx) {
    frame_bitmap[idx/32] &= ~(1 << (idx%32));
}

/**
 * @brief 检查页帧是否已使用
 * @param idx 页帧索引
 * @return 已使用返回 true，空闲返回 false
 */
static inline bool test_frame(uint32_t idx) {
    return frame_bitmap[idx/32] & (1 << (idx%32));
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
 * @brief 初始化物理内存管理器
 * @param mbi Multiboot信息结构指针
 * 
 * 解析内存映射，初始化位图，标记已使用和空闲的页帧
 */
void pmm_init(multiboot_info_t *mbi) {
    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP))
        PANIC("No memory map");
    
    memset(&pmm_info, 0, sizeof(pmm_info_t));
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
    uint32_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 7) / 8);
    frame_bitmap = (uint32_t*)PAGE_ALIGN_UP((uint32_t)&_kernel_end);
    bitmap_size = bitmap_bytes / 4;
    memset(frame_bitmap, 0xFF, bitmap_bytes);
    
    // 计算位图占用的物理地址范围
    uint32_t bitmap_phys_start = VIRT_TO_PHYS((uint32_t)frame_bitmap);
    uint32_t bitmap_end = PAGE_ALIGN_UP(bitmap_phys_start + bitmap_bytes);
    
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
            
            // 跳过内核和位图占用的区域
            if (start < kernel_end) start = kernel_end;
            if (start < bitmap_end) start = bitmap_end;
            
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
    
    set_frame(idx);
    pmm_info.free_frames--;
    pmm_info.used_frames++;
    
    uint32_t addr = idx * PAGE_SIZE;
    
    // 安全检查：确保物理地址在可映射范围内
    if (addr >= 0x80000000) {  // >= 2GB
        LOG_ERROR_MSG("PMM: Allocated frame beyond 2GB (%x), this should not happen!\n", addr);
        // 回退分配
        clear_frame(idx);
        pmm_info.free_frames++;
        pmm_info.used_frames--;
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return 0;
    }
    
    // 清零页帧内容
    memset((void*)PHYS_TO_VIRT(addr), 0, PAGE_SIZE);
    
    spinlock_unlock_irqrestore(&pmm_lock, irq_state);
    return addr;
}

/**
 * @brief 释放一个物理页帧
 * @param frame 页帧的物理地址
 * 
 * 地址必须是页对齐的
 */
void pmm_free_frame(uint32_t frame) {
    // 检查页对齐
    if (frame & (PAGE_SIZE-1)) return;
    uint32_t idx = frame / PAGE_SIZE;

    bool irq_state;
    spinlock_lock_irqsave(&pmm_lock, &irq_state);

    // 检查有效性和状态
    if (idx >= total_frames || !test_frame(idx)) {
        spinlock_unlock_irqrestore(&pmm_lock, irq_state);
        return;
    }
    
    clear_frame(idx);
    pmm_info.free_frames++;
    pmm_info.used_frames--;
    
    // 更新搜索游标，如果有更小的空闲帧
    if (idx < last_free_index) {
        last_free_index = idx;
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
 * @brief 获取位图结束地址（虚拟地址）
 * @return 位图结束的虚拟地址（页对齐）
 * 
 * 用于确定堆的起始地址，避免堆与位图重叠
 */
uint32_t pmm_get_bitmap_end(void) {
    uint32_t bitmap_bytes = PAGE_ALIGN_UP((total_frames + 7) / 8);
    return PAGE_ALIGN_UP((uint32_t)frame_bitmap + bitmap_bytes);
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
