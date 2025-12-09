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
 * 
 * Requirements: 5.2, 12.1
 */

#include <types.h>
#include <hal/hal.h>
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
#define PTE64_WRITE_THROUGH (1ULL << 3)   /**< 写穿透 */
#define PTE64_CACHE_DISABLE (1ULL << 4)   /**< 禁用缓存 */
#define PTE64_ACCESSED      (1ULL << 5)   /**< 已访问 */
#define PTE64_DIRTY         (1ULL << 6)   /**< 已修改 */
#define PTE64_HUGE          (1ULL << 7)   /**< 大页 (2MB/1GB) */
#define PTE64_GLOBAL        (1ULL << 8)   /**< 全局页 */
#define PTE64_COW           (1ULL << 9)   /**< COW 标志 (Available bit) */
#define PTE64_NX            (1ULL << 63)  /**< 不可执行 */

/** 物理地址掩码 (bits 12-51 for 4KB pages) */
#define PTE64_ADDR_MASK     0x000FFFFFFFFFF000ULL

/** 页表项数量 */
#define PTE64_ENTRIES       512

/* ============================================================================
 * 地址分解宏
 * ========================================================================== */

/** @brief 获取 PML4 索引 (bits 47:39) */
static inline uint64_t pml4_index(uint64_t virt) {
    return (virt >> 39) & 0x1FF;
}

/** @brief 获取 PDPT 索引 (bits 38:30) */
static inline uint64_t pdpt_index(uint64_t virt) {
    return (virt >> 30) & 0x1FF;
}

/** @brief 获取 PD 索引 (bits 29:21) */
static inline uint64_t pd_index(uint64_t virt) {
    return (virt >> 21) & 0x1FF;
}

/** @brief 获取 PT 索引 (bits 20:12) */
static inline uint64_t pt_index(uint64_t virt) {
    return (virt >> 12) & 0x1FF;
}

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
void hal_mmu_flush_tlb(uintptr_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/**
 * @brief 刷新整个 TLB (x86_64)
 * 
 * 通过重新加载 CR3 寄存器来刷新整个 TLB
 */
void hal_mmu_flush_tlb_all(void) {
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
void hal_mmu_switch_space(uintptr_t page_table_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint64_t)page_table_phys) : "memory");
}

/**
 * @brief 获取页错误地址 (x86_64)
 * @return CR2 寄存器中的错误地址
 */
uintptr_t hal_mmu_get_fault_addr(void) {
    uint64_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    return (uintptr_t)fault_addr;
}

/**
 * @brief 获取当前 PML4 物理地址 (x86_64)
 * @return CR3 寄存器的值
 */
uintptr_t hal_mmu_get_current_page_table(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return (uintptr_t)cr3;
}

/**
 * @brief 启用分页 (x86_64)
 * 
 * 在 x86_64 长模式下，分页总是启用的
 * 此函数主要用于确保 CR0.PG 位已设置
 */
void hal_mmu_enable_paging(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Set PG bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

/**
 * @brief 检查分页是否启用 (x86_64)
 * @return true 如果分页已启用
 */
bool hal_mmu_is_paging_enabled(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) != 0;
}

/* ============================================================================
 * x86_64 页表格式验证
 * ========================================================================== */

/**
 * @brief 验证 x86_64 页表项格式
 * @param entry 页表项
 * @return true 如果格式正确
 * 
 * x86_64 PTE 格式:
 *   [63]    - NX (No Execute)
 *   [62:52] - Available / Reserved
 *   [51:12] - 物理页帧地址 (40 bits)
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
bool x86_64_validate_pte_format(pte64_t entry) {
    // 如果不存在，格式无关紧要
    if (!pte64_is_present(entry)) {
        return true;
    }
    
    // 物理地址必须页对齐
    uint64_t frame = pte64_get_frame(entry);
    if (frame & (PAGE_SIZE - 1)) {
        return false;
    }
    
    // 检查物理地址是否在有效范围内 (48-bit physical addressing)
    if (frame > PHYS_ADDR_MAX_X64) {
        return false;
    }
    
    return true;
}

/**
 * @brief 获取 x86_64 页表级数
 * @return 4 (x86_64 使用 4 级页表)
 */
uint32_t x86_64_get_page_table_levels(void) {
    return 4;
}

/**
 * @brief 获取 x86_64 页大小
 * @return 4096 (4KB)
 */
uint32_t x86_64_get_page_size(void) {
    return PAGE_SIZE;
}

/**
 * @brief 获取 x86_64 内核虚拟基址
 * @return 0xFFFF800000000000 (高半核)
 */
uint64_t x86_64_get_kernel_virtual_base(void) {
    return KERNEL_VIRTUAL_BASE_X64;
}

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

/**
 * @brief 检查地址是否在内核空间
 * @param virt 虚拟地址
 * @return true 如果在内核空间
 */
bool x86_64_is_kernel_address(uint64_t virt) {
    return virt >= KERNEL_VIRTUAL_BASE_X64;
}

/**
 * @brief 检查地址是否在用户空间
 * @param virt 虚拟地址
 * @return true 如果在用户空间
 */
bool x86_64_is_user_address(uint64_t virt) {
    return virt <= USER_SPACE_END_X64;
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

/**
 * @brief 解析 x86_64 页错误错误码
 * @param error_code 错误码
 * @return 解析后的页错误信息
 * 
 * x86_64 Page Fault Error Code:
 *   Bit 0 (P):    1 = 页面存在（保护违规），0 = 页面不存在
 *   Bit 1 (W/R):  1 = 写操作，0 = 读操作
 *   Bit 2 (U/S):  1 = 用户模式，0 = 内核模式
 *   Bit 3 (RSVD): 1 = 保留位被设置
 *   Bit 4 (I/D):  1 = 指令获取导致
 *   Bit 5 (PK):   1 = 保护密钥违规
 *   Bit 6 (SS):   1 = 影子栈访问
 *   Bit 15 (SGX): 1 = SGX 违规
 */
x86_64_page_fault_info_t x86_64_parse_page_fault_error(uint64_t error_code) {
    x86_64_page_fault_info_t info;
    info.present     = (error_code & (1ULL << 0)) != 0;
    info.write       = (error_code & (1ULL << 1)) != 0;
    info.user        = (error_code & (1ULL << 2)) != 0;
    info.reserved    = (error_code & (1ULL << 3)) != 0;
    info.instruction = (error_code & (1ULL << 4)) != 0;
    info.pk          = (error_code & (1ULL << 5)) != 0;
    info.ss          = (error_code & (1ULL << 6)) != 0;
    info.sgx         = (error_code & (1ULL << 15)) != 0;
    return info;
}

/**
 * @brief 检查是否为 COW 页错误
 * @param error_code 错误码
 * @return true 如果是 COW 页错误
 * 
 * COW 页错误特征：页面存在(P=1) + 写操作(W=1)
 */
bool x86_64_is_cow_fault(uint64_t error_code) {
    // P=1 (页面存在) + W=1 (写操作) = 0x3
    return (error_code & 0x3) == 0x3;
}

/**
 * @brief 获取页错误类型描述字符串
 * @param error_code 错误码
 * @return 描述字符串
 */
const char* x86_64_page_fault_type_str(uint64_t error_code) {
    x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(error_code);
    
    if (!info.present) {
        if (info.write) {
            return info.user ? "User write to non-present page" 
                             : "Kernel write to non-present page";
        } else {
            return info.user ? "User read from non-present page"
                             : "Kernel read from non-present page";
        }
    } else {
        if (info.write) {
            return info.user ? "User write protection violation"
                             : "Kernel write protection violation";
        } else if (info.instruction) {
            return info.user ? "User instruction fetch violation"
                             : "Kernel instruction fetch violation";
        } else {
            return info.user ? "User read protection violation"
                             : "Kernel read protection violation";
        }
    }
}

