/**
 * @file paging64.c
 * @brief x86_64 架构特定的分页实现
 * 
 * 实现 x86_64 (AMD64/Intel 64-bit) 的 4 级页表操作
 * 提供 HAL MMU 接口的 x86_64 实现
 * 
 * x86_64 使用 4 级页表：
 *   - PML4 (Page Map Level 4): 512 个 PML4E，每个 8 字节
 *   - PDPT (Page Directory Pointer Table): 512 个 PDPTE，每个 8 字节
 *   - PD (Page Directory): 512 个 PDE，每个 8 字节
 *   - PT (Page Table): 512 个 PTE，每个 8 字节
 * 
 * 虚拟地址分解 (48-bit canonical):
 *   [63:48] - 符号扩展 (必须与 bit 47 相同)
 *   [47:39] - PML4 索引 (9 bits, 512 entries)
 *   [38:30] - PDPT 索引 (9 bits, 512 entries)
 *   [29:21] - PD 索引 (9 bits, 512 entries)
 *   [20:12] - PT 索引 (9 bits, 512 entries)
 *   [11:0]  - 页内偏移 (12 bits, 4KB page)
 */

#include <types.h>
#include <hal/hal.h>
#include <hal/pt.h>
#include <lib/klog.h>
#include <lib/string.h>

/* x86_64 specific constants (from arch_types.h) */
#ifndef KERNEL_VIRTUAL_BASE_X64
#define KERNEL_VIRTUAL_BASE_X64     0xFFFF800000000000ULL
#endif

#ifndef USER_SPACE_END_X64
#define USER_SPACE_END_X64          0x00007FFFFFFFFFFFULL
#endif

#ifndef PHYS_ADDR_MAX_X64
#define PHYS_ADDR_MAX_X64           0x0000FFFFFFFFFFFFULL
#endif

/* ============================================================================
 * x86_64 页表结构定义
 * ========================================================================== */

/** 页表项类型 (64-bit) */
typedef uint64_t pte64_t;

/** 页表项标志位 */
#define PTE64_PRESENT       (1ULL << 0)   /**< 页存在 */
#define PTE64_WRITE         (1ULL << 1)   /**< 可写 */
#define PTE64_USER          (1ULL << 2)   /**< 用户可访问 */
#define PTE64_CACHE_DISABLE (1ULL << 4)   /**< 禁用缓存 */
#define PTE64_ACCESSED      (1ULL << 5)   /**< 已访问 */
#define PTE64_DIRTY         (1ULL << 6)   /**< 已修改 */
#define PTE64_HUGE          (1ULL << 7)   /**< 大页 (2MB/1GB) */
#define PTE64_COW           (1ULL << 9)   /**< COW 标志 (Available bit) */
#define PTE64_SHARED        (1ULL << 10)  /**< 共享映射：fork 时不做 COW (Available bit) */
#define PTE64_NX            (1ULL << 63)  /**< 不可执行 */

/** 物理地址掩码 (bits 12-51 for 4KB pages) */
#define PTE64_ADDR_MASK     0x000FFFFFFFFFF000ULL


/* ============================================================================
 * 地址分解宏
 * ========================================================================== */

/** @brief 从页表项中提取物理地址 */
static inline uint64_t pte64_get_frame(pte64_t entry) {
    return entry & PTE64_ADDR_MASK;
}

/** @brief 检查页表项是否存在 */
static inline bool pte64_is_present(pte64_t entry) {
    return (entry & PTE64_PRESENT) != 0;
}

/** @brief 检查是否为大页 */
static inline bool pte64_is_huge(pte64_t entry) {
    return (entry & PTE64_HUGE) != 0;
}

/* ============================================================================
 * HAL MMU 接口实现 - x86_64
 * ========================================================================== */

/**
 * @brief 刷新单个 TLB 条目 (x86_64)
 * @param virt 虚拟地址
 */
void hal::Mmu::flush_tlb(uintptr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (x86_64)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal::Mmu::flush_tlb_all() {
    __asm__ volatile(
        "mov %%cr3, %%rax\n\t"
        "mov %%rax, %%cr3"
        ::: "rax", "memory"
    );
}

/**
 * @brief 切换地址空间 (x86_64)
 * @param page_table_phys 新 PML4 的物理地址
 */
void hal::Mmu::switch_space(paddr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint64_t)page_table_phys) : "memory");
}

/**
 * @brief 获取当前 PML4 物理地址 (x86_64)
 * @return CR3 寄存器的值
 */
paddr_t hal::Mmu::get_current_page_table() {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (paddr_t)cr3;
}

/* ============================================================================
 * x86_64 页表格式验证
 * ========================================================================== */

/**
 * @brief 检查虚拟地址是否为规范地址 (canonical)
 * @param virt 虚拟地址
 * @return true 如果是规范地址
 * 
 * x86_64 使用 48 位虚拟地址，bits 63:48 必须与 bit 47 相同
 * 规范地址范围：
 *   - 低半部分: 0x0000000000000000 - 0x00007FFFFFFFFFFF
 *   - 高半部分: 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
 */
bool x86_64_is_canonical_address(uint64_t virt) {
    // 提取 bits 63:47
    uint64_t high_bits = virt >> 47;
    // 必须全为 0 或全为 1
    return (high_bits == 0) || (high_bits == 0x1FFFF);
}

/* ============================================================================
 * 页错误信息解析 (x86_64)
 * ========================================================================== */

/**
 * @brief x86_64 页错误信息结构
 */
typedef struct {
    bool present;       /**< 页是否存在 (P=1 表示保护违规) */
    bool write;         /**< 是否为写操作 */
    bool user;          /**< 是否为用户模式 */
    bool reserved;      /**< 是否为保留位错误 */
    bool instruction;   /**< 是否为指令获取 */
    bool pk;            /**< 是否为保护密钥违规 */
    bool ss;            /**< 是否为影子栈访问 */
    bool sgx;           /**< 是否为 SGX 违规 */
} x86_64_page_fault_info_t;


/* ============================================================================
 * HAL MMU 扩展接口实现 - x86_64
 * 
 * 实现 Requirements 4.1, 4.3, 5.1
 * ========================================================================== */

#include <mm/pmm.h>
#include <mm/mm_types.h>

/**
 * @brief 获取当前地址空间 (x86_64)
 * @return 当前 PML4 的物理地址
 */
hal_addr_space_t hal::Mmu::current_space() {
    return (hal_addr_space_t)hal::Mmu::get_current_page_table();
}

/**
 * @brief 将 HAL 页标志转换为 x86_64 页表项标志
 * @param hal_flags HAL 页标志 (HAL_PAGE_*)
 * @return x86_64 页表项标志
 */
static uint64_t hal_flags_to_x64(uint32_t hal_flags) {
    uint64_t x64_flags = 0;
    
    if (hal_flags & HAL_PAGE_PRESENT)   x64_flags |= PTE64_PRESENT;
    if (hal_flags & HAL_PAGE_WRITE)     x64_flags |= PTE64_WRITE;
    if (hal_flags & HAL_PAGE_USER)      x64_flags |= PTE64_USER;
    if (hal_flags & HAL_PAGE_NOCACHE)   x64_flags |= PTE64_CACHE_DISABLE;
    if (hal_flags & HAL_PAGE_COW)       x64_flags |= PTE64_COW;
    if (hal_flags & HAL_PAGE_SHARED)    x64_flags |= PTE64_SHARED;
    if (!(hal_flags & HAL_PAGE_EXEC))   x64_flags |= PTE64_NX;  /* NX = not executable */
    /* HAL_PAGE_DIRTY/ACCESSED: set by hardware, not by software */
    
    return x64_flags;
}

/**
 * @brief 将 x86_64 页表项标志转换为 HAL 页标志
 * @param x64_flags x86_64 页表项标志
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t x64_flags_to_hal(uint64_t x64_flags) {
    uint32_t hal_flags = 0;
    
    if (x64_flags & PTE64_PRESENT)       hal_flags |= HAL_PAGE_PRESENT;
    if (x64_flags & PTE64_WRITE)         hal_flags |= HAL_PAGE_WRITE;
    if (x64_flags & PTE64_USER)          hal_flags |= HAL_PAGE_USER;
    if (x64_flags & PTE64_CACHE_DISABLE) hal_flags |= HAL_PAGE_NOCACHE;
    if (x64_flags & PTE64_COW)           hal_flags |= HAL_PAGE_COW;
    if (x64_flags & PTE64_SHARED)        hal_flags |= HAL_PAGE_SHARED;
    if (x64_flags & PTE64_DIRTY)         hal_flags |= HAL_PAGE_DIRTY;
    if (x64_flags & PTE64_ACCESSED)      hal_flags |= HAL_PAGE_ACCESSED;
    if (!(x64_flags & PTE64_NX))         hal_flags |= HAL_PAGE_EXEC;
    
    return hal_flags;
}

/**
 * @brief 在现有页表项上应用 HAL 标志的增量（protect 用）
 *
 * 只改动 set/clear 中提到的属性。不能用 hal_flags_to_x64() 编码增量：
 * 它编码的是完整页表项，没带 HAL_PAGE_EXEC 时会加上 NX，于是
 * protect(WRITE, COW) 这类调用的 clear 掩码里也带 NX，
 * 把原本不可执行的数据页/栈页变成可执行。
 */
static pte64_t pte64_apply_flag_delta(pte64_t entry, uint32_t set_flags, uint32_t clear_flags) {
    static const struct { uint32_t hal; uint64_t x64; } direct[] = {
        { HAL_PAGE_PRESENT,  PTE64_PRESENT },
        { HAL_PAGE_WRITE,    PTE64_WRITE },
        { HAL_PAGE_USER,     PTE64_USER },
        { HAL_PAGE_NOCACHE,  PTE64_CACHE_DISABLE },
        { HAL_PAGE_COW,      PTE64_COW },
        { HAL_PAGE_SHARED,   PTE64_SHARED },
        { HAL_PAGE_DIRTY,    PTE64_DIRTY },
        { HAL_PAGE_ACCESSED, PTE64_ACCESSED },
    };
    for (const auto &m : direct) {
        if (set_flags & m.hal)   entry |= m.x64;
        if (clear_flags & m.hal) entry &= ~m.x64;
    }
    /* EXEC 是反向编码：NX=1 表示不可执行 */
    if (set_flags & HAL_PAGE_EXEC)   entry &= ~PTE64_NX;
    if (clear_flags & HAL_PAGE_EXEC) entry |= PTE64_NX;
    return entry;
}

/**
 * @brief 获取指定地址空间的 PML4 虚拟地址
 * @param space 地址空间句柄 (HAL_ADDR_SPACE_CURRENT 表示当前)
 * @return PML4 的虚拟地址
 */
static pte64_t* get_pml4(hal_addr_space_t space) {
    paddr_t pml4_phys;
    
    if (space == HAL_ADDR_SPACE_CURRENT || space == 0) {
        pml4_phys = hal::Mmu::get_current_page_table();
    } else {
        pml4_phys = space;
    }
    
    return (pte64_t*)PADDR_TO_KVADDR(pml4_phys);
}

/* ============================================================================
 * x86_64 地址空间管理实现
 * 
 * 实现 Requirements 5.2, 5.3, 5.5
 * ========================================================================== */


/**
 * @brief 让内核的直接映射区覆盖全部物理内存
 *
 * 引导页表只映射了前 1GB（一张页目录，512 个 2MB 大页）。这里按同样的方式为其余的
 * 物理内存每 1GB 建一张页目录，挂到内核那一半的 PDPT 上。PDPT 是所有地址空间共用的，
 * 所以新映射在每个地址空间里都看得到。
 *
 * 这时 1GB 以上的页帧还访问不到（PMM 清零新页时要通过直接映射区访问它），所以新页目录
 * 必须来自 1GB 以下——PMM 从低地址开始分配，启动阶段拿到的总是低处的页帧，这里再确认一下。
 */
void hal::Mmu::map_physical_memory() {
    const uint64_t GB = 1ULL << 30;
    const uint64_t huge = 2ULL << 20;
    uint64_t max_phys = (uint64_t)mm::Pmm::get_info().total_frames * PAGE_SIZE;

    pte64_t *pml4 = get_pml4(HAL_ADDR_SPACE_CURRENT);
    pte64_t *pdpt = (pte64_t *)PADDR_TO_KVADDR(pte64_get_frame(pml4[(KERNEL_VIRTUAL_BASE >> 39) & 0x1FF]));

    for (uint64_t base = GB; base < max_phys; base += GB) {
        uint64_t slot = base >> 30;
        if (slot >= 512) {
            LOG_WARN_MSG("MMU: physical memory above 512GB is not mapped\n");
            break;
        }
        if (pte64_is_present(pdpt[slot])) {
            continue;
        }
        paddr_t pd_phys = mm::Pmm::alloc_frame();
        if (pd_phys == PADDR_INVALID || pd_phys >= GB) {
            LOG_ERROR_MSG("MMU: no low frame for the direct map of %llu-%llu GB\n",
                          (unsigned long long)slot, (unsigned long long)slot + 1);
            if (pd_phys != PADDR_INVALID) {
                mm::Pmm::free_frame(pd_phys);
            }
            break;
        }
        pte64_t *pd = (pte64_t *)PADDR_TO_KVADDR(pd_phys);
        for (uint64_t i = 0; i < 512; i++) {
            pd[i] = (base + i * huge) | PTE64_PRESENT | PTE64_WRITE | PTE64_HUGE;
        }
        pdpt[slot] = pd_phys | PTE64_PRESENT | PTE64_WRITE;
    }
    hal::Mmu::flush_tlb_all();
    LOG_INFO_MSG("MMU: direct map covers %llu MB of physical memory\n",
                 (unsigned long long)(max_phys >> 20));
}

/**
 * @brief 内核地址缺页时同步内核映射：这个架构不需要
 *
 * 内核半区的顶层表项指向所有地址空间共享的下级页表，没有可同步的东西。
 */
bool hal::Mmu::sync_kernel_mapping(vaddr_t addr) {
    (void)addr;
    return false;
}

/* ============================================================================
 * 页表格式（通用页表代码 src/mm/pagetable.cpp 用，说明见 <hal/pt.h>）
 * ========================================================================== */

bool pt::valid_vaddr(vaddr_t virt) {
    return x86_64_is_canonical_address((uint64_t)virt);
}

pte_t *pt::root(hal_addr_space_t space, vaddr_t virt) {
    (void)virt;
    return get_pml4(space);
}

bool pt::present(pte_t entry) {
    return pte64_is_present(entry);
}

bool pt::is_leaf(pte_t entry, int level) {
    return level == 0 || pte64_is_huge(entry);
}

paddr_t pt::addr(pte_t entry) {
    return pte64_get_frame(entry);
}

pte_t pt::make_table(paddr_t table_phys, uint32_t hal_flags) {
    /* 权限是各级表项取交集：中间表项放开写，用户映射经过的还要带 USER，真正的限制在最后一级 */
    return table_phys | PTE64_PRESENT | PTE64_WRITE | ((hal_flags & HAL_PAGE_USER) ? PTE64_USER : 0);
}

pte_t pt::table_for_user(pte_t entry) {
    return entry | PTE64_USER;
}

pte_t pt::make_leaf(paddr_t phys, uint32_t hal_flags) {
    return phys | hal_flags_to_x64(hal_flags);
}

uint32_t pt::leaf_flags(pte_t entry) {
    return x64_flags_to_hal(entry);
}

pte_t pt::apply_delta(pte_t entry, uint32_t set_flags, uint32_t clear_flags) {
    return pte64_apply_flag_delta(entry, set_flags, clear_flags);
}

bool pt::kernel_only_leaf(pte_t entry) {
    (void)entry;
    return false;
}

void pt::init_root(pte_t *new_root) {
    /* 内核那一半 (PML4[256..511]) 指向所有地址空间共享的下级页表 */
    pte_t *current = get_pml4(HAL_ADDR_SPACE_CURRENT);
    for (uint32_t i = PT_USER_ROOT_ENTRIES; i < PT_TABLE_SIZE; i++) {
        new_root[i] = current[i];
    }
}

void pt::top_entry_created(pte_t *root, uint32_t index) {
    (void)root;
    (void)index;
}
