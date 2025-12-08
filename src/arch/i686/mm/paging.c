/**
 * @file paging.c
 * @brief i686 架构特定的分页实现
 * 
 * 实现 i686 (x86 32-bit) 的 2 级页表操作
 * 提供 HAL MMU 接口的 i686 实现
 * 
 * Requirements: 5.2, 12.1
 */

#include <types.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>

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

/** @brief 获取页表索引 */
static inline uint32_t i686_pte_index(uint32_t virt) {
    return (virt >> 12) & 0x3FF;
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
void hal_mmu_flush_tlb(uintptr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (i686)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal_mmu_flush_tlb_all(void) {
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
void hal_mmu_switch_space(uintptr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_table_phys) : "memory");
}

/**
 * @brief 获取页错误地址 (i686)
 * @return CR2 寄存器中的错误地址
 */
uintptr_t hal_mmu_get_fault_addr(void) {
    uint32_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    return (uintptr_t)fault_addr;
}

/**
 * @brief 获取当前页目录物理地址 (i686)
 * @return CR3 寄存器的值
 */
uintptr_t hal_mmu_get_current_page_table(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (uintptr_t)cr3;
}

/**
 * @brief 启用分页 (i686)
 * 
 * 设置 CR0 的 PG 位启用分页
 * 注意：在调用此函数前必须先设置好 CR3
 */
void hal_mmu_enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Set PG bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

/**
 * @brief 检查分页是否启用 (i686)
 * @return true 如果分页已启用
 */
bool hal_mmu_is_paging_enabled(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) != 0;
}


/* ============================================================================
 * i686 页表格式验证
 * ========================================================================== */

/**
 * @brief 验证 i686 页表项格式
 * @param entry 页表项
 * @return true 如果格式正确
 * 
 * i686 PTE 格式:
 *   [31:12] - 物理页帧地址 (20 bits)
 *   [11:9]  - Available (3 bits, 用于 COW 等)
 *   [8]     - Global (G)
 *   [7]     - PAT (Page Attribute Table)
 *   [6]     - Dirty (D)
 *   [5]     - Accessed (A)
 *   [4]     - Cache Disable (PCD)
 *   [3]     - Write-Through (PWT)
 *   [2]     - User/Supervisor (U/S)
 *   [1]     - Read/Write (R/W)
 *   [0]     - Present (P)
 */
bool i686_validate_pte_format(uint32_t entry) {
    // 如果不存在，格式无关紧要
    if (!i686_is_present(entry)) {
        return true;
    }
    
    // 物理地址必须页对齐
    uint32_t frame = i686_get_frame(entry);
    if (frame & (PAGE_SIZE - 1)) {
        return false;
    }
    
    return true;
}

/**
 * @brief 验证 i686 页目录项格式
 * @param entry 页目录项
 * @return true 如果格式正确
 * 
 * i686 PDE 格式与 PTE 类似，但指向页表而非物理页
 */
bool i686_validate_pde_format(uint32_t entry) {
    // 如果不存在，格式无关紧要
    if (!i686_is_present(entry)) {
        return true;
    }
    
    // 页表地址必须页对齐
    uint32_t table_addr = i686_get_frame(entry);
    if (table_addr & (PAGE_SIZE - 1)) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取 i686 页表级数
 * @return 2 (i686 使用 2 级页表)
 */
uint32_t i686_get_page_table_levels(void) {
    return 2;
}

/**
 * @brief 获取 i686 页大小
 * @return 4096 (4KB)
 */
uint32_t i686_get_page_size(void) {
    return PAGE_SIZE;
}

/**
 * @brief 获取 i686 内核虚拟基址
 * @return 0x80000000 (2GB, 高半核)
 */
uintptr_t i686_get_kernel_virtual_base(void) {
    return KERNEL_VIRTUAL_BASE;
}
