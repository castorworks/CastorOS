/**
 * @file vmm.h
 * @brief 虚拟内存管理器
 * 
 * 实现分页机制，管理虚拟地址到物理地址的映射
 * 
 * @see Requirements 3.4, 4.1, 4.2, 4.4, 12.1
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
 * @see Requirements 4.4, 12.1
 * ========================================================================== */

/**
 * @brief VMM 错误码枚举
 * 
 * 与 HAL 错误码保持一致，便于错误码转换。
 */
typedef enum vmm_error {
    VMM_OK = 0,                     /**< 操作成功 */
    VMM_ERR_INVALID_PARAM = -1,     /**< 无效参数（地址未对齐等） */
    VMM_ERR_NO_MEMORY = -2,         /**< 内存不足（无法分配页表或物理页） */
    VMM_ERR_NOT_SUPPORTED = -3,     /**< 不支持的操作 */
    VMM_ERR_NOT_FOUND = -4,         /**< 映射不存在 */
    VMM_ERR_ALREADY_MAPPED = -5,    /**< 地址已映射 */
    VMM_ERR_PERMISSION = -6,        /**< 权限错误 */
    VMM_ERR_COW_FAILED = -7,        /**< COW 处理失败 */
} vmm_error_t;

/**
 * @brief 检查 VMM 操作是否成功
 * @param err vmm_error_t 返回值
 * @return 如果操作成功返回非零值
 */
#define VMM_SUCCESS(err) ((err) == VMM_OK)

/**
 * @brief 检查 VMM 操作是否失败
 * @param err vmm_error_t 返回值
 * @return 如果操作失败返回非零值
 */
#define VMM_FAILED(err) ((err) != VMM_OK)

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
/** @brief PAT 标志（用于页表项）*/
#define PAGE_PAT        0x080
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
typedef page_directory_t pd_t;     ///< Page Directory/Level 2 (Level 2)
typedef page_table_t pt_t;         ///< Page Table/Level 3 (Level 1)

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
     * @brief 取消虚拟页映射
     * @param virt 虚拟地址（页对齐）
     */
    static void unmap_page(uintptr_t virt);

    /**
     * @brief 刷新TLB缓存
     * @param virt 虚拟地址（0表示刷新全部）
     */
    static void flush_tlb(uintptr_t virt);

    /**
     * @brief 获取当前页目录的物理地址
     * @return 页目录的物理地址
     */
    static uintptr_t get_page_directory();

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
     * @brief 清理指定范围内的空页表
     * @param dir_phys 页目录的物理地址
     * @param start_virt 起始虚拟地址（页对齐）
     * @param end_virt 结束虚拟地址（页对齐）
     * 
     * 检查指定虚拟地址范围内的页表，如果页表为空（所有条目都未映射），
     * 则释放该页表并清除对应的页目录项。
     * 
     * 注意：只处理用户空间页表，不会影响内核空间。
     */
    static void cleanup_empty_page_tables(uintptr_t dir_phys, uintptr_t start_virt, uintptr_t end_virt);

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

    /**
     * @brief 映射 MMIO 区域
     * @param phys_addr 物理地址
     * @param size 映射大小（字节）
     * @return 成功返回映射的虚拟地址，失败返回 0
     * 
     * 用于映射设备寄存器等内存映射 I/O 区域
     * 映射的页面标记为不可缓存（Cache-Disable）
     */
    static uintptr_t map_mmio(uintptr_t phys_addr, size_t size);

    /**
     * @brief 映射帧缓冲区域（使用 Write-Combining 模式）
     * @param phys_addr 物理地址
     * @param size 映射大小（字节）
     * @return 成功返回映射的虚拟地址，失败返回 0
     * 
     * 帧缓冲区使用 Write-Combining 缓存模式，可以将多个连续写入
     * 合并为一个操作，大幅提升图形输出性能。
     */
    static uintptr_t map_framebuffer(uintptr_t phys_addr, size_t size);

    /**
     * @brief 取消 MMIO 区域映射
     * @param virt_addr 虚拟地址
     * @param size 映射大小（字节）
     */
    static void unmap_mmio(uintptr_t virt_addr, size_t size);

    /**
     * @brief 初始化 PAT (Page Attribute Table)
     * 
     * 配置 PAT 以支持 Write-Combining 内存类型。
     * 应在 vmm_init() 后尽早调用。
     */
    static void init_pat();

    /**
     * @brief 通过查询页表获取虚拟地址对应的物理地址
     * @param virt 虚拟地址
     * @return 物理地址，如果虚拟地址未映射则返回 0
     * 
     * 注意：此函数会查询当前页目录的页表来获取真正的物理地址，
     * 而不是简单地使用 VIRT_TO_PHYS 宏（该宏只对恒等映射有效）。
     * 对于动态分配的堆内存，必须使用此函数获取物理地址用于 DMA 等操作。
     */
    static uintptr_t virt_to_phys(uintptr_t virt);

    /**
     * @brief 转储页表内容（调试功能）
     * @param dir_phys 页目录的物理地址（0 表示当前页目录）
     * @param start_virt 起始虚拟地址
     * @param end_virt 结束虚拟地址
     * 
     * 打印指定虚拟地址范围内的页表映射信息，包括：
     * - 虚拟地址
     * - 物理地址
     * - 页标志（Present, Write, User, COW 等）
     * 
     * @see Requirements 11.1
     */
    static void dump_page_tables(uintptr_t dir_phys, uintptr_t start_virt, uintptr_t end_virt);

    /**
     * @brief 转储当前页目录的用户空间映射
     * 
     * 便捷函数，转储当前页目录中用户空间（0x00000000 - KERNEL_VIRTUAL_BASE）的所有映射
     */
    static void dump_user_mappings();

    /**
     * @brief 转储当前页目录的内核空间映射
     * 
     * 便捷函数，转储当前页目录中内核空间（KERNEL_VIRTUAL_BASE - 0xFFFFFFFF）的所有映射
     */
    static void dump_kernel_mappings();

    /* ============================================================================
     * 错误码转换函数
     * 
     * 提供 HAL 错误码与 VMM 错误码之间的转换。
     * 
     * @see Requirements 4.4, 12.1
     * ========================================================================== */

    /**
     * @brief 将 HAL 错误码转换为 VMM 错误码
     * @param hal_err HAL 错误码
     * @return 对应的 VMM 错误码
     * 
     * 转换规则：
     *   HAL_OK              -> VMM_OK
     *   HAL_ERR_INVALID_PARAM -> VMM_ERR_INVALID_PARAM
     *   HAL_ERR_NO_MEMORY   -> VMM_ERR_NO_MEMORY
     *   HAL_ERR_NOT_SUPPORTED -> VMM_ERR_NOT_SUPPORTED
     *   HAL_ERR_NOT_FOUND   -> VMM_ERR_NOT_FOUND
     *   其他                -> VMM_ERR_INVALID_PARAM
     */
    static vmm_error_t error_from_hal(hal_error_t hal_err);

    /**
     * @brief 将 VMM 错误码转换为 HAL 错误码
     * @param vmm_err VMM 错误码
     * @return 对应的 HAL 错误码
     */
    static hal_error_t error_to_hal(vmm_error_t vmm_err);

    /**
     * @brief 获取 VMM 错误码的字符串描述
     * @param err VMM 错误码
     * @return 错误描述字符串
     */
    static const char *error_string(vmm_error_t err);
};

} // namespace mm

#endif // _MM_VMM_H_
