/**
 * @file vmm.c
 * @brief 虚拟内存管理器实现
 * 
 * 实现分页机制，管理虚拟地址到物理地址的映射
 * 核心逻辑保持架构无关，通过 HAL 接口调用架构特定操作
 * 
 * Requirements: 5.1, 5.2
 */

#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/sync/spinlock.h>
#include <kernel/task.h>
#include <hal/hal.h>

static page_directory_t *current_dir = NULL;  ///< 当前页目录虚拟地址
static uintptr_t current_dir_phys = 0;         ///< 当前页目录物理地址
#if defined(ARCH_X86_64)
extern uint64_t boot_page_directory[];         ///< 引导时的 PML4 (x86_64)
#else
extern uint32_t boot_page_directory[];         ///< 引导时的页目录 (i686)
#endif
static spinlock_t vmm_lock;                    ///< VMM 自旋锁，保护页表操作

#if defined(ARCH_X86_64)
#define KERNEL_PDE_START 256   // x86_64: PML4 entry 256 = 0xFFFF800000000000
#define KERNEL_PDE_END   512
#else
#define KERNEL_PDE_START 512   // i686: PDE 512 = 0x80000000
#define KERNEL_PDE_END   1024
#endif

// 活动页目录跟踪（防止页目录被意外覆盖）
#define MAX_PAGE_DIRECTORIES 64
static uintptr_t active_page_directories[MAX_PAGE_DIRECTORIES];
static uint32_t active_pd_count = 0;

#if defined(ARCH_X86_64)
/* x86_64: 4-level paging address decomposition */
static inline uint32_t pml4_idx(uintptr_t v) { return (v >> 39) & 0x1FF; }
static inline uint32_t pdpt_idx(uintptr_t v) { return (v >> 30) & 0x1FF; }
static inline uint32_t pd_idx(uintptr_t v)   { return (v >> 21) & 0x1FF; }
static inline uint32_t pt_idx(uintptr_t v)   { return (v >> 12) & 0x1FF; }
static inline uintptr_t get_frame64(uint64_t e) { return e & 0x000FFFFFFFFFF000ULL; }
static inline bool is_present64(uint64_t e) { return e & PAGE_PRESENT; }
/* Compatibility aliases for x86_64 */
#define pde_idx(v) pml4_idx(v)
#define pte_idx(v) pt_idx(v)
#define get_frame(e) get_frame64(e)
#define is_present(e) is_present64(e)
#else
/* i686: 2-level paging address decomposition */
/**
 * @brief 获取页目录索引
 * @param v 虚拟地址
 * @return 页目录索引（高10位）
 */
static inline uint32_t pde_idx(uintptr_t v) { return v >> 22; }

/**
 * @brief 获取页表索引
 * @param v 虚拟地址
 * @return 页表索引（中间10位）
 */
static inline uint32_t pte_idx(uintptr_t v) { return (v >> 12) & 0x3FF; }

/**
 * @brief 从页表项/页目录项中提取物理地址
 * @param e 页表项或页目录项
 * @return 物理地址（页对齐）
 */
static inline uintptr_t get_frame(pde_t e) { return e & 0xFFFFF000; }

/**
 * @brief 检查页表项/页目录项是否存在
 * @param e 页表项或页目录项
 * @return 存在返回 true，否则返回 false
 */
static inline bool is_present(pde_t e) { return e & PAGE_PRESENT; }
#endif

/**
 * @brief 检查物理帧是否是活动页目录
 * @param frame 物理帧地址
 * @return 是活动页目录返回 true，否则返回 false
 */
__attribute__((unused))
static bool is_active_page_directory(uintptr_t frame) {
    for (uint32_t i = 0; i < active_pd_count; i++) {
        if (active_page_directories[i] == frame) {
            return true;
        }
    }
    return false;
}

static inline void protect_phys_frame(paddr_t frame) {
    if (frame && frame != PADDR_INVALID) {
        pmm_protect_frame(frame);
    }
}

static inline void unprotect_phys_frame(paddr_t frame) {
    if (frame && frame != PADDR_INVALID) {
        pmm_unprotect_frame(frame);
    }
}

#if !defined(ARCH_X86_64)
/* i686-only helper functions - x86_64 uses boot page tables directly */
static void protect_directory_range(page_directory_t *dir, uint32_t start_idx, uint32_t end_idx) {
    for (uint32_t i = start_idx; i < end_idx; i++) {
        pde_t entry = dir->entries[i];
        if (is_present(entry)) {
            uintptr_t frame = get_frame(entry);
            if (frame == 0) {
                LOG_ERROR_MSG("VMM: protect_directory_range detected zero frame at PDE %u\n", i);
                continue;
            }
            protect_phys_frame(frame);
        }
    }
}

static void release_directory_range(page_directory_t *dir, uint32_t start_idx, uint32_t end_idx, bool clear_entries) {
    for (uint32_t i = start_idx; i < end_idx; i++) {
        pde_t entry = dir->entries[i];
        if (is_present(entry)) {
            uintptr_t frame = get_frame(entry);
            if (frame == 0) {
                LOG_ERROR_MSG("VMM: release_directory_range detected zero frame at PDE %u\n", i);
                if (clear_entries) {
                    dir->entries[i] = 0;
                }
                continue;
            }
            unprotect_phys_frame(frame);
            if (clear_entries) {
                dir->entries[i] = 0;
            }
        }
    }
}

/**
 * @brief 注册活动页目录
 * @param dir_phys 页目录的物理地址
 */
static void register_page_directory(uintptr_t dir_phys) {
    if (active_pd_count >= MAX_PAGE_DIRECTORIES) {
        LOG_ERROR_MSG("VMM: Too many active page directories! Cannot register 0x%lx\n", (unsigned long)dir_phys);
        return;
    }
    
    // 检查是否已注册
    if (is_active_page_directory(dir_phys)) {
        LOG_WARN_MSG("VMM: Page directory 0x%lx already registered\n", (unsigned long)dir_phys);
        return;
    }
    
    protect_phys_frame(dir_phys);
    active_page_directories[active_pd_count++] = dir_phys;
    LOG_INFO_MSG("VMM: Registered page directory 0x%lx (total: %u)\n", (unsigned long)dir_phys, active_pd_count);
}

/**
 * @brief 注销活动页目录
 * @param dir_phys 页目录的物理地址
 */
static void unregister_page_directory(uintptr_t dir_phys) {
    for (uint32_t i = 0; i < active_pd_count; i++) {
        if (active_page_directories[i] == dir_phys) {
            unprotect_phys_frame(dir_phys);
            // 用最后一个元素替换当前元素
            active_page_directories[i] = active_page_directories[--active_pd_count];
            LOG_INFO_MSG("VMM: Unregistered page directory 0x%lx (remaining: %u)\n", (unsigned long)dir_phys, active_pd_count);
            return;
        }
    }
    LOG_ERROR_MSG("VMM: ERROR: Tried to unregister unknown page directory 0x%lx\n", (unsigned long)dir_phys);
}
#endif /* !ARCH_X86_64 */

#if !defined(ARCH_X86_64)
/**
 * @brief 创建新的页表 (i686 only)
 * @return 成功返回页表虚拟地址，失败返回 NULL
 */
static page_table_t* create_page_table(void) {
    paddr_t frame = pmm_alloc_frame();
    if (frame == PADDR_INVALID) return NULL;
    return (page_table_t*)PHYS_TO_VIRT(frame);
}
#endif

/**
 * @brief 初始化虚拟内存管理器
 * 
 * 使用引导时创建的页目录，设置CR3寄存器
 * 扩展高半核映射以覆盖所有可用的物理内存
 */
void vmm_init(void) {
    // 初始化 VMM 自旋锁
    spinlock_init(&vmm_lock);
    
#if defined(ARCH_X86_64)
    // x86_64: 引导代码已经设置了 4 级页表，映射了前 1GB
    // 暂时不扩展映射，直接使用引导时的页表
    // boot_page_directory 在 x86_64 上是指向 PML4 的指针
    current_dir_phys = hal_mmu_get_current_page_table();
    current_dir = (page_directory_t*)PHYS_TO_VIRT(current_dir_phys);
    
    LOG_INFO_MSG("VMM: x86_64 mode - using boot page tables\n");
    LOG_INFO_MSG("VMM: PML4 at phys 0x%llx, virt 0x%llx\n", 
                 (unsigned long long)current_dir_phys, (unsigned long long)current_dir);
    LOG_INFO_MSG("VMM: Boot mapping covers first 1GB of physical memory\n");
    
    // x86_64 暂时不需要扩展映射，引导代码已经映射了足够的内存
    // TODO: 实现完整的 x86_64 VMM，支持动态页表管理
#else
    // i686: 原有的 32 位实现
    current_dir = (page_directory_t*)boot_page_directory;
    current_dir_phys = VIRT_TO_PHYS((uintptr_t)current_dir);
    
    // 检查并更新页表基址寄存器 (通过 HAL 接口)
    uintptr_t current_page_table = hal_mmu_get_current_page_table();
    if (current_page_table != current_dir_phys)
        hal_mmu_switch_space(current_dir_phys);
    
    // 扩展高半核映射以覆盖所有可用的物理内存
    // 引导时已经映射了前8MB（页目录项512-513）
    // 现在需要扩展到所有可用内存（最多2GB）
    pmm_info_t pmm_info = pmm_get_info();
    uint32_t max_phys = pmm_info.total_frames * PAGE_SIZE;
    
    // 限制在2GB以内（高半核虚拟地址空间限制）
    if (max_phys > 0x80000000) {
        max_phys = 0x80000000;
    }
    
    // 计算需要映射的页目录项数量（每个页目录项映射4MB）
    // 引导时已经映射了前 16MB（索引 512-515），从索引 516 开始
    uint32_t start_pde = 516;  // 对应虚拟地址 0x81000000
    // 修复：使用向上取整，确保覆盖所有物理内存。如果 max_phys 不是 4MB 对齐，
    // 向下取整会导致末尾的内存无法被映射。
    uint32_t end_pde = 512 + ((max_phys + 0x3FFFFF) >> 22); 
    
    LOG_INFO_MSG("VMM: Extending high-half kernel mapping\n");
    LOG_INFO_MSG("  Physical memory: %u MB\n", max_phys / (1024*1024));
    LOG_INFO_MSG("  Mapping PDEs: %u-%u (phys: 0x%x-0x%x)\n",
                 start_pde, end_pde - 1,
                 (start_pde - 512) << 22, ((end_pde - 512) << 22) - 1);
    
    // 为每个页目录项创建页表并映射
    // 注意：每映射完一个 PDE 后立即刷新 TLB，这样后续的 pmm_alloc_frame 
    // 可以使用新映射的内存区域（因为 pmm_alloc_frame 会清零新分配的帧）
    uint32_t mapped_pdes = 0;
    for (uint32_t pde = start_pde; pde < end_pde; pde++) {
        // 检查页目录项是否已存在
        if (is_present(current_dir->entries[pde])) {
            continue;  // 已经映射，跳过
        }
        
        // 分配页表
        // 注意：pmm_alloc_frame 会清零新分配的帧，需要确保帧在已映射范围内
        paddr_t table_phys = pmm_alloc_frame();
        if (table_phys == PADDR_INVALID) {
            LOG_WARN_MSG("VMM: Failed to allocate page table for PDE %u\n", pde);
            break;  // 分配失败，停止扩展
        }
        
        // 安全检查：确保页表帧在已映射范围内（引导时映射了前 16MB）
        // 如果帧超出范围，pmm_alloc_frame 内部的 memset 就会失败
        // 但由于 PMM 优先分配低地址帧，这种情况不应该发生
        if (table_phys >= 0x1000000) {  // >= 16MB
            LOG_ERROR_MSG("VMM: Page table frame 0x%llx exceeds boot mapping! This is a bug.\n", (unsigned long long)table_phys);
            pmm_free_frame(table_phys);
            break;
        }
        
        page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
        
        // 计算这个页表对应的物理地址范围
        // PDE索引pde对应虚拟地址 pde * 4MB
        // 对应的物理地址也是 pde * 4MB（高半核恒等映射）
        uint32_t phys_base = (pde - 512) << 22;  // 物理地址基址
        
        // 填充页表项：每个页表项映射一个4KB页
        // 映射整个 4MB 区域，包括保留内存（如 ACPI 表）
        // 这样可以确保所有物理地址都可以通过高半核访问
        for (uint32_t pte = 0; pte < 1024; pte++) {
            uint32_t phys_addr = phys_base + (pte << 12);
            // 设置页表项：Present | Read/Write | Supervisor
            table->entries[pte] = phys_addr | PAGE_PRESENT | PAGE_WRITE;
        }
        
        // 设置页目录项：指向页表
        current_dir->entries[pde] = (uint32_t)table_phys | PAGE_PRESENT | PAGE_WRITE;
        
        // 立即刷新 TLB，使新映射生效
        // 这样下一次 pmm_alloc_frame 就可以安全地访问更高地址的内存了
        vmm_flush_tlb(0);
        mapped_pdes++;
    }
    
    LOG_INFO_MSG("VMM: Extended mapping by %u PDEs (now covers 0-%u MB)\n", 
                 mapped_pdes, ((end_pde - 512) * 4));
    
    // 注册引导页目录为活动页目录（保护它不被覆盖）
    register_page_directory(current_dir_phys);
    protect_directory_range(current_dir, KERNEL_PDE_START, KERNEL_PDE_END);
    
    LOG_INFO_MSG("VMM: High-half kernel mapping extended\n");
    LOG_INFO_MSG("VMM: Boot page directory registered at phys 0x%x\n", current_dir_phys);
#endif
}


/**
 * @brief 处理内核空间缺页异常（同步内核页目录）
 * @param addr 缺页地址
 * @return 是否成功处理
 */
bool vmm_handle_kernel_page_fault(uintptr_t addr) {
#if defined(ARCH_X86_64)
    // x86_64: 引导时已映射所有内核空间，不需要同步
    (void)addr;
    return false;
#else
    // 必须是内核空间地址
    if (addr < KERNEL_VIRTUAL_BASE) return false;
    
    uint32_t pd_idx = (uint32_t)(addr >> 22);
    page_directory_t *k_dir = (page_directory_t *)boot_page_directory;
    
    // 检查主内核页目录中是否存在该映射
    // 注意：我们检查 PDE 是否存在 (Present 位)
    if (k_dir->entries[pd_idx] & PAGE_PRESENT) {
        // 将条目复制到当前页目录
        current_dir->entries[pd_idx] = k_dir->entries[pd_idx];
        
        // 刷新 TLB，确保 CPU 看到新的映射
        // 虽然 Intel 手册说修改 PDE 后需要刷新 TLB，但有些实现可能缓存了 PDE
        // 对于缺页处理，invlpg 通常足够，但这里我们修改了 PDE，安全起见可以刷新整个 TLB
        // 不过针对特定地址的 invlpg 应该也足以让 CPU 重新遍历页表结构
        vmm_flush_tlb(addr);
        
        return true;
    }
    
    return false;
#endif
}

/**
 * @brief 处理写保护异常（COW）
 * @param addr 缺页地址
 * @param error_code 错误码
 * @return 是否成功处理
 * 
 * x86 Page Fault Error Code:
 *   Bit 0 (P): 1 = 页面存在，0 = 页面不存在
 *   Bit 1 (W): 1 = 写操作，0 = 读操作
 *   Bit 2 (U): 1 = 用户模式，0 = 内核模式
 *   Bit 3 (RSVD): 1 = 保留位被设置
 *   Bit 4 (I/D): 1 = 指令获取导致
 * 
 * COW 异常特征：页面存在(P=1) + 写操作(W=1) + 页面有 PAGE_COW 标志
 */
bool vmm_handle_cow_page_fault(uintptr_t addr, uint32_t error_code) {
#if defined(ARCH_X86_64)
    // x86_64: 使用 HAL 接口实现通用 COW 处理
    // error_code bit 1: 写入导致的异常
    // error_code bit 0: 页面存在
    // COW 异常应该是：页面存在(bit0=1) + 写入(bit1=1) = 0x3 或 0x7
    if ((error_code & 0x3) != 0x3) {
        LOG_DEBUG_MSG("COW x64: Not a COW fault - addr=0x%llx, error=0x%x\n", 
                     (unsigned long long)addr, error_code);
        return false;  // 不是写保护异常
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 使用 HAL 接口查询页面映射
    paddr_t old_frame;
    uint32_t hal_flags;
    if (!hal_mmu_query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, &old_frame, &hal_flags)) {
        LOG_DEBUG_MSG("COW x64: Page not mapped - addr=0x%llx\n", (unsigned long long)addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    // 检查页面是否标记为 COW
    if (!(hal_flags & HAL_PAGE_COW)) {
        LOG_DEBUG_MSG("COW x64: Not a COW page - addr=0x%llx, flags=0x%x\n",
                     (unsigned long long)addr, hal_flags);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    // 【安全检查】验证旧物理帧地址
    if (old_frame == PADDR_INVALID || old_frame == 0) {
        LOG_ERROR_MSG("COW x64: Invalid old frame address 0x%llx at addr=0x%llx\n", 
                     (unsigned long long)old_frame, (unsigned long long)addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    uint32_t refcount = pmm_frame_get_refcount(old_frame);
    
    LOG_INFO_MSG("COW x64: Handling page fault - addr=0x%llx, old_frame=0x%llx, refcount=%u\n", 
                (unsigned long long)addr, (unsigned long long)old_frame, refcount);
    
    if (refcount == 0) {
        // 异常情况：COW 页面但引用计数为 0
        // 这不应该发生，但为了安全，我们恢复写权限并继续
        LOG_WARN_MSG("COW x64: Page at 0x%llx has refcount=0 but is marked COW, restoring write\n", 
                    (unsigned long long)addr);
        hal_mmu_protect(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, 
                       HAL_PAGE_WRITE, HAL_PAGE_COW);
        hal_mmu_flush_tlb((vaddr_t)addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return true;
    }
    
    if (refcount == 1) {
        // 只有当前进程引用，直接恢复写权限，无需复制
        hal_mmu_protect(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, 
                       HAL_PAGE_WRITE, HAL_PAGE_COW);
        hal_mmu_flush_tlb((vaddr_t)addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        LOG_DEBUG_MSG("COW x64: Single reference (refcount=1), restored write permission\n");
        return true;
    }
    
    // 多个进程共享（refcount > 1），需要复制页面
    paddr_t new_frame = pmm_alloc_frame();
    if (new_frame == PADDR_INVALID) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        LOG_ERROR_MSG("COW x64: Failed to allocate frame for COW copy (out of memory)\n");
        return false;
    }
    
    // 复制页面内容
    void *src_virt = (void*)PHYS_TO_VIRT((uintptr_t)old_frame);
    void *dst_virt = (void*)PHYS_TO_VIRT((uintptr_t)new_frame);
    memcpy(dst_virt, src_virt, PAGE_SIZE);
    
    // 计算新的标志：去掉 COW，恢复写权限
    uint32_t new_flags = (hal_flags & ~HAL_PAGE_COW) | HAL_PAGE_WRITE;
    
    // 取消旧映射并创建新映射
    hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, (vaddr_t)(addr & ~(PAGE_SIZE - 1)));
    hal_mmu_map(HAL_ADDR_SPACE_CURRENT, (vaddr_t)(addr & ~(PAGE_SIZE - 1)), new_frame, new_flags);
    
    // 刷新 TLB
    hal_mmu_flush_tlb((vaddr_t)addr);
    
    // 减少旧页面的引用计数
    pmm_frame_ref_dec(old_frame);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    
    LOG_DEBUG_MSG("COW x64: Copied page (refcount was %u), old_frame=0x%llx -> new_frame=0x%llx\n", 
                 refcount, (unsigned long long)old_frame, (unsigned long long)new_frame);
    return true;
#else
    // error_code bit 1: 写入导致的异常
    // error_code bit 0: 页面存在
    // COW 异常应该是：页面存在(bit0=1) + 写入(bit1=1) = 0x3 或 0x7
    if ((error_code & 0x3) != 0x3) {
        LOG_DEBUG_MSG("COW: Not a COW fault - addr=0x%x, error=0x%x\n", addr, error_code);
        return false;  // 不是写保护异常
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    uint32_t pd_idx = pde_idx(addr);
    uint32_t pt_idx = pte_idx(addr);
    
    // 检查页表是否存在
    if (!is_present(current_dir->entries[pd_idx])) {
        LOG_DEBUG_MSG("COW: Page table not present - addr=0x%x\n", addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    // 【安全检查】验证页表的物理地址
    paddr_t table_phys = get_frame(current_dir->entries[pd_idx]);
    if (table_phys == 0 || table_phys >= 0x80000000) {
        LOG_ERROR_MSG("COW: Invalid page table address 0x%llx at PDE %u\n", (unsigned long long)table_phys, pd_idx);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
    pte_t *pte = &table->entries[pt_idx];
    
    // 检查页面是否存在且标记为 COW
    if (!is_present(*pte) || !(*pte & PAGE_COW)) {
        LOG_DEBUG_MSG("COW: Not a COW page - addr=0x%lx, pte=0x%x, present=%d, cow=%d\n",
                     (unsigned long)addr, *pte, is_present(*pte), (*pte & PAGE_COW) != 0);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;  // 不是 COW 页面
    }
    
    paddr_t old_frame = get_frame(*pte);
    
    // 【安全检查】验证旧物理帧地址
    if (old_frame == 0 || old_frame >= 0x80000000) {
        LOG_ERROR_MSG("COW: Invalid old frame address 0x%llx at addr=0x%lx\n", (unsigned long long)old_frame, (unsigned long)addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    uint32_t refcount = pmm_frame_get_refcount(old_frame);
    
    LOG_INFO_MSG("COW: Handling page fault - addr=0x%lx, old_frame=0x%llx, refcount=%u\n", 
                (unsigned long)addr, (unsigned long long)old_frame, refcount);
    
    if (refcount == 0) {
        // 异常情况：COW 页面但引用计数为 0
        // 这不应该发生，但为了安全，我们恢复写权限并继续
        LOG_WARN_MSG("COW: Page at 0x%lx has refcount=0 but is marked COW, restoring write\n", (unsigned long)addr);
        *pte = (uint32_t)old_frame | ((*pte & 0xFFF) & ~PAGE_COW) | PAGE_WRITE;
        vmm_flush_tlb(addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return true;
    }
    
    if (refcount == 1) {
        // 只有当前进程引用，直接恢复写权限，无需复制
        *pte = (uint32_t)old_frame | ((*pte & 0xFFF) & ~PAGE_COW) | PAGE_WRITE;
        vmm_flush_tlb(addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        LOG_DEBUG_MSG("COW: Single reference (refcount=1), restored write permission\n");
        return true;
    }
    
    // 多个进程共享（refcount > 1），需要复制页面
    paddr_t new_frame = pmm_alloc_frame();
    if (new_frame == PADDR_INVALID) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        LOG_ERROR_MSG("COW: Failed to allocate frame for COW copy (out of memory)\n");
        return false;
    }
    
    // 复制页面内容
    void *src_virt = (void*)PHYS_TO_VIRT((uintptr_t)old_frame);
    void *dst_virt = (void*)PHYS_TO_VIRT((uintptr_t)new_frame);
    memcpy(dst_virt, src_virt, PAGE_SIZE);
    
    // 更新页表项：指向新页面，恢复写权限，去掉 COW 标记
    uint32_t old_flags = *pte & 0xFFF;
    uint32_t new_flags = (old_flags & ~PAGE_COW) | PAGE_WRITE;
    *pte = (uint32_t)new_frame | new_flags;
    
    // 刷新 TLB
    vmm_flush_tlb(addr);
    
    // 减少旧页面的引用计数
    // 注意：这里不调用 pmm_free_frame，因为我们不想释放帧
    // 我们只是减少引用计数，让其他进程继续共享
    pmm_frame_ref_dec(old_frame);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    
    LOG_DEBUG_MSG("COW: Copied page (refcount was %u), old_frame=0x%llx -> new_frame=0x%llx\n", 
                 refcount, (unsigned long long)old_frame, (unsigned long long)new_frame);
    return true;
#endif /* !ARCH_X86_64 */
}

/**
 * @brief 将 VMM 页标志转换为 HAL 页标志
 * @param vmm_flags VMM 页标志 (PAGE_*)
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t vmm_flags_to_hal(uint32_t vmm_flags) {
    uint32_t hal_flags = 0;
    
    if (vmm_flags & PAGE_PRESENT)       hal_flags |= HAL_PAGE_PRESENT;
    if (vmm_flags & PAGE_WRITE)         hal_flags |= HAL_PAGE_WRITE;
    if (vmm_flags & PAGE_USER)          hal_flags |= HAL_PAGE_USER;
    if (vmm_flags & PAGE_CACHE_DISABLE) hal_flags |= HAL_PAGE_NOCACHE;
    if (vmm_flags & PAGE_COW)           hal_flags |= HAL_PAGE_COW;
    
    return hal_flags;
}

/**
 * @brief 将 HAL 页标志转换为 VMM 页标志
 * @param hal_flags HAL 页标志 (HAL_PAGE_*)
 * @return VMM 页标志 (PAGE_*)
 */
__attribute__((unused))
static uint32_t hal_flags_to_vmm(uint32_t hal_flags) {
    uint32_t vmm_flags = 0;
    
    if (hal_flags & HAL_PAGE_PRESENT)   vmm_flags |= PAGE_PRESENT;
    if (hal_flags & HAL_PAGE_WRITE)     vmm_flags |= PAGE_WRITE;
    if (hal_flags & HAL_PAGE_USER)      vmm_flags |= PAGE_USER;
    if (hal_flags & HAL_PAGE_NOCACHE)   vmm_flags |= PAGE_CACHE_DISABLE;
    if (hal_flags & HAL_PAGE_COW)       vmm_flags |= PAGE_COW;
    
    return vmm_flags;
}

/**
 * @brief 映射虚拟页到物理页
 * @param virt 虚拟地址（页对齐）
 * @param phys 物理地址（页对齐）
 * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
 * @return 成功返回 true，失败返回 false
 * 
 * 使用 HAL MMU 接口实现跨架构页面映射
 */
bool vmm_map_page(uintptr_t virt, uintptr_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 转换为 HAL 标志
    uint32_t hal_flags = vmm_flags_to_hal(flags);
    
    // 使用 HAL 接口映射页面
    bool result = hal_mmu_map(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, (paddr_t)phys, hal_flags);
    
    if (result) {
        // 刷新 TLB
        hal_mmu_flush_tlb((vaddr_t)virt);
        
#if !defined(ARCH_X86_64)
        // i686: 如果是内核空间的新映射，同步到主内核页目录
        // 这样其他进程可以通过 page fault handler 同步这个新映射
        if (virt >= KERNEL_VIRTUAL_BASE) {
            page_directory_t *k_dir = (page_directory_t *)boot_page_directory;
            uint32_t pd = pde_idx(virt);
            if (k_dir != current_dir && is_present(current_dir->entries[pd])) {
                k_dir->entries[pd] = current_dir->entries[pd];
            }
        }
#endif
    }
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

/**
 * @brief 取消虚拟页映射
 * @param virt 虚拟地址（页对齐）
 * 
 * 使用 HAL MMU 接口实现跨架构页面取消映射
 */
void vmm_unmap_page(uintptr_t virt) {
    // 检查页对齐
    if (virt & (PAGE_SIZE-1)) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 使用 HAL 接口取消映射
    hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt);
    
    // 刷新 TLB
    hal_mmu_flush_tlb((vaddr_t)virt);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 刷新TLB缓存
 * @param virt 虚拟地址（0表示刷新全部TLB）
 * 
 * 当修改页表后需要刷新TLB以确保CPU使用最新的页表项
 * 通过 HAL 接口调用架构特定的 TLB 刷新操作
 */
void vmm_flush_tlb(uintptr_t virt) {
    if (virt == 0) {
        // 刷新整个TLB
        hal_mmu_flush_tlb_all();
    } else {
        // 刷新单个页
        hal_mmu_flush_tlb(virt);
    }
}

/**
 * @brief 获取当前页目录的物理地址
 * @return 页目录的物理地址
 */
uintptr_t vmm_get_page_directory(void) {
    return current_dir_phys;
}

/**
 * @brief 创建新的页目录（用于新进程）
 * @return 成功返回页目录的物理地址，失败返回 0
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间创建
 */
uintptr_t vmm_create_page_directory(void) {
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 使用 HAL 接口创建新地址空间
    hal_addr_space_t new_space = hal_mmu_create_space();
    
    if (new_space == HAL_ADDR_SPACE_INVALID) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
#if !defined(ARCH_X86_64)
    // i686: 注册为活动页目录并保护内核页表
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT((uintptr_t)new_space);
    protect_directory_range(new_dir, KERNEL_PDE_START, KERNEL_PDE_END);
    register_page_directory((uintptr_t)new_space);
#endif
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return (uintptr_t)new_space;
}

/**
 * @brief 克隆页目录（用于 fork）
 * @param src_dir_phys 源页目录的物理地址
 * @return 成功返回新页目录的物理地址，失败返回 0
 * 
 * 实现 Copy-on-Write (COW) 语义：
 * - 父子进程共享物理页，但页表是独立的
 * - 共享的可写页面被标记为只读 + COW
 * - 首次写入时触发 page fault，由 vmm_handle_cow_page_fault 处理
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间克隆
 */
uintptr_t vmm_clone_page_directory(uintptr_t src_dir_phys) {
#if defined(ARCH_X86_64)
    // x86_64: 使用 HAL 接口克隆地址空间
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    hal_addr_space_t src_space = (src_dir_phys == 0) 
                                 ? HAL_ADDR_SPACE_CURRENT 
                                 : (hal_addr_space_t)src_dir_phys;
    
    hal_addr_space_t new_space = hal_mmu_clone_space(src_space);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    
    if (new_space == HAL_ADDR_SPACE_INVALID) {
        return 0;
    }
    
    return (uintptr_t)new_space;
#else
    // 【安全检查】验证源页目录地址有效
    if (!src_dir_phys || src_dir_phys >= 0x80000000) {
        LOG_ERROR_MSG("vmm_clone_page_directory: Invalid src_dir_phys 0x%lx\n", (unsigned long)src_dir_phys);
        return 0;
    }
    
    // 分配新页目录
    paddr_t new_dir_phys = pmm_alloc_frame();
    if (new_dir_phys == PADDR_INVALID) return 0;
    
    // 【安全检查】确保新分配的帧不与源相同
    if (new_dir_phys == (paddr_t)src_dir_phys) {
        LOG_ERROR_MSG("vmm_clone_page_directory: CRITICAL! PMM returned same frame as source 0x%lx!\n", (unsigned long)src_dir_phys);
        pmm_free_frame(new_dir_phys);
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *src_dir = (page_directory_t*)PHYS_TO_VIRT(src_dir_phys);
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT((uintptr_t)new_dir_phys);
    
    // 【调试检查】验证源页目录前几个 PDE 的完整性
    for (uint32_t i = 0; i < 4; i++) {
        if (is_present(src_dir->entries[i])) {
            uint32_t phys = get_frame(src_dir->entries[i]);
            if (phys == 0 || phys >= 0x80000000) {
                LOG_ERROR_MSG("vmm_clone: CORRUPTED source PDE[%u]=0x%x before clone!\n", 
                             i, src_dir->entries[i]);
            }
        }
    }
    
    // 清空新页目录
    memset(new_dir, 0, sizeof(page_directory_t));
    
    // 复制内核空间映射（共享）
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = src_dir->entries[i];
    }
    protect_directory_range(new_dir, KERNEL_PDE_START, KERNEL_PDE_END);
    
    // 复制用户空间映射（COW方式：复制页表，共享物理页，标记只读）
    LOG_DEBUG_MSG("vmm_clone_cow: Cloning user space with COW (src=0x%lx, new=0x%llx)\n", 
                 (unsigned long)src_dir_phys, (unsigned long long)new_dir_phys);
    
    // 用于跟踪失败时的回滚信息
    uint32_t last_successful_pde = 0;
    bool clone_failed = false;
    
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(src_dir->entries[i])) {
            // 【安全检查】验证源页表的物理地址有效性
            paddr_t src_table_phys = get_frame(src_dir->entries[i]);
            if (src_table_phys >= 0x80000000 || src_table_phys == 0) {
                LOG_ERROR_MSG("vmm_clone_cow: Invalid src_table_phys 0x%llx at PDE %u\n",
                             (unsigned long long)src_table_phys, i);
                clone_failed = true;
                break;
            }
            
            page_table_t *src_table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)src_table_phys);
            
            // COW策略关键修复：为子进程创建新的页表副本，而不是共享页表
            paddr_t new_table_phys = pmm_alloc_frame();
            if (new_table_phys == PADDR_INVALID) {
                LOG_ERROR_MSG("vmm_clone_cow: Failed to allocate page table for PDE %u\n", i);
                clone_failed = true;
                break;
            }
            
            page_table_t *new_table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)new_table_phys);
            
            // 遍历源页表中的每个页面，设置 COW 标记并复制到新页表
            uint32_t cow_pages = 0;
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(src_table->entries[j])) {
                    paddr_t src_frame = get_frame(src_table->entries[j]);
                    uint32_t flags = src_table->entries[j] & 0xFFF;
                    
                    // 如果页面可写，将其改为只读并标记为 COW
                    if (flags & PAGE_WRITE) {
                        flags &= ~PAGE_WRITE;  // 去掉写权限
                        flags |= PAGE_COW;     // 标记为 COW
                        src_table->entries[j] = (uint32_t)src_frame | flags;
                        cow_pages++;
                        
                        // 调试：记录第一个 COW 页面的详细信息
                        if (cow_pages == 1) {
                            LOG_DEBUG_MSG("vmm_clone_cow: First COW page: PDE=%u PTE=%u frame=0x%llx flags=0x%x\n",
                                         i, j, (unsigned long long)src_frame, flags);
                            LOG_DEBUG_MSG("vmm_clone_cow: src_table_phys=0x%llx new_table_phys=0x%llx\n",
                                         (unsigned long long)src_table_phys, (unsigned long long)new_table_phys);
                        }
                    }
                    
                    // 复制页表项到新页表（指向相同的物理页）
                    new_table->entries[j] = (uint32_t)src_frame | flags;
                    
                    // 增加物理页的引用计数（父子进程共享物理页）
                    pmm_frame_ref_inc(src_frame);
                } else {
                    // 空页表项
                    new_table->entries[j] = 0;
                }
            }
            
            LOG_DEBUG_MSG("vmm_clone_cow: PDE %u cloned, %u pages marked COW (src_table=0x%llx, new_table=0x%llx)\n", 
                         i, cow_pages, (unsigned long long)src_table_phys, (unsigned long long)new_table_phys);
            
            // 子进程使用新的页表
            uint32_t pde_flags = src_dir->entries[i] & 0xFFF;
            new_dir->entries[i] = (uint32_t)new_table_phys | pde_flags;
            protect_phys_frame(new_table_phys);
            
            last_successful_pde = i + 1;
        }
    }
    
    // 刷新父进程的TLB（因为我们修改了页表项的权限）
    // 移到循环外面，只刷新一次
    if (src_dir_phys == current_dir_phys && last_successful_pde > 0) {
        vmm_flush_tlb(0);  // 刷新整个 TLB
    }
    
    // 【调试检查】验证克隆后源页目录的完整性
    for (uint32_t i = 0; i < 4; i++) {
        if (is_present(src_dir->entries[i])) {
            uint32_t phys = get_frame(src_dir->entries[i]);
            if (phys == 0 || phys >= 0x80000000) {
                LOG_ERROR_MSG("vmm_clone: Source PDE[%u]=0x%x CORRUPTED after clone!\n", 
                             i, src_dir->entries[i]);
            }
        }
    }
    
    // 处理克隆失败的情况
    if (clone_failed) {
        LOG_WARN_MSG("vmm_clone_cow: Clone failed, cleaning up (processed %u PDEs)\n", 
                    last_successful_pde);
        
        // 注意：此时源页表的 COW 标记已经设置，但这不会造成问题
        // 因为 vmm_handle_cow_page_fault 会正确处理 refcount == 1 的情况
        // （直接恢复写权限，无需复制）
        
        // 释放已分配的新页目录资源
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        vmm_free_page_directory(new_dir_phys);
        return 0;
    }
    
    // 注册为活动页目录
    register_page_directory((uintptr_t)new_dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return (uintptr_t)new_dir_phys;
#endif /* !ARCH_X86_64 */
}

#if !defined(ARCH_X86_64)
/**
 * @brief 检查页目录是否被任何任务使用 (i686 only)
 * @param dir_phys 页目录的物理地址
 * @return 如果被使用返回 true，否则返回 false
 */
static bool is_page_directory_in_use(uintptr_t dir_phys) {
    // task_pool 在 task.h 中声明，在 task.c 中定义
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        // 只有活跃状态的任务才算"在使用"页目录
        // TASK_UNUSED: 空闲槽位
        // TASK_ZOMBIE: 已退出，等待回收（页目录可以释放）
        // TASK_TERMINATED: 已终止
        if (task_pool[i].state != TASK_UNUSED && 
            task_pool[i].state != TASK_ZOMBIE &&
            task_pool[i].state != TASK_TERMINATED &&
            task_pool[i].page_dir_phys == dir_phys) {
            return true;
        }
    }
    return false;
}
#endif

/**
 * @brief 释放页目录及其用户空间页表和物理页
 * @param dir_phys 页目录的物理地址
 * 
 * 注意：由于 fork 现在使用深拷贝，所以需要释放所有物理页
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间销毁
 */
void vmm_free_page_directory(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
#if defined(ARCH_X86_64)
    // x86_64: 使用 HAL 接口销毁地址空间
    
    // 【安全检查】防止释放当前正在使用的页目录
    if (dir_phys == current_dir_phys) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Attempting to free current page directory 0x%llx!\n", 
                     (unsigned long long)dir_phys);
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    hal_mmu_destroy_space((hal_addr_space_t)dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return;
#else
    LOG_INFO_MSG("vmm_free_page_directory: Attempting to free page directory 0x%lx\n", (unsigned long)dir_phys);
    
    // 【安全检查】防止释放当前正在使用的页目录
    if (dir_phys == current_dir_phys) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Attempting to free current page directory 0x%lx!\n", (unsigned long)dir_phys);
        return;
    }
    
    // 【安全检查】防止释放主内核页目录
    uintptr_t boot_dir_phys = VIRT_TO_PHYS((uintptr_t)boot_page_directory);
    if (dir_phys == boot_dir_phys) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Attempting to free boot page directory 0x%lx!\n", (unsigned long)dir_phys);
        return;
    }
    
    // 【关键修复】检查页目录是否仍被其他任务使用
    if (is_page_directory_in_use(dir_phys)) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Page directory 0x%lx is still in use by a task!\n", (unsigned long)dir_phys);
        return;
    }
    
    // 【新增检查】验证这个页目录是否在活动列表中
    if (!is_active_page_directory(dir_phys)) {
        LOG_ERROR_MSG("vmm_free_page_directory: WARNING! Page directory 0x%lx is not in active list!\n", (unsigned long)dir_phys);
        LOG_ERROR_MSG("  This might be a double-free or invalid pointer. Proceeding cautiously...\n");
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    uint32_t freed_pages = 0;
    uint32_t freed_tables = 0;
    
    // 获取当前的内存使用量
    pmm_info_t info_start = pmm_get_info();
    
    // LOG_DEBUG_MSG("vmm_free_page_directory: dir_phys=0x%x\n", dir_phys);
    
    // 只释放用户空间页表（0-511）
    // 内核空间页表（512-1023）是共享的，不释放
    // COW 模式下：页表和页面都可能被共享，依靠引用计数自动管理
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(dir->entries[i])) {
            paddr_t table_phys = get_frame(dir->entries[i]);
            if (table_phys == 0) {
                LOG_ERROR_MSG("vmm_free_page_directory: PDE %u has zero frame, skipping\n", i);
                dir->entries[i] = 0;
                continue;
            }
            
            page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
            uint32_t pages_in_table = 0;
            
            // 释放页表中的所有物理页（pmm_free_frame 自动处理引用计数）
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(table->entries[j])) {
                    paddr_t frame = get_frame(table->entries[j]);
                    
                    // 基本安全检查
                    if (frame == 0 || frame >= 0x80000000) {
                        LOG_WARN_MSG("vmm_free: PDE %u PTE %u invalid frame 0x%llx\n", i, j, (unsigned long long)frame);
                        freed_pages++;
                        pages_in_table++;
                        continue;
                    }
                    
                    // pmm_free_frame 会自动处理引用计数：
                    // - 如果 refcount > 1，只递减，不释放
                    // - 如果 refcount == 1，递减后释放
                    pmm_free_frame(frame);
                    freed_pages++;
                    pages_in_table++;
                }
            }
            
            // 释放页表本身（同样由 pmm_free_frame 处理引用计数）
            unprotect_phys_frame(table_phys);
            pmm_free_frame(table_phys);
            freed_tables++;
            
            // 打印栈区域的详细信息
            if (i >= 510) {
                LOG_INFO_MSG("vmm_free_page_directory: PDE %u has %u pages\n", i, pages_in_table);
            }
            
            dir->entries[i] = 0;
        }
    }

    // 释放内核共享页表（仅移除本页目录的引用，不释放物理帧）
    release_directory_range(dir, KERNEL_PDE_START, KERNEL_PDE_END, true);

    // 注销活动页目录（如果已注册）
    if (is_active_page_directory(dir_phys)) {
        unregister_page_directory(dir_phys);
    } else {
        LOG_WARN_MSG("vmm_free_page_directory: dir 0x%lx was not registered\n", (unsigned long)dir_phys);
    }
    
    // 释放页目录本身
    LOG_DEBUG_MSG("vmm_free_page_directory: freeing page directory at phys 0x%lx (virt 0x%lx)\n", 
                  (unsigned long)dir_phys, (unsigned long)dir);
    pmm_free_frame((paddr_t)dir_phys);
    
    pmm_info_t info_end = pmm_get_info();
    LOG_INFO_MSG("vmm_free_page_directory: freed %u pages (PMM: %llu -> %llu, diff %d), %u tables, 1 directory\n", 
                  freed_pages, (unsigned long long)info_start.used_frames, (unsigned long long)info_end.used_frames, 
                  (int)(info_start.used_frames - info_end.used_frames), freed_tables);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
#endif /* !ARCH_X86_64 */
}

/**
 * @brief 同步 VMM 的 current_dir_phys（不切换 CR3）
 * @param dir_phys 当前页目录的物理地址
 * 
 * 仅更新内部状态变量，不修改 CR3 寄存器
 * 用于在 task_switch_context 已经切换 CR3 后同步状态
 */
void vmm_sync_current_dir(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 切换到指定的页目录
 * @param dir_phys 页目录的物理地址
 * 
 * 通过 HAL 接口切换地址空间
 */
void vmm_switch_page_directory(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 通过 HAL 接口切换地址空间
    hal_mmu_switch_space(dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 在指定页目录中映射页面
 * @param dir_phys 页目录的物理地址
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 * @return 成功返回 true，失败返回 false
 * 
 * 使用 HAL MMU 接口实现跨架构页面映射
 */
bool vmm_map_page_in_directory(uintptr_t dir_phys, uintptr_t virt, 
                                uintptr_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 转换为 HAL 标志
    uint32_t hal_flags = vmm_flags_to_hal(flags);
    
    // 使用 HAL 接口映射页面
    hal_addr_space_t space = (dir_phys == current_dir_phys) 
                             ? HAL_ADDR_SPACE_CURRENT 
                             : (hal_addr_space_t)dir_phys;
    
    bool result = hal_mmu_map(space, (vaddr_t)virt, (paddr_t)phys, hal_flags);
    
    // 如果是当前页目录，刷新 TLB
    if (result && dir_phys == current_dir_phys) {
        hal_mmu_flush_tlb((vaddr_t)virt);
    }
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

uintptr_t vmm_unmap_page_in_directory(uintptr_t dir_phys, uintptr_t virt) {
    if (virt & (PAGE_SIZE - 1)) {
        return 0;
    }

    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);

    // 使用 HAL 接口查询原物理地址
    hal_addr_space_t space = (dir_phys == current_dir_phys) 
                             ? HAL_ADDR_SPACE_CURRENT 
                             : (hal_addr_space_t)dir_phys;
    
    paddr_t old_phys;
    if (!hal_mmu_query(space, (vaddr_t)virt, &old_phys, NULL)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }

    // 使用 HAL 接口取消映射
    hal_mmu_unmap(space, (vaddr_t)virt);

    // 如果是当前页目录，刷新 TLB
    if (dir_phys == current_dir_phys) {
        hal_mmu_flush_tlb((vaddr_t)virt);
    }

    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return (uintptr_t)old_phys;
}

/**
 * @brief 清理指定范围内的空页表
 * @param dir_phys 页目录的物理地址
 * @param start_virt 起始虚拟地址（页对齐）
 * @param end_virt 结束虚拟地址（页对齐）
 * 
 * 检查指定虚拟地址范围内的页表，如果页表为空（所有条目都未映射），
 * 则释放该页表并清除对应的页目录项。
 */
void vmm_cleanup_empty_page_tables(uintptr_t dir_phys, uintptr_t start_virt, uintptr_t end_virt) {
    if (!dir_phys || start_virt >= end_virt) {
        return;
    }
    
    // 只处理用户空间
    if (start_virt >= KERNEL_VIRTUAL_BASE || end_virt > KERNEL_VIRTUAL_BASE) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
#if defined(ARCH_X86_64)
    // x86_64: 4-level paging - more complex cleanup needed
    // For now, skip cleanup on x86_64 as it requires walking multiple levels
    (void)dir;
    (void)start_virt;
    (void)end_virt;
#else
    // i686: 2-level paging
    uint32_t start_pde = pde_idx(start_virt);
    uint32_t end_pde = pde_idx(end_virt - 1);  // -1 because end_virt is exclusive
    
    for (uint32_t pd = start_pde; pd <= end_pde; pd++) {
        pde_t pde = dir->entries[pd];
        
        // Skip if PDE is not present
        if (!is_present(pde)) {
            continue;
        }
        
        // Get page table
        uintptr_t table_phys = get_frame(pde);
        page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
        
        // Check if all entries in the page table are empty
        bool is_empty = true;
        for (uint32_t pt = 0; pt < 1024; pt++) {
            if (is_present(table->entries[pt])) {
                is_empty = false;
                break;
            }
        }
        
        // If page table is empty, free it and clear the PDE
        if (is_empty) {
            dir->entries[pd] = 0;
            pmm_free_frame((paddr_t)table_phys);
        }
    }
#endif
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/* ============================================================================
 * MMIO 映射
 * ============================================================================ */

#if defined(ARCH_X86_64)
/** MMIO 映射区域起始地址（在内核空间的高地址区域） */
#define MMIO_VIRT_BASE      0xFFFF800040000000ULL
#define MMIO_VIRT_END       0xFFFF8000C0000000ULL
#else
/** MMIO 映射区域起始地址（在内核空间的高地址区域） */
#define MMIO_VIRT_BASE      0xF0000000
#define MMIO_VIRT_END       0xFFC00000  // 保留最后 4MB 给递归页表等
#endif

/** 当前 MMIO 虚拟地址分配位置 */
static uintptr_t mmio_next_virt = MMIO_VIRT_BASE;

/** PAT 初始化标志 */
static bool pat_initialized = false;

/**
 * @brief 映射 MMIO 区域
 * @param phys_addr 物理地址
 * @param size 映射大小（字节）
 * @return 成功返回映射的虚拟地址，失败返回 0
 * 
 * 使用 HAL MMU 接口实现跨架构 MMIO 映射
 */
uintptr_t vmm_map_mmio(uintptr_t phys_addr, size_t size) {
    if (size == 0) {
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 计算需要的页数（向上取整）
    uintptr_t phys_start = PAGE_ALIGN_DOWN(phys_addr);
    uintptr_t phys_end = PAGE_ALIGN_UP(phys_addr + size);
    uint32_t num_pages = (phys_end - phys_start) / PAGE_SIZE;
    
    // 分配虚拟地址空间
    uintptr_t virt_start = mmio_next_virt;
    if (virt_start + num_pages * PAGE_SIZE > MMIO_VIRT_END) {
        LOG_ERROR_MSG("vmm_map_mmio: No more MMIO virtual address space\n");
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
    // 使用 HAL 标志：PRESENT + WRITE + NOCACHE
    uint32_t hal_flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_NOCACHE;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uintptr_t virt = virt_start + i * PAGE_SIZE;
        uintptr_t phys = phys_start + i * PAGE_SIZE;
        
        // 使用 HAL 接口映射页面
        if (!hal_mmu_map(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, (paddr_t)phys, hal_flags)) {
            // 回滚已映射的页面
            for (uint32_t j = 0; j < i; j++) {
                hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, (vaddr_t)(virt_start + j * PAGE_SIZE));
            }
            spinlock_unlock_irqrestore(&vmm_lock, irq_state);
            return 0;
        }
        hal_mmu_flush_tlb((vaddr_t)virt);
    }
    
    // 更新下一个可用的 MMIO 虚拟地址
    mmio_next_virt = virt_start + num_pages * PAGE_SIZE;
    
    // 计算返回地址：虚拟基址 + 物理地址页内偏移
    uintptr_t offset = phys_addr - phys_start;
    uintptr_t result = virt_start + offset;
    
    LOG_INFO_MSG("vmm_map_mmio: mapped phys 0x%lx size 0x%lx -> virt 0x%lx\n", 
                 (unsigned long)phys_addr, (unsigned long)size, (unsigned long)result);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

/**
 * @brief 取消 MMIO 区域映射
 * @param virt_addr 虚拟地址
 * @param size 映射大小（字节）
 * 
 * 使用 HAL MMU 接口实现跨架构 MMIO 取消映射
 */
void vmm_unmap_mmio(uintptr_t virt_addr, size_t size) {
    if (size == 0 || virt_addr < MMIO_VIRT_BASE || virt_addr >= MMIO_VIRT_END) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    uintptr_t virt_start = PAGE_ALIGN_DOWN(virt_addr);
    uintptr_t virt_end = PAGE_ALIGN_UP(virt_addr + size);
    
    for (uintptr_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
        hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt);
        hal_mmu_flush_tlb((vaddr_t)virt);
    }
    
    LOG_INFO_MSG("vmm_unmap_mmio: unmapped virt 0x%lx size 0x%lx\n", (unsigned long)virt_addr, (unsigned long)size);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 通过查询页表获取虚拟地址对应的物理地址
 * @param virt 虚拟地址
 * @return 物理地址，如果虚拟地址未映射则返回 0
 * 
 * 使用 HAL MMU 接口实现跨架构地址转换
 */
uintptr_t vmm_virt_to_phys(uintptr_t virt) {
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    paddr_t phys;
    if (!hal_mmu_query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, &phys, NULL)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
    // 物理地址 = 页帧地址 + 页内偏移
    uintptr_t result = (uintptr_t)phys | (virt & (PAGE_SIZE - 1));
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

/* ============================================================================
 * PAT (Page Attribute Table) 支持
 * ============================================================================ */

/**
 * PAT (Page Attribute Table) 内存类型定义
 * 
 * PAT 是 Intel 在 Pentium III 引入的机制，用于更灵活地控制内存缓存类型。
 * PAT 使用页表项中的 3 个位（PAT、PCD、PWT）组合成 3 位索引，
 * 从 IA32_PAT MSR 中选择内存类型。
 */
#define PAT_TYPE_UC     0   // Uncacheable
#define PAT_TYPE_WC     1   // Write-Combining
#define PAT_TYPE_WT     4   // Write-Through
#define PAT_TYPE_WP     5   // Write-Protected
#define PAT_TYPE_WB     6   // Write-Back
#define PAT_TYPE_UC_    7   // Uncacheable (UC-)

/** IA32_PAT MSR 地址 */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#define MSR_IA32_PAT    0x277

/**
 * @brief 写入 MSR 寄存器 (x86 only)
 */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/**
 * @brief 检查 CPU 是否支持 PAT (x86 only)
 */
static bool cpu_has_pat(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" 
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1));
    return (edx & (1 << 16)) != 0;  // PAT 在 EDX bit 16
}
#endif /* ARCH_I686 || ARCH_X86_64 */

/**
 * @brief 初始化 PAT (Page Attribute Table)
 * 
 * 配置 PAT 以支持 Write-Combining 内存类型：
 * - PAT[0] = WB  (默认，用于普通内存)
 * - PAT[1] = WT  (Write-Through)
 * - PAT[2] = UC- (Uncacheable)
 * - PAT[3] = UC  (Uncacheable)
 * - PAT[4] = WB  (Write-Back)
 * - PAT[5] = WT  (Write-Through)
 * - PAT[6] = UC- (Uncacheable)
 * - PAT[7] = WC  (Write-Combining - 用于帧缓冲)
 * 
 * 页表项组合：
 * - PAT=0, PCD=0, PWT=0 -> PAT[0] = WB
 * - PAT=0, PCD=0, PWT=1 -> PAT[1] = WT
 * - PAT=0, PCD=1, PWT=0 -> PAT[2] = UC-
 * - PAT=0, PCD=1, PWT=1 -> PAT[3] = UC
 * - PAT=1, PCD=0, PWT=0 -> PAT[4] = WB
 * - PAT=1, PCD=0, PWT=1 -> PAT[5] = WT
 * - PAT=1, PCD=1, PWT=0 -> PAT[6] = UC-
 * - PAT=1, PCD=1, PWT=1 -> PAT[7] = WC  <-- 用于帧缓冲
 */
void vmm_init_pat(void) {
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    if (!cpu_has_pat()) {
        LOG_WARN_MSG("vmm: PAT not supported by CPU, framebuffer will use UC mode\n");
        return;
    }
    
    // 构建 PAT 值：每个条目 8 位
    // PAT[7] = WC (1), PAT[6] = UC- (7), PAT[5] = WT (4), PAT[4] = WB (6)
    // PAT[3] = UC (0), PAT[2] = UC- (7), PAT[1] = WT (4), PAT[0] = WB (6)
    uint64_t pat_value = 
        ((uint64_t)PAT_TYPE_WC  << 56) |  // PAT[7] = WC (Write-Combining)
        ((uint64_t)PAT_TYPE_UC_ << 48) |  // PAT[6] = UC-
        ((uint64_t)PAT_TYPE_WT  << 40) |  // PAT[5] = WT
        ((uint64_t)PAT_TYPE_WB  << 32) |  // PAT[4] = WB
        ((uint64_t)PAT_TYPE_UC  << 24) |  // PAT[3] = UC
        ((uint64_t)PAT_TYPE_UC_ << 16) |  // PAT[2] = UC-
        ((uint64_t)PAT_TYPE_WT  <<  8) |  // PAT[1] = WT
        ((uint64_t)PAT_TYPE_WB  <<  0);   // PAT[0] = WB
    
    wrmsr(MSR_IA32_PAT, pat_value);
    pat_initialized = true;
    
    LOG_INFO_MSG("vmm: PAT initialized (WC mode available for framebuffer)\n");
#else
    /* ARM64: Memory attributes are configured via MAIR_EL1 in mmu.c */
    LOG_INFO_MSG("vmm: PAT not applicable on ARM64 (using MAIR_EL1)\n");
#endif
}

/**
 * @brief 映射帧缓冲区域（使用 Write-Combining 模式）
 * @param phys_addr 物理地址
 * @param size 映射大小（字节）
 * @return 成功返回映射的虚拟地址，失败返回 0
 * 
 * 使用 PAT[7] = WC 模式，需要设置 PAT=1, PCD=1, PWT=1
 * 如果 PAT 不可用，回退到 UC 模式
 * 
 * 使用 HAL MMU 接口实现跨架构帧缓冲映射
 */
uintptr_t vmm_map_framebuffer(uintptr_t phys_addr, size_t size) {
    if (size == 0) {
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 计算需要的页数（向上取整）
    uintptr_t phys_start = PAGE_ALIGN_DOWN(phys_addr);
    uintptr_t phys_end = PAGE_ALIGN_UP(phys_addr + size);
    uint32_t num_pages = (phys_end - phys_start) / PAGE_SIZE;
    
    // 分配虚拟地址空间
    uintptr_t virt_start = mmio_next_virt;
    if (virt_start + num_pages * PAGE_SIZE > MMIO_VIRT_END) {
        LOG_ERROR_MSG("vmm_map_framebuffer: No more MMIO virtual address space\n");
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
    // 根据 PAT 是否可用选择 HAL 标志
    uint32_t hal_flags;
    if (pat_initialized) {
        // Write-Combining 模式
        hal_flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_WRITECOMB;
        LOG_INFO_MSG("vmm_map_framebuffer: using Write-Combining mode\n");
    } else {
        // 回退到 UC 模式
        hal_flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_NOCACHE;
        LOG_WARN_MSG("vmm_map_framebuffer: PAT not available, using UC mode\n");
    }
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uintptr_t virt = virt_start + i * PAGE_SIZE;
        uintptr_t phys = phys_start + i * PAGE_SIZE;
        
        // 使用 HAL 接口映射页面
        if (!hal_mmu_map(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, (paddr_t)phys, hal_flags)) {
            // 回滚已映射的页面
            for (uint32_t j = 0; j < i; j++) {
                hal_mmu_unmap(HAL_ADDR_SPACE_CURRENT, (vaddr_t)(virt_start + j * PAGE_SIZE));
            }
            spinlock_unlock_irqrestore(&vmm_lock, irq_state);
            return 0;
        }
        hal_mmu_flush_tlb((vaddr_t)virt);
    }
    
    // 更新下一个可用的 MMIO 虚拟地址
    mmio_next_virt = virt_start + num_pages * PAGE_SIZE;
    
    // 计算返回地址：虚拟基址 + 物理地址页内偏移
    uintptr_t offset = phys_addr - phys_start;
    uintptr_t result = virt_start + offset;
    
    LOG_INFO_MSG("vmm_map_framebuffer: mapped phys 0x%lx size 0x%lx -> virt 0x%lx\n", 
                 (unsigned long)phys_addr, (unsigned long)size, (unsigned long)result);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

/*============================================================================
 * 调试功能：页表转储
 * @see Requirements 11.1
 *============================================================================*/

/**
 * @brief 将页标志转换为可读字符串
 * @param flags 页标志
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 */
static void flags_to_string(uint32_t flags, char *buf, size_t buf_size) {
    if (buf_size < 16) return;
    
    buf[0] = (flags & PAGE_PRESENT) ? 'P' : '-';
    buf[1] = (flags & PAGE_WRITE) ? 'W' : 'R';
    buf[2] = (flags & PAGE_USER) ? 'U' : 'K';
    buf[3] = (flags & PAGE_WRITE_THROUGH) ? 'T' : '-';
    buf[4] = (flags & PAGE_CACHE_DISABLE) ? 'N' : '-';
    buf[5] = (flags & PAGE_COW) ? 'C' : '-';
    buf[6] = '\0';
}

/**
 * @brief 转储页表内容（调试功能）
 * @param dir_phys 页目录的物理地址（0 表示当前页目录）
 * @param start_virt 起始虚拟地址
 * @param end_virt 结束虚拟地址
 */
void vmm_dump_page_tables(uintptr_t dir_phys, uintptr_t start_virt, uintptr_t end_virt) {
    if (dir_phys == 0) {
        dir_phys = current_dir_phys;
    }
    
    if (dir_phys == 0) {
        kprintf("VMM: No page directory available\n");
        return;
    }
    
    // 页对齐
    start_virt = PAGE_ALIGN_DOWN(start_virt);
    end_virt = PAGE_ALIGN_UP(end_virt);
    
    kprintf("\n==================== Page Table Dump ====================\n");
    kprintf("Page Directory: phys=0x%lx\n", (unsigned long)dir_phys);
    kprintf("Range: 0x%lx - 0x%lx\n", (unsigned long)start_virt, (unsigned long)end_virt);
    kprintf("----------------------------------------------------------\n");
    kprintf("Virtual Addr     Physical Addr    Flags   Refcount\n");
    kprintf("----------------------------------------------------------\n");
    
    uint32_t mapped_count = 0;
    uint32_t cow_count = 0;
    
#if defined(ARCH_X86_64)
    // x86_64: 4-level paging
    pml4_t *pml4 = (pml4_t*)PHYS_TO_VIRT(dir_phys);
    
    for (uintptr_t virt = start_virt; virt < end_virt; virt += PAGE_SIZE) {
        uint32_t pml4_i = pml4_idx(virt);
        if (!is_present64(pml4->entries[pml4_i])) continue;
        
        pdpt_t *pdpt = (pdpt_t*)PHYS_TO_VIRT(get_frame64(pml4->entries[pml4_i]));
        uint32_t pdpt_i = pdpt_idx(virt);
        if (!is_present64(pdpt->entries[pdpt_i])) continue;
        
        pd_t *pd = (pd_t*)PHYS_TO_VIRT(get_frame64(pdpt->entries[pdpt_i]));
        uint32_t pd_i = pd_idx(virt);
        if (!is_present64(pd->entries[pd_i])) continue;
        
        pt_t *pt = (pt_t*)PHYS_TO_VIRT(get_frame64(pd->entries[pd_i]));
        uint32_t pt_i = pt_idx(virt);
        if (!is_present64(pt->entries[pt_i])) continue;
        
        paddr_t phys = get_frame64(pt->entries[pt_i]);
        uint32_t flags = (uint32_t)(pt->entries[pt_i] & 0xFFF);
        uint32_t refcount = pmm_frame_get_refcount(phys);
        
        char flags_str[16];
        flags_to_string(flags, flags_str, sizeof(flags_str));
        
        kprintf("0x%016llx 0x%016llx %s    %u\n",
               (unsigned long long)virt, (unsigned long long)phys, 
               flags_str, refcount);
        
        mapped_count++;
        if (flags & PAGE_COW) cow_count++;
        
        // 限制输出数量
        if (mapped_count >= 100) {
            kprintf("... (truncated, showing first 100 mappings)\n");
            break;
        }
    }
#else
    // i686: 2-level paging
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    for (uintptr_t virt = start_virt; virt < end_virt; virt += PAGE_SIZE) {
        uint32_t pd_i = pde_idx(virt);
        if (!is_present(dir->entries[pd_i])) continue;
        
        paddr_t table_phys = get_frame(dir->entries[pd_i]);
        if (table_phys == 0 || table_phys >= 0x80000000) continue;
        
        page_table_t *table = (page_table_t*)PHYS_TO_VIRT((uintptr_t)table_phys);
        uint32_t pt_i = pte_idx(virt);
        if (!is_present(table->entries[pt_i])) continue;
        
        paddr_t phys = get_frame(table->entries[pt_i]);
        uint32_t flags = table->entries[pt_i] & 0xFFF;
        uint32_t refcount = pmm_frame_get_refcount(phys);
        
        char flags_str[16];
        flags_to_string(flags, flags_str, sizeof(flags_str));
        
        kprintf("0x%08lx       0x%08llx       %s    %u\n",
               (unsigned long)virt, (unsigned long long)phys, 
               flags_str, refcount);
        
        mapped_count++;
        if (flags & PAGE_COW) cow_count++;
        
        // 限制输出数量
        if (mapped_count >= 100) {
            kprintf("... (truncated, showing first 100 mappings)\n");
            break;
        }
    }
#endif
    
    kprintf("----------------------------------------------------------\n");
    kprintf("Summary: %u mapped pages, %u COW pages\n", mapped_count, cow_count);
    kprintf("Flags: P=Present, W=Write, R=ReadOnly, U=User, K=Kernel\n");
    kprintf("       T=WriteThrough, N=NoCache, C=COW\n");
    kprintf("==========================================================\n\n");
}

/**
 * @brief 转储当前页目录的用户空间映射
 */
void vmm_dump_user_mappings(void) {
    vmm_dump_page_tables(0, 0x00000000, KERNEL_VIRTUAL_BASE);
}

/**
 * @brief 转储当前页目录的内核空间映射
 */
void vmm_dump_kernel_mappings(void) {
#if defined(ARCH_X86_64)
    // x86_64: 内核空间从 0xFFFF800000000000 开始
    // 只转储前 1GB 以避免输出过多
    vmm_dump_page_tables(0, KERNEL_VIRTUAL_BASE, KERNEL_VIRTUAL_BASE + 0x40000000ULL);
#else
    // i686: 内核空间从 0x80000000 开始
    // 只转储前 256MB 以避免输出过多
    vmm_dump_page_tables(0, KERNEL_VIRTUAL_BASE, KERNEL_VIRTUAL_BASE + 0x10000000);
#endif
}
