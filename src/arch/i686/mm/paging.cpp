/**
 * @file paging.c
 * @brief i686 架构特定的分页实现
 * 
 * 实现 i686 (x86 32-bit) 的 2 级页表操作
 * 提供 HAL MMU 接口的 i686 实现
 */

#include <types.h>
#include <mm/mm_types.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>
#include <hal/hal.h>
#include <hal/pt.h>

/* 引导页目录（定义在 boot.asm） */
extern "C" uint32_t boot_page_directory[];

/* ============================================================================
 * i686 页表结构定义
 * ============================================================================
 * 
 * i686 使用 2 级页表：
 *   - 页目录 (Page Directory): 1024 个 PDE，每个 4 字节
 *   - 页表 (Page Table): 1024 个 PTE，每个 4 字节
 * 
 * 虚拟地址分解 (32-bit):
 *   [31:22] - 页目录索引 (10 bits, 1024 entries)
 *   [21:12] - 页表索引 (10 bits, 1024 entries)
 *   [11:0]  - 页内偏移 (12 bits, 4KB page)
 * ========================================================================== */

/** @brief 获取页目录索引 */
static inline uint32_t i686_pde_index(uint32_t virt) {
    return virt >> 22;
}

/** @brief 从页表项中提取物理地址 */
static inline uint32_t i686_get_frame(uint32_t entry) {
    return entry & 0xFFFFF000;
}

/** @brief 检查页表项是否存在 */
static inline bool i686_is_present(uint32_t entry) {
    return (entry & PAGE_PRESENT) != 0;
}


/* ============================================================================
 * HAL MMU 接口实现 - i686
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目 (i686)
 * @param virt 虚拟地址
 */
void hal::Mmu::flush_tlb(vaddr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"((uint32_t)virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (i686)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal::Mmu::flush_tlb_all() {
    __asm__ volatile(
        "mov %%cr3, %%eax\n\t"
        "mov %%eax, %%cr3"
        ::: "eax", "memory"
    );
}

/**
 * @brief 切换地址空间 (i686)
 * @param page_table_phys 新页目录的物理地址
 */
void hal::Mmu::switch_space(paddr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_table_phys) : "memory");
}

/**
 * @brief 获取当前页目录物理地址 (i686)
 * @return CR3 寄存器的值
 */
paddr_t hal::Mmu::get_current_page_table() {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (paddr_t)cr3;
}


/* ============================================================================
 * HAL MMU 扩展接口实现 - i686
 * 
 * 实现 Requirements 4.1, 4.3, 4.4, 4.5
 * ========================================================================== */

/**
 * @brief 获取当前地址空间 (i686)
 * @return 当前页目录的物理地址
 */
hal_addr_space_t hal::Mmu::current_space() {
    return (hal_addr_space_t)hal::Mmu::get_current_page_table();
}

/**
 * @brief 将 HAL 页标志转换为 i686 页表项标志
 * @param hal_flags HAL 页标志 (HAL_PAGE_*)
 * @return i686 页表项标志
 */
static uint32_t hal_flags_to_i686(uint32_t hal_flags) {
    uint32_t i686_flags = 0;
    
    if (hal_flags & HAL_PAGE_PRESENT)   i686_flags |= PAGE_PRESENT;
    if (hal_flags & HAL_PAGE_WRITE)     i686_flags |= PAGE_WRITE;
    if (hal_flags & HAL_PAGE_USER)      i686_flags |= PAGE_USER;
    if (hal_flags & HAL_PAGE_NOCACHE)   i686_flags |= PAGE_CACHE_DISABLE;
    if (hal_flags & HAL_PAGE_COW)       i686_flags |= PAGE_COW;
    if (hal_flags & HAL_PAGE_SHARED)    i686_flags |= PAGE_SHARED;
    /* HAL_PAGE_EXEC: i686 doesn't have NX bit in standard mode */
    /* HAL_PAGE_DIRTY/ACCESSED: set by hardware, not by software */
    
    return i686_flags;
}

/**
 * @brief 将 i686 页表项标志转换为 HAL 页标志
 * @param i686_flags i686 页表项标志
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t i686_flags_to_hal(uint32_t i686_flags) {
    uint32_t hal_flags = 0;
    
    if (i686_flags & PAGE_PRESENT)       hal_flags |= HAL_PAGE_PRESENT;
    if (i686_flags & PAGE_WRITE)         hal_flags |= HAL_PAGE_WRITE;
    if (i686_flags & PAGE_USER)          hal_flags |= HAL_PAGE_USER;
    if (i686_flags & PAGE_CACHE_DISABLE) hal_flags |= HAL_PAGE_NOCACHE;
    if (i686_flags & PAGE_COW)           hal_flags |= HAL_PAGE_COW;
    if (i686_flags & PAGE_SHARED)        hal_flags |= HAL_PAGE_SHARED;
    if (i686_flags & 0x40)               hal_flags |= HAL_PAGE_DIRTY;    /* Bit 6 */
    if (i686_flags & 0x20)               hal_flags |= HAL_PAGE_ACCESSED; /* Bit 5 */
    
    return hal_flags;
}

/**
 * @brief 获取指定地址空间的页目录虚拟地址
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @return 页目录的虚拟地址
 */
static page_directory_t* get_page_directory(hal_addr_space_t space) {
    paddr_t dir_phys;
    
    if (space == HAL_ADDR_SPACE_CURRENT || space == 0) {
        dir_phys = hal::Mmu::get_current_page_table();
    } else {
        dir_phys = space;
    }
    
    return (page_directory_t*)PHYS_TO_VIRT((uintptr_t)dir_phys);
}


/* ============================================================================
 * 大页映射实现 (i686)
 * 
 * i686 支持 4MB 大页（通过 PSE），但此实现使用回退方式：
 * 将 2MB 大页请求映射为 512 个 4KB 页。
 * 
 * ========================================================================== */


/**
 * @brief 让内核的直接映射区覆盖全部物理内存
 *
 * 引导代码只映射了前 16MB。这里把其余的物理内存（最多 2GB）映射到
 * KERNEL_VIRTUAL_BASE 之上（PMM 清零新分配的帧时要通过这个区域访问它们）。
 */
void hal::Mmu::map_physical_memory() {
    page_directory_t *current_dir = (page_directory_t *)boot_page_directory;
    uintptr_t current_dir_phys = VIRT_TO_PHYS((uintptr_t)current_dir);
    
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
        if (i686_is_present(current_dir->entries[pde])) {
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
        hal::Mmu::flush_tlb_all();
        mapped_pdes++;
    }
    
    LOG_INFO_MSG("VMM: Extended mapping by %u PDEs (now covers 0-%u MB)\n", 
                 mapped_pdes, ((end_pde - 512) * 4));
    
    LOG_INFO_MSG("VMM: High-half kernel mapping extended\n");
}

/**
 * @brief 内核地址缺页时，从主内核页目录同步缺少的页目录项
 *
 * i686 的每个地址空间在创建时复制一份内核半区的页目录项，之后新建的内核页表
 * 只记在主内核页目录（和创建它的那个地址空间）里。别的地址空间第一次访问到时缺页，
 * 在这里补上。
 *
 * @return 补上了返回 true（重试访问即可）；否则是真正的缺页
 */
bool hal::Mmu::sync_kernel_mapping(vaddr_t addr) {
    if (addr < KERNEL_VIRTUAL_BASE) {
        return false;
    }
    uint32_t pd_idx = i686_pde_index((uint32_t)addr);
    page_directory_t *master = (page_directory_t *)boot_page_directory;
    page_directory_t *current = (page_directory_t *)PHYS_TO_VIRT((uintptr_t)hal::Mmu::get_current_page_table());

    // 主内核页目录里也没有，或者当前页目录已经有同样的项（缺的是页表项）：同步解决不了
    if (!i686_is_present(master->entries[pd_idx]) || current->entries[pd_idx] == master->entries[pd_idx]) {
        return false;
    }
    current->entries[pd_idx] = master->entries[pd_idx];
    hal::Mmu::flush_tlb(addr);
    return true;
}

/* ============================================================================
 * 页表格式（通用页表代码 src/mm/pagetable.cpp 用，说明见 <hal/pt.h>）
 * ========================================================================== */

bool pt::valid_vaddr(vaddr_t virt) {
    (void)virt;
    return true;
}

pte_t *pt::root(hal_addr_space_t space, vaddr_t virt) {
    (void)virt;
    return get_page_directory(space)->entries;
}

bool pt::present(pte_t entry) {
    return i686_is_present(entry);
}

bool pt::is_leaf(pte_t entry, int level) {
    (void)entry;
    return level == 0;      /* 不用 4MB 大页 */
}

paddr_t pt::addr(pte_t entry) {
    return i686_get_frame(entry);
}

pte_t pt::make_table(paddr_t table_phys, uint32_t hal_flags) {
    /* 权限是两级表项取交集：页目录项放开写，用户映射经过的还要带 USER，真正的限制在页表项 */
    return (pte_t)table_phys | PAGE_PRESENT | PAGE_WRITE | ((hal_flags & HAL_PAGE_USER) ? PAGE_USER : 0);
}

pte_t pt::table_for_user(pte_t entry) {
    return entry | PAGE_USER;
}

pte_t pt::make_leaf(paddr_t phys, uint32_t hal_flags) {
    return (pte_t)phys | hal_flags_to_i686(hal_flags);
}

uint32_t pt::leaf_flags(pte_t entry) {
    return i686_flags_to_hal(entry & 0xFFF);
}

pte_t pt::apply_delta(pte_t entry, uint32_t set_flags, uint32_t clear_flags) {
    return (entry | hal_flags_to_i686(set_flags)) & ~hal_flags_to_i686(clear_flags);
}

bool pt::kernel_only_leaf(pte_t entry) {
    (void)entry;
    return false;
}

void pt::init_root(pte_t *new_root) {
    /* 内核那一半的页目录项从主内核页目录复制一份；之后新增的靠缺页时同步
     * （sync_kernel_mapping） */
    for (uint32_t i = PT_USER_ROOT_ENTRIES; i < PT_TABLE_SIZE; i++) {
        new_root[i] = boot_page_directory[i];
    }
}

void pt::top_entry_created(pte_t *root, uint32_t index) {
    /* 新建的内核页表要记进主内核页目录，别的地址空间才同步得到 */
    if (index >= PT_USER_ROOT_ENTRIES && root != boot_page_directory) {
        boot_page_directory[index] = root[index];
    }
}
