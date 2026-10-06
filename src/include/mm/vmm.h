/**
 * @file vmm.h
 * @brief 虚拟内存管理器
 * 
 * 实现分页机制，管理虚拟地址到物理地址的映射
 */

#ifndef _MM_VMM_H_
#define _MM_VMM_H_

#include <types.h>
#include <hal/hal_error.h>

/* ============================================================================
 * VMM 错误码定义
 * 
 * VMM 使用与 HAL 兼容的错误码，确保跨架构的错误处理一致性。
 * 
 * ========================================================================== */


/**
 * @brief 检查 VMM 操作是否成功
 * @param err vmm_error_t 返回值
 * @return 如果操作成功返回非零值
 */

/**
 * @brief 检查 VMM 操作是否失败
 * @param err vmm_error_t 返回值
 * @return 如果操作失败返回非零值
 */

/** @brief 页存在标志 */
#define PAGE_PRESENT    0x001
/** @brief 页可写标志 */
#define PAGE_WRITE      0x002
/** @brief 用户模式标志 */
#define PAGE_USER       0x004
/** @brief Write-Through 标志 */
#define PAGE_WRITE_THROUGH 0x008
/** @brief 禁用缓存标志 */
#define PAGE_CACHE_DISABLE 0x010
/** @brief 页可执行标志 */
#define PAGE_EXEC       0x100

/** 
 * @brief Copy-on-Write 标志（使用 x86 Available bit 9）
 * 
 * COW 机制说明：
 * - fork() 时，父子进程共享物理页，但各自有独立的页表
 * - 共享的可写页面被标记为只读 + PAGE_COW
 * - 首次写入时触发 Page Fault (#14)，由 vmm_handle_cow_page_fault 处理
 * - COW handler 会：
 *   1. 如果引用计数 == 1：直接恢复写权限（无需复制）
 *   2. 如果引用计数 > 1：分配新物理页，复制内容，更新页表
 * 
 * 注意：PAGE_COW 与 PAGE_WRITE 互斥，COW 页面必须是只读的
 */
#define PAGE_COW        0x200

/**
 * 共享映射（软件标志，x86 页表项的 Available 位 10）
 *
 * 设备内存和进程间共享内存用它标记。fork 时这样的页不做写时复制，
 * 父子进程继续指向同一个物理帧并保持可写。
 */
#define PAGE_SHARED     0x400

/**
 * @brief 用户地址范围的上界（不含）
 *
 * 带 PAGE_USER 的映射只允许建立在这个地址以下。
 */
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
#define VMM_USER_VADDR_END 0x0000800000000000ULL
#else
#define VMM_USER_VADDR_END KERNEL_VIRTUAL_BASE
#endif

/* Architecture-specific page table types */
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
/* x86_64/ARM64: 4-level paging with 64-bit entries, 512 entries per level */
typedef uint64_t pde_t;  ///< 页目录项类型 (64-bit)
typedef uint64_t pte_t;  ///< 页表项类型 (64-bit)

typedef struct {
    pte_t entries[512];   ///< 页表项数组 (512 entries for 64-bit archs)
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

typedef struct {
    pde_t entries[512];   ///< 页目录项数组 (512 entries for 64-bit archs)
} __attribute__((aligned(PAGE_SIZE))) page_directory_t;

/* 4-level paging: PML4/L0 -> PDPT/L1 -> PD/L2 -> PT/L3 */
typedef page_directory_t pml4_t;   ///< PML4/Level 0 (Level 4)
typedef page_directory_t pdpt_t;   ///< PDPT/Level 1 (Level 3)

#else
/* i686: 2-level paging with 32-bit entries, 1024 entries per level */
typedef uint32_t pde_t;  ///< 页目录项类型
typedef uint32_t pte_t;  ///< 页表项类型

/**
 * @brief 页目录结构
 * 
 * 包含1024个页目录项，每个项指向一个页表
 */
typedef struct {
    pde_t entries[1024];  ///< 页目录项数组
} __attribute__((aligned(PAGE_SIZE))) page_directory_t;

/**
 * @brief 页表结构
 * 
 * 包含1024个页表项，每个项映射一个4KB页面
 */
typedef struct {
    pte_t entries[1024];  ///< 页表项数组
} __attribute__((aligned(PAGE_SIZE))) page_table_t;
#endif

namespace mm {

/**
 * @brief 虚拟内存管理器（页表映射、地址空间创建/克隆、缺页处理）
 */
class Vmm {
public:
    /**
     * @brief 初始化虚拟内存管理器
     */
    static void init();

    /**
     * @brief 映射虚拟页到物理页
     * @param virt 虚拟地址（页对齐）
     * @param phys 物理地址（页对齐）
     * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
     * @return 成功返回 true，失败返回 false
     * 
     * Note: On x86_64, this function is a stub that returns false.
     * Full 64-bit VMM support requires implementing 4-level paging.
     */
    static bool map_page(uintptr_t virt, uintptr_t phys, uint32_t flags);

    /**
     * @brief 获取当前页目录的物理地址
     * @return 页目录的物理地址
     */
    static uintptr_t get_page_directory();

    /** 内核自己的页表（顶层表的物理地址）：不属于任何进程，idle 任务用它 */
    static uintptr_t kernel_page_directory();

    /**
     * @brief 创建新的页目录（用于新进程）
     * @return 成功返回页目录的物理地址，失败返回 0
     * 
     * 创建的页目录会：
     * 1. 自动复制内核空间映射（0x80000000+）
     * 2. 用户空间（0x00000000-0x7FFFFFFF）初始为空
     * 
     * Note: On x86_64, this function is a stub that returns 0.
     */
    static uintptr_t create_page_directory();

    /**
     * @brief 克隆页目录（用于 fork，实现 COW 语义）
     * @param src_dir_phys 源页目录的物理地址
     * @return 成功返回新页目录的物理地址，失败返回 0
     * 
     * Copy-on-Write (COW) 实现：
     * - 页表是独立的（每个进程有自己的页表副本）
     * - 物理页是共享的（通过引用计数管理）
     * - 可写页面被标记为只读 + PAGE_COW
     * - 首次写入时触发 page fault，由 vmm_handle_cow_page_fault 处理
     * 
     * Note: On x86_64, this function is a stub that returns 0.
     */
    static uintptr_t clone_page_directory(uintptr_t src_dir_phys);

    /**
     * @brief 释放页目录及其用户空间页表
     * @param dir_phys 页目录的物理地址
     * 
     * 注意：只释放用户空间页表，内核空间页表是共享的
     */
    static void free_page_directory(uintptr_t dir_phys);

    /**
     * @brief 同步 VMM 的 current_dir_phys（不切换 CR3）
     * @param dir_phys 当前页目录的物理地址
     */
    static void sync_current_dir(uintptr_t dir_phys);

    /**
     * @brief 切换到指定的页目录
     * @param dir_phys 页目录的物理地址
     */
    static void switch_page_directory(uintptr_t dir_phys);

    /**
     * @brief 在指定页目录中映射页面
     * @param dir_phys 页目录的物理地址
     * @param virt 虚拟地址
     * @param phys 物理地址
     * @param flags 页标志
     * @return 成功返回 true，失败返回 false
     */
    static bool map_page_in_directory(uintptr_t dir_phys, uintptr_t virt, 
                                    uintptr_t phys, uint32_t flags);
    static uintptr_t unmap_page_in_directory(uintptr_t dir_phys, uintptr_t virt);

    /**
     * @brief 处理内核空间缺页异常（同步内核页目录）
     * @param addr 缺页地址
     * @return 是否成功处理（如果成功，不需要 panic）
     */
    static bool handle_kernel_page_fault(uintptr_t addr);

    /**
     * @brief 处理写保护异常（COW）
     * @param addr 缺页地址
     * @param error_code 错误码
     * @return 是否成功处理（如果成功，不需要 panic）
     */
    static bool handle_cow_page_fault(uintptr_t addr, uint32_t error_code);

    /* ============================================================================
     * 错误码转换函数
     * 
     * 提供 HAL 错误码与 VMM 错误码之间的转换。
     * 
     * ========================================================================== */

};

} // namespace mm

#endif // _MM_VMM_H_
