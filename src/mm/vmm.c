/**
 * @file vmm.c
 * @brief 虚拟内存管理器实现
 * 
 * 实现x86分页机制，管理虚拟地址到物理地址的映射
 */

#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <kernel/sync/spinlock.h>
#include <kernel/task.h>

static page_directory_t *current_dir = NULL;  ///< 当前页目录虚拟地址
static uint32_t current_dir_phys = 0;          ///< 当前页目录物理地址
extern uint32_t boot_page_directory[];         ///< 引导时的页目录
static spinlock_t vmm_lock;                    ///< VMM 自旋锁，保护页表操作

#define KERNEL_PDE_START 512
#define KERNEL_PDE_END   1024

// 活动页目录跟踪（防止页目录被意外覆盖）
#define MAX_PAGE_DIRECTORIES 64
static uint32_t active_page_directories[MAX_PAGE_DIRECTORIES];
static uint32_t active_pd_count = 0;

/**
 * @brief 获取页目录索引
 * @param v 虚拟地址
 * @return 页目录索引（高10位）
 */
static inline uint32_t pde_idx(uint32_t v) { return v >> 22; }

/**
 * @brief 获取页表索引
 * @param v 虚拟地址
 * @return 页表索引（中间10位）
 */
static inline uint32_t pte_idx(uint32_t v) { return (v >> 12) & 0x3FF; }

/**
 * @brief 从页表项/页目录项中提取物理地址
 * @param e 页表项或页目录项
 * @return 物理地址（页对齐）
 */
static inline uint32_t get_frame(uint32_t e) { return e & 0xFFFFF000; }

/**
 * @brief 检查页表项/页目录项是否存在
 * @param e 页表项或页目录项
 * @return 存在返回 true，否则返回 false
 */
static inline bool is_present(uint32_t e) { return e & PAGE_PRESENT; }

/**
 * @brief 检查物理帧是否是活动页目录
 * @param frame 物理帧地址
 * @return 是活动页目录返回 true，否则返回 false
 */
static bool is_active_page_directory(uint32_t frame) {
    for (uint32_t i = 0; i < active_pd_count; i++) {
        if (active_page_directories[i] == frame) {
            return true;
        }
    }
    return false;
}

static inline void protect_phys_frame(uint32_t frame) {
    if (frame) {
        pmm_protect_frame(frame);
    }
}

static inline void unprotect_phys_frame(uint32_t frame) {
    if (frame) {
        pmm_unprotect_frame(frame);
    }
}

static void protect_directory_range(page_directory_t *dir, uint32_t start_idx, uint32_t end_idx) {
    for (uint32_t i = start_idx; i < end_idx; i++) {
        pde_t entry = dir->entries[i];
        if (is_present(entry)) {
            uint32_t frame = get_frame(entry);
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
            uint32_t frame = get_frame(entry);
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
static void register_page_directory(uint32_t dir_phys) {
    if (active_pd_count >= MAX_PAGE_DIRECTORIES) {
        LOG_ERROR_MSG("VMM: Too many active page directories! Cannot register 0x%x\n", dir_phys);
        return;
    }
    
    // 检查是否已注册
    if (is_active_page_directory(dir_phys)) {
        LOG_WARN_MSG("VMM: Page directory 0x%x already registered\n", dir_phys);
        return;
    }
    
    protect_phys_frame(dir_phys);
    active_page_directories[active_pd_count++] = dir_phys;
    LOG_INFO_MSG("VMM: Registered page directory 0x%x (total: %u)\n", dir_phys, active_pd_count);
}

/**
 * @brief 注销活动页目录
 * @param dir_phys 页目录的物理地址
 */
static void unregister_page_directory(uint32_t dir_phys) {
    for (uint32_t i = 0; i < active_pd_count; i++) {
        if (active_page_directories[i] == dir_phys) {
            unprotect_phys_frame(dir_phys);
            // 用最后一个元素替换当前元素
            active_page_directories[i] = active_page_directories[--active_pd_count];
            LOG_INFO_MSG("VMM: Unregistered page directory 0x%x (remaining: %u)\n", dir_phys, active_pd_count);
            return;
        }
    }
    LOG_ERROR_MSG("VMM: ERROR: Tried to unregister unknown page directory 0x%x\n", dir_phys);
}

/**
 * @brief 创建新的页表
 * @return 成功返回页表虚拟地址，失败返回 NULL
 */
static page_table_t* create_page_table(void) {
    uint32_t frame = pmm_alloc_frame();
    if (!frame) return NULL;
    return (page_table_t*)PHYS_TO_VIRT(frame);
}

/**
 * @brief 初始化虚拟内存管理器
 * 
 * 使用引导时创建的页目录，设置CR3寄存器
 * 扩展高半核映射以覆盖所有可用的物理内存
 */
void vmm_init(void) {
    // 初始化 VMM 自旋锁
    spinlock_init(&vmm_lock);
    
    current_dir = (page_directory_t*)boot_page_directory;
    current_dir_phys = VIRT_TO_PHYS((uint32_t)current_dir);
    
    // 检查并更新CR3寄存器
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 != current_dir_phys)
        __asm__ volatile("mov %0, %%cr3" : : "r"(current_dir_phys));
    
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
    // 引导时已经映射了前8MB（索引512-513），从索引514开始
    uint32_t start_pde = 514;  // 对应虚拟地址 0x80800000
    // 修复：使用向上取整，确保覆盖所有物理内存。如果 max_phys 不是 4MB 对齐，
    // 向下取整会导致末尾的内存无法被映射。
    uint32_t end_pde = 512 + ((max_phys + 0x3FFFFF) >> 22); 
    
    LOG_DEBUG_MSG("VMM: Extending high-half kernel mapping\n");
    LOG_DEBUG_MSG("  Physical memory: %u MB\n", max_phys / (1024*1024));
    LOG_DEBUG_MSG("  Mapping PDEs: %u-%u (virtual: 0x%x-0x%x)\n",
                 start_pde, end_pde - 1,
                 start_pde << 22, (end_pde << 22) - 1);
    
    // 为每个页目录项创建页表并映射
    for (uint32_t pde = start_pde; pde < end_pde; pde++) {
        // 检查页目录项是否已存在
        if (is_present(current_dir->entries[pde])) {
            continue;  // 已经映射，跳过
        }
        
        // 分配页表
        uint32_t table_phys = pmm_alloc_frame();
        if (!table_phys) {
            LOG_WARN_MSG("VMM: Failed to allocate page table for PDE %u\n", pde);
            break;  // 分配失败，停止扩展
        }
        
        page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
        memset(table, 0, sizeof(page_table_t));
        
        // 计算这个页表对应的物理地址范围
        // PDE索引pde对应虚拟地址 pde * 4MB
        // 对应的物理地址也是 pde * 4MB（高半核恒等映射）
        uint32_t phys_base = (pde - 512) << 22;  // 物理地址基址
        
        // 填充页表项：每个页表项映射一个4KB页
        for (uint32_t pte = 0; pte < 1024; pte++) {
            uint32_t phys_addr = phys_base + (pte << 12);
            
            // 只映射在可用物理内存范围内的页
            if (phys_addr < max_phys) {
                // 设置页表项：Present | Read/Write | Supervisor
                table->entries[pte] = phys_addr | PAGE_PRESENT | PAGE_WRITE;
            }
        }
        
        // 设置页目录项：指向页表
        current_dir->entries[pde] = table_phys | PAGE_PRESENT | PAGE_WRITE;
    }
    
    // 刷新TLB以确保新映射生效
    vmm_flush_tlb(0);
    
    // 注册引导页目录为活动页目录（保护它不被覆盖）
    register_page_directory(current_dir_phys);
    protect_directory_range(current_dir, KERNEL_PDE_START, KERNEL_PDE_END);
    
    LOG_INFO_MSG("VMM: High-half kernel mapping extended\n");
    LOG_INFO_MSG("VMM: Boot page directory registered at phys 0x%x\n", current_dir_phys);
}


// 声明引导页目录（作为内核主页目录参考）
extern uint32_t boot_page_directory[];

/**
 * @brief 处理内核空间缺页异常（同步内核页目录）
 * @param addr 缺页地址
 * @return 是否成功处理
 */
bool vmm_handle_kernel_page_fault(uint32_t addr) {
    // 必须是内核空间地址
    if (addr < KERNEL_VIRTUAL_BASE) return false;
    
    uint32_t pd_idx = addr >> 22;
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
bool vmm_handle_cow_page_fault(uint32_t addr, uint32_t error_code) {
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
    uint32_t table_phys = get_frame(current_dir->entries[pd_idx]);
    if (table_phys == 0 || table_phys >= 0x80000000) {
        LOG_ERROR_MSG("COW: Invalid page table address 0x%x at PDE %u\n", table_phys, pd_idx);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
    pte_t *pte = &table->entries[pt_idx];
    
    // 检查页面是否存在且标记为 COW
    if (!is_present(*pte) || !(*pte & PAGE_COW)) {
        LOG_DEBUG_MSG("COW: Not a COW page - addr=0x%x, pte=0x%x, present=%d, cow=%d\n",
                     addr, *pte, is_present(*pte), (*pte & PAGE_COW) != 0);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;  // 不是 COW 页面
    }
    
    uint32_t old_frame = get_frame(*pte);
    
    // 【安全检查】验证旧物理帧地址
    if (old_frame == 0 || old_frame >= 0x80000000) {
        LOG_ERROR_MSG("COW: Invalid old frame address 0x%x at addr=0x%x\n", old_frame, addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    
    uint32_t refcount = pmm_frame_get_refcount(old_frame);
    
    LOG_INFO_MSG("COW: Handling page fault - addr=0x%x, old_frame=0x%x, refcount=%u\n", 
                addr, old_frame, refcount);
    
    if (refcount == 0) {
        // 异常情况：COW 页面但引用计数为 0
        // 这不应该发生，但为了安全，我们恢复写权限并继续
        LOG_WARN_MSG("COW: Page at 0x%x has refcount=0 but is marked COW, restoring write\n", addr);
        *pte = old_frame | ((*pte & 0xFFF) & ~PAGE_COW) | PAGE_WRITE;
        vmm_flush_tlb(addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return true;
    }
    
    if (refcount == 1) {
        // 只有当前进程引用，直接恢复写权限，无需复制
        *pte = old_frame | ((*pte & 0xFFF) & ~PAGE_COW) | PAGE_WRITE;
        vmm_flush_tlb(addr);
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        LOG_DEBUG_MSG("COW: Single reference (refcount=1), restored write permission\n");
        return true;
    }
    
    // 多个进程共享（refcount > 1），需要复制页面
    uint32_t new_frame = pmm_alloc_frame();
    if (!new_frame) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        LOG_ERROR_MSG("COW: Failed to allocate frame for COW copy (out of memory)\n");
        return false;
    }
    
    // 复制页面内容
    void *src_virt = (void*)PHYS_TO_VIRT(old_frame);
    void *dst_virt = (void*)PHYS_TO_VIRT(new_frame);
    memcpy(dst_virt, src_virt, PAGE_SIZE);
    
    // 更新页表项：指向新页面，恢复写权限，去掉 COW 标记
    uint32_t old_flags = *pte & 0xFFF;
    uint32_t new_flags = (old_flags & ~PAGE_COW) | PAGE_WRITE;
    *pte = new_frame | new_flags;
    
    // 刷新 TLB
    vmm_flush_tlb(addr);
    
    // 减少旧页面的引用计数
    // 注意：这里不调用 pmm_free_frame，因为我们不想释放帧
    // 我们只是减少引用计数，让其他进程继续共享
    pmm_frame_ref_dec(old_frame);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    
    LOG_DEBUG_MSG("COW: Copied page (refcount was %u), old_frame=0x%x -> new_frame=0x%x\n", 
                 refcount, old_frame, new_frame);
    return true;
}

/**
 * @brief 映射虚拟页到物理页
 * @param virt 虚拟地址（页对齐）
 * @param phys 物理地址（页对齐）
 * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
 * @return 成功返回 true，失败返回 false
 */
bool vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    uint32_t pd = pde_idx(virt);  // 页目录索引
    uint32_t pt = pte_idx(virt);  // 页表索引
    
    pde_t *pde = &current_dir->entries[pd];
    page_table_t *table;
    
    // 如果页表不存在，创建新页表
    if (!is_present(*pde)) {
        table = create_page_table();
        if (!table) {
            spinlock_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        uint32_t table_phys = VIRT_TO_PHYS((uint32_t)table);
        
        // 创建页表时，权限应该根据目标虚拟地址决定
        // 如果是内核空间（>= 0x80000000），则不应设置 PAGE_USER
        // 否则（用户空间），通常需要 PAGE_USER 允许用户进程访问其内容
        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITE;
        if (virt < KERNEL_VIRTUAL_BASE) {
            pde_flags |= PAGE_USER;
        }
        
        uint32_t new_pde = table_phys | pde_flags;
        *pde = new_pde;
        protect_phys_frame(table_phys);
        
        // 关键修复：如果是内核空间的新页表，必须同步到主内核页目录 (boot_page_directory)
        // 这样其他进程可以通过 page fault handler 同步这个新映射
        if (virt >= KERNEL_VIRTUAL_BASE) {
            page_directory_t *k_dir = (page_directory_t *)boot_page_directory;
            k_dir->entries[pd] = new_pde;
            if (k_dir != current_dir) {
                protect_phys_frame(table_phys);
            }
        }
    } else {
        table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    }
    
    // 设置页表项
    table->entries[pt] = phys | flags;
    vmm_flush_tlb(virt);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return true;
}

/**
 * @brief 取消虚拟页映射
 * @param virt 虚拟地址（页对齐）
 */
void vmm_unmap_page(uint32_t virt) {
    // 检查页对齐
    if (virt & (PAGE_SIZE-1)) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    pde_t *pde = &current_dir->entries[pde_idx(virt)];
    // 如果页表不存在，直接返回
    if (!is_present(*pde)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return;
    }
    
    // 清除页表项
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    table->entries[pte_idx(virt)] = 0;
    vmm_flush_tlb(virt);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 刷新TLB缓存
 * @param virt 虚拟地址（0表示刷新全部TLB）
 * 
 * 当修改页表后需要刷新TLB以确保CPU使用最新的页表项
 */
void vmm_flush_tlb(uint32_t virt) {
    if (virt == 0) {
        // 刷新整个TLB：重新加载CR3
        __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
    } else {
        // 刷新单个页：使用invlpg指令
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

/**
 * @brief 获取当前页目录的物理地址
 * @return 页目录的物理地址
 */
uint32_t vmm_get_page_directory(void) {
    return current_dir_phys;
}

/**
 * @brief 创建新的页目录（用于新进程）
 * @return 成功返回页目录的物理地址，失败返回 0
 */
uint32_t vmm_create_page_directory(void) {
    // 分配页目录的物理页
    uint32_t dir_phys = pmm_alloc_frame();
    if (!dir_phys) return 0;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 获取虚拟地址访问
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 清空整个页目录
    memset(new_dir, 0, sizeof(page_directory_t));
    
    // 复制内核空间映射（512-1023，即 0x80000000-0xFFFFFFFF）
    // 关键修改：从主内核页目录 (boot_page_directory) 复制，而不是从当前页目录复制
    // 这确保新进程总是获得最完整、最新的内核映射
    page_directory_t *master_dir = (page_directory_t *)boot_page_directory;
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = master_dir->entries[i];
    }
    protect_directory_range(new_dir, KERNEL_PDE_START, KERNEL_PDE_END);
    
    // 注册为活动页目录
    register_page_directory(dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return dir_phys;
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
 */
uint32_t vmm_clone_page_directory(uint32_t src_dir_phys) {
    // 【安全检查】验证源页目录地址有效
    if (!src_dir_phys || src_dir_phys >= 0x80000000) {
        LOG_ERROR_MSG("vmm_clone_page_directory: Invalid src_dir_phys 0x%x\n", src_dir_phys);
        return 0;
    }
    
    // 分配新页目录
    uint32_t new_dir_phys = pmm_alloc_frame();
    if (!new_dir_phys) return 0;
    
    // 【安全检查】确保新分配的帧不与源相同
    if (new_dir_phys == src_dir_phys) {
        LOG_ERROR_MSG("vmm_clone_page_directory: CRITICAL! PMM returned same frame as source 0x%x!\n", src_dir_phys);
        pmm_free_frame(new_dir_phys);
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *src_dir = (page_directory_t*)PHYS_TO_VIRT(src_dir_phys);
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(new_dir_phys);
    
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
    LOG_DEBUG_MSG("vmm_clone_cow: Cloning user space with COW (src=0x%x, new=0x%x)\n", 
                 src_dir_phys, new_dir_phys);
    
    // 用于跟踪失败时的回滚信息
    uint32_t last_successful_pde = 0;
    bool clone_failed = false;
    
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(src_dir->entries[i])) {
            // 【安全检查】验证源页表的物理地址有效性
            uint32_t src_table_phys = get_frame(src_dir->entries[i]);
            if (src_table_phys >= 0x80000000 || src_table_phys == 0) {
                LOG_ERROR_MSG("vmm_clone_cow: Invalid src_table_phys 0x%x at PDE %u\n",
                             src_table_phys, i);
                clone_failed = true;
                break;
            }
            
            page_table_t *src_table = (page_table_t*)PHYS_TO_VIRT(src_table_phys);
            
            // COW策略关键修复：为子进程创建新的页表副本，而不是共享页表
            uint32_t new_table_phys = pmm_alloc_frame();
            if (!new_table_phys) {
                LOG_ERROR_MSG("vmm_clone_cow: Failed to allocate page table for PDE %u\n", i);
                clone_failed = true;
                break;
            }
            
            page_table_t *new_table = (page_table_t*)PHYS_TO_VIRT(new_table_phys);
            
            // 遍历源页表中的每个页面，设置 COW 标记并复制到新页表
            uint32_t cow_pages = 0;
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(src_table->entries[j])) {
                    uint32_t src_frame = get_frame(src_table->entries[j]);
                    uint32_t flags = src_table->entries[j] & 0xFFF;
                    
                    // 如果页面可写，将其改为只读并标记为 COW
                    if (flags & PAGE_WRITE) {
                        flags &= ~PAGE_WRITE;  // 去掉写权限
                        flags |= PAGE_COW;     // 标记为 COW
                        src_table->entries[j] = src_frame | flags;
                        cow_pages++;
                        
                        // 调试：记录第一个 COW 页面的详细信息
                        if (cow_pages == 1) {
                            LOG_DEBUG_MSG("vmm_clone_cow: First COW page: PDE=%u PTE=%u frame=0x%x flags=0x%x\n",
                                         i, j, src_frame, flags);
                            LOG_DEBUG_MSG("vmm_clone_cow: src_table_phys=0x%x new_table_phys=0x%x\n",
                                         src_table_phys, new_table_phys);
                        }
                    }
                    
                    // 复制页表项到新页表（指向相同的物理页）
                    new_table->entries[j] = src_frame | flags;
                    
                    // 增加物理页的引用计数（父子进程共享物理页）
                    pmm_frame_ref_inc(src_frame);
                } else {
                    // 空页表项
                    new_table->entries[j] = 0;
                }
            }
            
            LOG_DEBUG_MSG("vmm_clone_cow: PDE %u cloned, %u pages marked COW (src_table=0x%x, new_table=0x%x)\n", 
                         i, cow_pages, src_table_phys, new_table_phys);
            
            // 子进程使用新的页表
            uint32_t pde_flags = src_dir->entries[i] & 0xFFF;
            new_dir->entries[i] = new_table_phys | pde_flags;
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
    register_page_directory(new_dir_phys);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return new_dir_phys;
}

/**
 * @brief 检查页目录是否被任何任务使用
 * @param dir_phys 页目录的物理地址
 * @return 如果被使用返回 true，否则返回 false
 */
static bool is_page_directory_in_use(uint32_t dir_phys) {
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

/**
 * @brief 释放页目录及其用户空间页表和物理页
 * @param dir_phys 页目录的物理地址
 * 
 * 注意：由于 fork 现在使用深拷贝，所以需要释放所有物理页
 */
void vmm_free_page_directory(uint32_t dir_phys) {
    if (!dir_phys) return;
    
    LOG_INFO_MSG("vmm_free_page_directory: Attempting to free page directory 0x%x\n", dir_phys);
    
    // 【安全检查】防止释放当前正在使用的页目录
    if (dir_phys == current_dir_phys) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Attempting to free current page directory 0x%x!\n", dir_phys);
        return;
    }
    
    // 【安全检查】防止释放主内核页目录
    uint32_t boot_dir_phys = VIRT_TO_PHYS((uint32_t)boot_page_directory);
    if (dir_phys == boot_dir_phys) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Attempting to free boot page directory 0x%x!\n", dir_phys);
        return;
    }
    
    // 【关键修复】检查页目录是否仍被其他任务使用
    if (is_page_directory_in_use(dir_phys)) {
        LOG_ERROR_MSG("vmm_free_page_directory: BLOCKED! Page directory 0x%x is still in use by a task!\n", dir_phys);
        return;
    }
    
    // 【新增检查】验证这个页目录是否在活动列表中
    if (!is_active_page_directory(dir_phys)) {
        LOG_ERROR_MSG("vmm_free_page_directory: WARNING! Page directory 0x%x is not in active list!\n", dir_phys);
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
            uint32_t table_phys = get_frame(dir->entries[i]);
            if (table_phys == 0) {
                LOG_ERROR_MSG("vmm_free_page_directory: PDE %u has zero frame, skipping\n", i);
                dir->entries[i] = 0;
                continue;
            }
            
            page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
            uint32_t pages_in_table = 0;
            
            // 释放页表中的所有物理页（pmm_free_frame 自动处理引用计数）
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(table->entries[j])) {
                    uint32_t frame = get_frame(table->entries[j]);
                    
                    // 基本安全检查
                    if (frame == 0 || frame >= 0x80000000) {
                        LOG_WARN_MSG("vmm_free: PDE %u PTE %u invalid frame 0x%x\n", i, j, frame);
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
        LOG_WARN_MSG("vmm_free_page_directory: dir 0x%x was not registered\n", dir_phys);
    }
    
    // 释放页目录本身
    LOG_DEBUG_MSG("vmm_free_page_directory: freeing page directory at phys 0x%x (virt 0x%x)\n", 
                  dir_phys, (uint32_t)dir);
    pmm_free_frame(dir_phys);
    
    pmm_info_t info_end = pmm_get_info();
    LOG_INFO_MSG("vmm_free_page_directory: freed %u pages (PMM: %u -> %u, diff %d), %u tables, 1 directory\n", 
                  freed_pages, info_start.used_frames, info_end.used_frames, 
                  (int)info_start.used_frames - (int)info_end.used_frames, freed_tables);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 同步 VMM 的 current_dir_phys（不切换 CR3）
 * @param dir_phys 当前页目录的物理地址
 * 
 * 仅更新内部状态变量，不修改 CR3 寄存器
 * 用于在 task_switch_context 已经切换 CR3 后同步状态
 */
void vmm_sync_current_dir(uint32_t dir_phys) {
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
 */
void vmm_switch_page_directory(uint32_t dir_phys) {
    if (!dir_phys) return;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 加载到 CR3 寄存器
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir_phys) : "memory");
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 在指定页目录中映射页面
 * @param dir_phys 页目录的物理地址
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 * @return 成功返回 true，失败返回 false
 */
bool vmm_map_page_in_directory(uint32_t dir_phys, uint32_t virt, 
                                uint32_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    uint32_t pd = pde_idx(virt);
    uint32_t pt = pte_idx(virt);
    
    pde_t *pde = &dir->entries[pd];
    page_table_t *table;
    
    // 如果页表不存在，创建新页表
    if (!is_present(*pde)) {
        uint32_t table_phys = pmm_alloc_frame();
        if (!table_phys) {
            spinlock_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        
        table = (page_table_t*)PHYS_TO_VIRT(table_phys);
        memset(table, 0, sizeof(page_table_t));
        
        // 权限逻辑同上：如果是内核空间地址，不加 PAGE_USER
        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITE;
        if (virt < KERNEL_VIRTUAL_BASE) {
            pde_flags |= PAGE_USER;
        }

        *pde = table_phys | pde_flags;
        protect_phys_frame(table_phys);
    } else {
        table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    }
    
    // 设置页表项
    table->entries[pt] = phys | flags;
    
    // 如果是当前页目录，刷新 TLB
    if (dir_phys == current_dir_phys) {
        vmm_flush_tlb(virt);
    }
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return true;
}

uint32_t vmm_unmap_page_in_directory(uint32_t dir_phys, uint32_t virt) {
    if (virt & (PAGE_SIZE - 1)) {
        return 0;
    }

    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);

    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    uint32_t pd = pde_idx(virt);
    uint32_t pt = pte_idx(virt);

    pde_t *pde = &dir->entries[pd];
    if (!is_present(*pde)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }

    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    uint32_t old_entry = table->entries[pt];
    if (!is_present(old_entry)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }

    table->entries[pt] = 0;

    // 检查页表是否为空（所有条目都为0）
    bool empty = true;
    for (uint32_t i = 0; i < 1024; i++) {
        if (is_present(table->entries[i])) {
            empty = false;
            break;
        }
    }

    if (empty) {
        uint32_t table_frame = get_frame(*pde);
        if (table_frame == 0) {
            LOG_ERROR_MSG("vmm_unmap_page_in_directory: PDE %u has zero frame, skipping free\n", pd);
        } else {
            unprotect_phys_frame(table_frame);
            pmm_free_frame(table_frame);
        }
        *pde = 0;
        LOG_DEBUG_MSG("  Page table at PDE %u (frame=0x%x) is empty, freed\n", pd, table_frame);
        
        // 如果页表被释放，需要刷新整个 TLB，因为 PDE 变了
        if (dir_phys == current_dir_phys) {
            vmm_flush_tlb(0);
        }
    } else {
        LOG_DEBUG_MSG("  Page table at PDE %u still has entries\n", pd);
        // 只有当页表没有被释放时，才只需要刷新单个页面的 TLB
        if (dir_phys == current_dir_phys) {
            vmm_flush_tlb(virt);
        }
    }

    uint32_t result = get_frame(old_entry);
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

/* ============================================================================
 * MMIO 映射
 * ============================================================================ */

/** MMIO 映射区域起始地址（在内核空间的高地址区域） */
#define MMIO_VIRT_BASE      0xF0000000
#define MMIO_VIRT_END       0xFFC00000  // 保留最后 4MB 给递归页表等
#define PAGE_CACHE_DISABLE  0x010       // PCD: Page Cache Disable
#define PAGE_WRITE_THROUGH  0x008       // PWT: Page Write Through

/** 当前 MMIO 虚拟地址分配位置 */
static uint32_t mmio_next_virt = MMIO_VIRT_BASE;

/**
 * @brief 映射 MMIO 区域
 * @param phys_addr 物理地址
 * @param size 映射大小（字节）
 * @return 成功返回映射的虚拟地址，失败返回 0
 */
uint32_t vmm_map_mmio(uint32_t phys_addr, uint32_t size) {
    if (size == 0) {
        return 0;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    // 计算需要的页数（向上取整）
    uint32_t phys_start = PAGE_ALIGN_DOWN(phys_addr);
    uint32_t phys_end = PAGE_ALIGN_UP(phys_addr + size);
    uint32_t num_pages = (phys_end - phys_start) / PAGE_SIZE;
    
    // 分配虚拟地址空间
    uint32_t virt_start = mmio_next_virt;
    if (virt_start + num_pages * PAGE_SIZE > MMIO_VIRT_END) {
        LOG_ERROR_MSG("vmm_map_mmio: No more MMIO virtual address space\n");
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
    // 映射所有页面（禁用缓存）
    uint32_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t virt = virt_start + i * PAGE_SIZE;
        uint32_t phys = phys_start + i * PAGE_SIZE;
        
        uint32_t pd = pde_idx(virt);
        uint32_t pt = pte_idx(virt);
        
        pde_t *pde = &current_dir->entries[pd];
        page_table_t *table;
        
        // 如果页表不存在，创建新页表
        if (!is_present(*pde)) {
            table = create_page_table();
            if (!table) {
                // 回滚已映射的页面
                for (uint32_t j = 0; j < i; j++) {
                    vmm_unmap_page(virt_start + j * PAGE_SIZE);
                }
                spinlock_unlock_irqrestore(&vmm_lock, irq_state);
                return 0;
            }
            uint32_t table_phys = VIRT_TO_PHYS((uint32_t)table);
            *pde = table_phys | PAGE_PRESENT | PAGE_WRITE;
            protect_phys_frame(table_phys);
            
            // 同步到主内核页目录
            page_directory_t *k_dir = (page_directory_t *)boot_page_directory;
            k_dir->entries[pd] = *pde;
        } else {
            table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
        }
        
        // 设置页表项
        table->entries[pt] = phys | flags;
        vmm_flush_tlb(virt);
    }
    
    // 更新下一个可用的 MMIO 虚拟地址
    mmio_next_virt = virt_start + num_pages * PAGE_SIZE;
    
    // 计算返回地址：虚拟基址 + 物理地址页内偏移
    uint32_t offset = phys_addr - phys_start;
    uint32_t result = virt_start + offset;
    
    LOG_INFO_MSG("vmm_map_mmio: mapped phys 0x%x size 0x%x -> virt 0x%x\n", 
                 phys_addr, size, result);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

/**
 * @brief 取消 MMIO 区域映射
 * @param virt_addr 虚拟地址
 * @param size 映射大小（字节）
 */
void vmm_unmap_mmio(uint32_t virt_addr, uint32_t size) {
    if (size == 0 || virt_addr < MMIO_VIRT_BASE || virt_addr >= MMIO_VIRT_END) {
        return;
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    uint32_t virt_start = PAGE_ALIGN_DOWN(virt_addr);
    uint32_t virt_end = PAGE_ALIGN_UP(virt_addr + size);
    
    for (uint32_t virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
        uint32_t pd = pde_idx(virt);
        uint32_t pt = pte_idx(virt);
        
        pde_t *pde = &current_dir->entries[pd];
        if (!is_present(*pde)) {
            continue;
        }
        
        page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
        table->entries[pt] = 0;
        vmm_flush_tlb(virt);
    }
    
    LOG_INFO_MSG("vmm_unmap_mmio: unmapped virt 0x%x size 0x%x\n", virt_addr, size);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
}

/**
 * @brief 通过查询页表获取虚拟地址对应的物理地址
 * @param virt 虚拟地址
 * @return 物理地址，如果虚拟地址未映射则返回 0
 */
uint32_t vmm_virt_to_phys(uint32_t virt) {
    bool irq_state;
    spinlock_lock_irqsave(&vmm_lock, &irq_state);
    
    uint32_t pd = pde_idx(virt);
    uint32_t pt = pte_idx(virt);
    
    // 检查页目录项是否存在
    pde_t pde = current_dir->entries[pd];
    if (!is_present(pde)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
    // 获取页表
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(pde));
    
    // 检查页表项是否存在
    pte_t pte = table->entries[pt];
    if (!is_present(pte)) {
        spinlock_unlock_irqrestore(&vmm_lock, irq_state);
        return 0;
    }
    
    // 物理地址 = 页帧地址 + 页内偏移
    uint32_t phys = get_frame(pte) | (virt & 0xFFF);
    
    spinlock_unlock_irqrestore(&vmm_lock, irq_state);
    return phys;
}
