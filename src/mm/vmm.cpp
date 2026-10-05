/**
 * @file vmm.c
 * @brief 虚拟内存管理器实现
 * 
 * 实现分页机制，管理虚拟地址到物理地址的映射
 * 核心逻辑保持架构无关，通过 HAL 接口和 pgtable 抽象层调用架构特定操作
 */

#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/sync/spinlock.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <hal/pgtable.h>
#include <hal/hal_error.h>

static page_directory_t *current_dir = NULL;  ///< 当前页目录虚拟地址
static uintptr_t current_dir_phys = 0;         ///< 当前页目录物理地址

/* 引导时的页目录 - 使用 pgtable 抽象层获取条目大小 */
#if defined(ARCH_X86_64)
extern uint64_t boot_page_directory[];         ///< 引导时的 PML4 (x86_64)
#else
extern uint32_t boot_page_directory[];         ///< 引导时的页目录 (i686)
#endif

static sync::Spinlock vmm_lock;                    ///< VMM 自旋锁，保护页表操作


/* ============================================================================
 * 页表索引提取函数 - 使用 pgtable 抽象层
 * 
 * 这些函数封装了 pgtable 抽象层的索引提取功能，提供向后兼容的接口。
 * ========================================================================== */

#if defined(ARCH_X86_64)
/* x86_64: 4-level paging address decomposition using pgtable abstraction */
static inline uint32_t pml4_idx(uintptr_t v) { return pgtable_get_index((vaddr_t)v, 3); }
static inline uint32_t pdpt_idx(uintptr_t v) { return pgtable_get_index((vaddr_t)v, 2); }
static inline uint32_t pd_idx(uintptr_t v)   { return pgtable_get_index((vaddr_t)v, 1); }
static inline uint32_t pt_idx(uintptr_t v)   { return pgtable_get_index((vaddr_t)v, 0); }
/* 使用 pgtable 抽象层提取物理地址和检查存在位 */
static inline uintptr_t get_frame64(pte_t e) { return (uintptr_t)pgtable_get_phys(e); }
static inline bool is_present64(pte_t e) { return pgtable_is_present(e); }
/* Compatibility aliases for x86_64 */
#define pde_idx(v) pml4_idx(v)
#define pte_idx(v) pt_idx(v)
#define get_frame(e) get_frame64(e)
#define is_present(e) is_present64(e)
#else
/* i686: 2-level paging address decomposition using pgtable abstraction */
/**
 * @brief 获取页目录索引
 * @param v 虚拟地址
 * @return 页目录索引（高10位）
 */
static inline uint32_t pde_idx(uintptr_t v) { return pgtable_get_index((vaddr_t)v, 1); }

/**
 * @brief 获取页表索引
 * @param v 虚拟地址
 * @return 页表索引（中间10位）
 */
static inline uint32_t pte_idx(uintptr_t v) { return pgtable_get_index((vaddr_t)v, 0); }

/**
 * @brief 从页表项/页目录项中提取物理地址
 * @param e 页表项或页目录项
 * @return 物理地址（页对齐）
 * 
 * 使用 pgtable 抽象层提取物理地址
 */
static inline uintptr_t get_frame(pte_t e) { return (uintptr_t)pgtable_get_phys(e); }

/**
 * @brief 检查页表项/页目录项是否存在
 * @param e 页表项或页目录项
 * @return 存在返回 true，否则返回 false
 * 
 * 使用 pgtable 抽象层检查存在位
 */
static inline bool is_present(pte_t e) { return pgtable_is_present(e); }
#endif


/**
 * @brief 初始化虚拟内存管理器
 * 
 * 使用引导时创建的页目录，设置CR3寄存器
 * 扩展高半核映射以覆盖所有可用的物理内存
 */
void mm::Vmm::init() {
    // 初始化 VMM 自旋锁
    vmm_lock.init();
    
#if defined(ARCH_ARM64)
    // ARM64: 引导代码已经设置了 4 级页表
    // 使用 HAL MMU 接口获取当前页表
    current_dir_phys = hal::Mmu::get_current_page_table();
    current_dir = (page_directory_t*)PADDR_TO_KVADDR(current_dir_phys);
    
    LOG_INFO_MSG("VMM: ARM64 mode - using boot page tables\n");
    LOG_INFO_MSG("VMM: L0 table at phys 0x%llx, virt 0x%llx\n", 
                 (unsigned long long)current_dir_phys, (unsigned long long)current_dir);
    
    // 获取 PMM 信息以确定需要映射的物理内存范围
    mm::PmmInfo pmm_info = mm::Pmm::get_info();
    uint64_t max_phys = (uint64_t)pmm_info.total_frames * PAGE_SIZE;
    
    LOG_INFO_MSG("VMM: Physical memory: %llu MB (%llu frames)\n", 
                 (unsigned long long)(max_phys / (1024*1024)),
                 (unsigned long long)pmm_info.total_frames);
    
    // ARM64 内核直接映射区：0xFFFF_0000_0000_0000 开始
    // 引导代码已经映射了基本的内核区域，这里扩展映射以覆盖所有物理内存
    // 使用 2MB 块映射提高效率
    LOG_INFO_MSG("VMM: Extending kernel direct mapping using 2MB blocks...\n");
    
    // 计算需要映射的 2MB 块数量
    uint64_t block_size = 2 * 1024 * 1024;  // 2MB
    uint64_t num_blocks = (max_phys + block_size - 1) / block_size;
    uint32_t mapped_blocks = 0;
    
    for (uint64_t i = 0; i < num_blocks; i++) {
        uint64_t phys = i * block_size;
        vaddr_t virt = (vaddr_t)(KERNEL_VIRTUAL_BASE + phys);
        
        // 检查是否已经映射（引导代码可能已经映射了部分区域）
        paddr_t existing_phys;
        if (hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, virt, &existing_phys, NULL)) {
            // 已映射，跳过
            continue;
        }
        
        // 使用 2MB 块映射
        uint32_t hal_flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_EXEC;
        if (hal::Mmu::map_huge(HAL_ADDR_SPACE_CURRENT, virt, (paddr_t)phys, hal_flags)) {
            mapped_blocks++;
        } else {
            // 如果 2MB 块映射失败，尝试使用 4KB 页映射
            LOG_WARN_MSG("VMM: 2MB block mapping failed at 0x%llx, falling back to 4KB pages\n",
                        (unsigned long long)virt);
            for (uint64_t offset = 0; offset < block_size; offset += PAGE_SIZE) {
                vaddr_t page_virt = virt + offset;
                paddr_t page_phys = phys + offset;
                if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, page_virt, NULL, NULL)) {
                    hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, page_virt, page_phys, hal_flags);
                }
            }
        }
    }
    
    // 刷新 TLB
    hal::Mmu::flush_tlb_all();
    
    LOG_INFO_MSG("VMM: Extended mapping by %u 2MB blocks (total %llu MB)\n", 
                 mapped_blocks, (unsigned long long)(num_blocks * 2));
    LOG_INFO_MSG("VMM: ARM64 VMM initialization complete\n");
    
#elif defined(ARCH_X86_64)
    // x86_64: 引导代码已经设置了 4 级页表，映射了前 1GB
    // 暂时不扩展映射，直接使用引导时的页表
    // boot_page_directory 在 x86_64 上是指向 PML4 的指针
    current_dir_phys = hal::Mmu::get_current_page_table();
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
    uintptr_t current_page_table = hal::Mmu::get_current_page_table();
    if (current_page_table != current_dir_phys)
        hal::Mmu::switch_space(current_dir_phys);
    
    // 扩展高半核映射以覆盖所有可用的物理内存
    // 引导时已经映射了前8MB（页目录项512-513）
    // 现在需要扩展到所有可用内存（最多2GB）
    mm::PmmInfo pmm_info = mm::Pmm::get_info();
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
    // 注意：每映射完一个 PDE 后立即刷新 TLB，这样后续的 mm::Pmm::alloc_frame 
    // 可以使用新映射的内存区域（因为 mm::Pmm::alloc_frame 会清零新分配的帧）
    uint32_t mapped_pdes = 0;
    for (uint32_t pde = start_pde; pde < end_pde; pde++) {
        // 检查页目录项是否已存在
        if (is_present(current_dir->entries[pde])) {
            continue;  // 已经映射，跳过
        }
        
        // 分配页表
        // 注意：mm::Pmm::alloc_frame 会清零新分配的帧，需要确保帧在已映射范围内
        paddr_t table_phys = mm::Pmm::alloc_frame();
        if (table_phys == PADDR_INVALID) {
            LOG_WARN_MSG("VMM: Failed to allocate page table for PDE %u\n", pde);
            break;  // 分配失败，停止扩展
        }
        
        // 安全检查：确保页表帧在已映射范围内（引导时映射了前 16MB）
        // 如果帧超出范围，mm::Pmm::alloc_frame 内部的 memset 就会失败
        // 但由于 PMM 优先分配低地址帧，这种情况不应该发生
        if (table_phys >= 0x1000000) {  // >= 16MB
            LOG_ERROR_MSG("VMM: Page table frame 0x%llx exceeds boot mapping! This is a bug.\n", (unsigned long long)table_phys);
            mm::Pmm::free_frame(table_phys);
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
        // 这样下一次 mm::Pmm::alloc_frame 就可以安全地访问更高地址的内存了
        mm::Vmm::flush_tlb(0);
        mapped_pdes++;
    }
    
    LOG_INFO_MSG("VMM: Extended mapping by %u PDEs (now covers 0-%u MB)\n", 
                 mapped_pdes, ((end_pde - 512) * 4));
    
    LOG_INFO_MSG("VMM: High-half kernel mapping extended\n");
    LOG_INFO_MSG("VMM: Boot page directory registered at phys 0x%x\n", current_dir_phys);
#endif
}


/**
 * @brief 处理内核空间缺页异常（同步内核页目录）
 * @param addr 缺页地址
 * @return 是否成功处理
 */
bool mm::Vmm::handle_kernel_page_fault(uintptr_t addr) {
#if !defined(ARCH_I686)
    // x86_64 / arm64: 内核空间由所有地址空间共享同一组页表，不需要同步
    // （下面的同步逻辑按 i686 的两级页目录格式访问 boot_page_directory）
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
        // 当前页目录已经有同样的 PDE：缺的是页表项而不是页目录项，
        // 同步解决不了，交给调用者按真正的缺页处理（否则会无限重试）
        if (current_dir->entries[pd_idx] == k_dir->entries[pd_idx]) {
            return false;
        }
        // 将条目复制到当前页目录
        current_dir->entries[pd_idx] = k_dir->entries[pd_idx];
        
        // 刷新 TLB，确保 CPU 看到新的映射
        // 虽然 Intel 手册说修改 PDE 后需要刷新 TLB，但有些实现可能缓存了 PDE
        // 对于缺页处理，invlpg 通常足够，但这里我们修改了 PDE，安全起见可以刷新整个 TLB
        // 不过针对特定地址的 invlpg 应该也足以让 CPU 重新遍历页表结构
        mm::Vmm::flush_tlb(addr);
        
        return true;
    }
    
    return false;
#endif
}

/**
 * @brief 处理写保护异常（COW）- 统一实现
 * @param addr 缺页地址
 * @param error_code 错误码
 * @return 是否成功处理
 * 
 * 使用 HAL 接口实现架构无关的 COW 处理。
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
bool mm::Vmm::handle_cow_page_fault(uintptr_t addr, uint32_t error_code) {
    /* 统一的 COW 处理逻辑，使用 HAL 接口实现架构无关 */
    
    // error_code bit 1: 写入导致的异常
    // error_code bit 0: 页面存在
    // COW 异常应该是：页面存在(bit0=1) + 写入(bit1=1) = 0x3 或 0x7
    if ((error_code & 0x3) != 0x3) {
        LOG_DEBUG_MSG("COW: Not a COW fault - addr=0x%lx, error=0x%x\n", 
                     (unsigned long)addr, error_code);
        return false;  // 不是写保护异常
    }
    
    bool irq_state;
    vmm_lock.lock_irqsave(irq_state);
    
    // 使用 HAL 接口查询页面映射
    paddr_t old_frame;
    uint32_t hal_flags;
    if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, &old_frame, &hal_flags)) {
        LOG_DEBUG_MSG("COW: Page not mapped - addr=0x%lx\n", (unsigned long)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        return false;
    }
    
    // 检查页面是否标记为 COW
    if (!(hal_flags & HAL_PAGE_COW)) {
        LOG_DEBUG_MSG("COW: Not a COW page - addr=0x%lx, flags=0x%x\n",
                     (unsigned long)addr, hal_flags);
        vmm_lock.unlock_irqrestore(irq_state);
        return false;
    }
    
    // 【安全检查】验证旧物理帧地址
    if (old_frame == PADDR_INVALID || old_frame == 0) {
        LOG_ERROR_MSG("COW: Invalid old frame address 0x%llx at addr=0x%lx\n", 
                     (unsigned long long)old_frame, (unsigned long)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        return false;
    }
    
    uint32_t refcount = mm::Pmm::frame_get_refcount(old_frame);
    
    LOG_DEBUG_MSG("COW: Handling page fault - addr=0x%lx, old_frame=0x%llx, refcount=%u\n", 
                (unsigned long)addr, (unsigned long long)old_frame, refcount);
    
    if (refcount == 0) {
        // 异常情况：COW 页面但引用计数为 0
        // 这不应该发生，但为了安全，我们恢复写权限并继续
        LOG_WARN_MSG("COW: Page at 0x%lx has refcount=0 but is marked COW, restoring write\n", 
                    (unsigned long)addr);
        hal::Mmu::protect(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, 
                       HAL_PAGE_WRITE, HAL_PAGE_COW);
        hal::Mmu::flush_tlb((vaddr_t)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        return true;
    }
    
    if (refcount == 1) {
        // 只有当前进程引用，直接恢复写权限，无需复制
        hal::Mmu::protect(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, 
                       HAL_PAGE_WRITE, HAL_PAGE_COW);
        hal::Mmu::flush_tlb((vaddr_t)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        LOG_DEBUG_MSG("COW: Single reference (refcount=1), restored write permission\n");
        return true;
    }
    
    // 多个进程共享（refcount > 1），需要复制页面
    paddr_t new_frame = mm::Pmm::alloc_frame();
    if (new_frame == PADDR_INVALID) {
        vmm_lock.unlock_irqrestore(irq_state);
        LOG_ERROR_MSG("COW: Failed to allocate frame for COW copy (out of memory)\n");
        return false;
    }
    
    // 复制页面内容
    void *src_virt = (void*)PHYS_TO_VIRT((uintptr_t)old_frame);
    void *dst_virt = (void*)PHYS_TO_VIRT((uintptr_t)new_frame);
    memcpy(dst_virt, src_virt, PAGE_SIZE);
    
    // 计算新的标志：去掉 COW，恢复写权限
    uint32_t new_flags = (hal_flags & ~HAL_PAGE_COW) | HAL_PAGE_WRITE;
    
    // 页对齐地址
    vaddr_t page_addr = (vaddr_t)(addr & ~(PAGE_SIZE - 1));
    
    // 取消旧映射并创建新映射
    hal::Mmu::unmap(HAL_ADDR_SPACE_CURRENT, page_addr);
    hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, page_addr, new_frame, new_flags);
    
    // 刷新 TLB
    hal::Mmu::flush_tlb((vaddr_t)addr);
    
    // 减少旧页面的引用计数
    mm::Pmm::frame_ref_dec(old_frame);
    
    vmm_lock.unlock_irqrestore(irq_state);
    
    LOG_DEBUG_MSG("COW: Copied page (refcount was %u), old_frame=0x%llx -> new_frame=0x%llx\n", 
                 refcount, (unsigned long long)old_frame, (unsigned long long)new_frame);
    return true;
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
    if (vmm_flags & PAGE_SHARED)        hal_flags |= HAL_PAGE_SHARED;
    if (vmm_flags & PAGE_EXEC)          hal_flags |= HAL_PAGE_EXEC;
    
    return hal_flags;
}

/**
 * @brief 用户可访问的映射只能建在用户地址范围内
 *
 * 内核半区的页表由所有地址空间共享，在那里建立带 PAGE_USER 的映射会改写
 * 每个进程的内核映射并把它暴露给用户态。
 */
static bool user_mapping_allowed(uintptr_t virt, uint32_t flags) {
    if ((flags & PAGE_USER) && virt >= VMM_USER_VADDR_END) {
        LOG_ERROR_MSG("VMM: refusing user mapping at kernel address 0x%llx\n",
                      (unsigned long long)virt);
        return false;
    }
    return true;
}

/* ============================================================================
 * 错误码转换函数实现
 * 
 * 提供 HAL 错误码与 VMM 错误码之间的转换，确保错误处理一致性。
 * 
 * ========================================================================== */

/**
 * @brief 映射虚拟页到物理页
 * @param virt 虚拟地址（页对齐）
 * @param phys 物理地址（页对齐）
 * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
 * @return 成功返回 true，失败返回 false
 * 
 * 使用 HAL MMU 接口实现跨架构页面映射
 */
bool mm::Vmm::map_page(uintptr_t virt, uintptr_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    if (!user_mapping_allowed(virt, flags)) return false;
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    // 转换为 HAL 标志
    uint32_t hal_flags = vmm_flags_to_hal(flags);
    
    // 使用 HAL 接口映射页面
    bool result = hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, (paddr_t)phys, hal_flags);
    
    if (result) {
        // 刷新 TLB
        hal::Mmu::flush_tlb((vaddr_t)virt);
        
#if defined(ARCH_I686)
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
    
    return result;
}

/**
 * @brief 刷新TLB缓存
 * @param virt 虚拟地址（0表示刷新全部TLB）
 * 
 * 当修改页表后需要刷新TLB以确保CPU使用最新的页表项
 * 通过 HAL 接口调用架构特定的 TLB 刷新操作
 */
void mm::Vmm::flush_tlb(uintptr_t virt) {
    if (virt == 0) {
        // 刷新整个TLB
        hal::Mmu::flush_tlb_all();
    } else {
        // 刷新单个页
        hal::Mmu::flush_tlb(virt);
    }
}

/**
 * @brief 获取当前页目录的物理地址
 * @return 页目录的物理地址
 */
uintptr_t mm::Vmm::get_page_directory() {
    return current_dir_phys;
}

/**
 * @brief 创建新的页目录（用于新进程）
 * @return 成功返回页目录的物理地址，失败返回 0
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间创建
 */
uintptr_t mm::Vmm::create_page_directory() {
    bool irq_state;
    vmm_lock.lock_irqsave(irq_state);
    
    // 使用 HAL 接口创建新地址空间
    hal_addr_space_t new_space = hal::Mmu::create_space();
    
    if (new_space == HAL_ADDR_SPACE_INVALID) {
        vmm_lock.unlock_irqrestore(irq_state);
        return 0;
    }
    
    vmm_lock.unlock_irqrestore(irq_state);
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
 * - 首次写入时触发 page fault，由 mm::Vmm::handle_cow_page_fault 处理
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间克隆
 */
uintptr_t mm::Vmm::clone_page_directory(uintptr_t src_dir_phys) {
    hal_addr_space_t new_space;
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        hal_addr_space_t src_space = (src_dir_phys == 0) 
                                     ? HAL_ADDR_SPACE_CURRENT 
                                     : (hal_addr_space_t)src_dir_phys;
    
        new_space = hal::Mmu::clone_space(src_space);
    }
    
    if (new_space == HAL_ADDR_SPACE_INVALID) {
        return 0;
    }
    
    return (uintptr_t)new_space;
}


/**
 * @brief 释放页目录及其用户空间页表和物理页
 * @param dir_phys 页目录的物理地址
 * 
 * 物理页由引用计数管理：和别的地址空间共享的页只减计数
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间销毁
 */
void mm::Vmm::free_page_directory(uintptr_t dir_phys) {
    if (!dir_phys) return;

    // 【安全检查】防止释放当前正在使用的页目录
    if (dir_phys == current_dir_phys) {
        LOG_ERROR_MSG("mm::Vmm::free_page_directory: BLOCKED! Attempting to free current page directory 0x%llx!\n", 
                     (unsigned long long)dir_phys);
        return;
    }
    
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        hal::Mmu::destroy_space((hal_addr_space_t)dir_phys);
    }
}

/**
 * @brief 同步 VMM 的 current_dir_phys（不切换 CR3）
 * @param dir_phys 当前页目录的物理地址
 * 
 * 仅更新内部状态变量，不修改 CR3 寄存器
 * 用于在 task_switch_context 已经切换 CR3 后同步状态
 */
void mm::Vmm::sync_current_dir(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
}

/**
 * @brief 切换到指定的页目录
 * @param dir_phys 页目录的物理地址
 * 
 * 通过 HAL 接口切换地址空间
 */
void mm::Vmm::switch_page_directory(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 通过 HAL 接口切换地址空间
    hal::Mmu::switch_space(dir_phys);
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
bool mm::Vmm::map_page_in_directory(uintptr_t dir_phys, uintptr_t virt, 
                                uintptr_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    if (!user_mapping_allowed(virt, flags)) return false;
    
    bool result;
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        // 转换为 HAL 标志
        uint32_t hal_flags = vmm_flags_to_hal(flags);
    
        // 使用 HAL 接口映射页面
        hal_addr_space_t space = (dir_phys == current_dir_phys) 
                                 ? HAL_ADDR_SPACE_CURRENT 
                                 : (hal_addr_space_t)dir_phys;
    
        result = hal::Mmu::map(space, (vaddr_t)virt, (paddr_t)phys, hal_flags);
    
        // 如果是当前页目录，刷新 TLB
        if (result && dir_phys == current_dir_phys) {
            hal::Mmu::flush_tlb((vaddr_t)virt);
        }
    }
    return result;
}

uintptr_t mm::Vmm::unmap_page_in_directory(uintptr_t dir_phys, uintptr_t virt) {
    if (virt & (PAGE_SIZE - 1)) {
        return 0;
    }

    paddr_t old_phys;
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        hal_addr_space_t space = (dir_phys == current_dir_phys) 
                                 ? HAL_ADDR_SPACE_CURRENT 
                                 : (hal_addr_space_t)dir_phys;
    
        // 以 unmap 的结果为准：query 对大页/块映射也会成功，但 unmap 不会
        // 拆除它们，此时不能把物理地址交给调用者去释放
        old_phys = hal::Mmu::unmap(space, (vaddr_t)virt);
        if (old_phys == PADDR_INVALID) {
            return 0;
        }

        // 如果是当前页目录，刷新 TLB
        if (dir_phys == current_dir_phys) {
            hal::Mmu::flush_tlb((vaddr_t)virt);
        }
    }
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
void mm::Vmm::cleanup_empty_page_tables(uintptr_t dir_phys, uintptr_t start_virt, uintptr_t end_virt) {
    if (!dir_phys || start_virt >= end_virt) {
        return;
    }
    
    // 只处理用户空间
    if (start_virt >= KERNEL_VIRTUAL_BASE || end_virt > KERNEL_VIRTUAL_BASE) {
        return;
    }
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
#if !defined(ARCH_I686)
    // x86_64 / arm64: 4-level paging - more complex cleanup needed
    // For now, skip cleanup as it requires walking multiple levels
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
            mm::Pmm::free_frame((paddr_t)table_phys);
        }
    }
#endif
}
