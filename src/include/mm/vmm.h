/**
 * @file vmm.h
 * @brief 虚拟内存管理器
 * 
 * 实现分页机制，管理虚拟地址到物理地址的映射
 */

#ifndef _MM_VMM_H_
#define _MM_VMM_H_

#include <types.h>

/** @brief 页存在标志 */
#define PAGE_PRESENT    0x001
/** @brief 页可写标志 */
#define PAGE_WRITE      0x002
/** @brief 用户模式标志 */
#define PAGE_USER       0x004
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

/**
 * @brief 初始化虚拟内存管理器
 */
void vmm_init(void);

/**
 * @brief 映射虚拟页到物理页
 * @param virt 虚拟地址（页对齐）
 * @param phys 物理地址（页对齐）
 * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
 * @return 成功返回 true，失败返回 false
 */
bool vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/**
 * @brief 取消虚拟页映射
 * @param virt 虚拟地址（页对齐）
 */
void vmm_unmap_page(uint32_t virt);

/**
 * @brief 刷新TLB缓存
 * @param virt 虚拟地址（0表示刷新全部）
 */
void vmm_flush_tlb(uint32_t virt);

/**
 * @brief 获取当前页目录的物理地址
 * @return 页目录的物理地址
 */
uint32_t vmm_get_page_directory(void);

/**
 * @brief 创建新的页目录（用于新进程）
 * @return 成功返回页目录的物理地址，失败返回 0
 * 
 * 创建的页目录会：
 * 1. 自动复制内核空间映射（0x80000000+）
 * 2. 用户空间（0x00000000-0x7FFFFFFF）初始为空
 */
uint32_t vmm_create_page_directory(void);

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
 * 引用计数：
 * - 每个共享的物理页都有引用计数
 * - 克隆时增加引用计数
 * - 释放页目录时减少引用计数
 * - 引用计数降为 0 时才真正释放物理页
 */
uint32_t vmm_clone_page_directory(uint32_t src_dir_phys);

/**
 * @brief 释放页目录及其用户空间页表
 * @param dir_phys 页目录的物理地址
 * 
 * 注意：只释放用户空间页表，内核空间页表是共享的
 */
void vmm_free_page_directory(uint32_t dir_phys);

/**
 * @brief 同步 VMM 的 current_dir_phys（不切换 CR3）
 * @param dir_phys 当前页目录的物理地址
 */
void vmm_sync_current_dir(uint32_t dir_phys);

/**
 * @brief 切换到指定的页目录
 * @param dir_phys 页目录的物理地址
 */
void vmm_switch_page_directory(uint32_t dir_phys);

/**
 * @brief 在指定页目录中映射页面
 * @param dir_phys 页目录的物理地址
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 * @return 成功返回 true，失败返回 false
 */
bool vmm_map_page_in_directory(uint32_t dir_phys, uint32_t virt, 
                                uint32_t phys, uint32_t flags);
uint32_t vmm_unmap_page_in_directory(uint32_t dir_phys, uint32_t virt);

/**
 * @brief 处理内核空间缺页异常（同步内核页目录）
 * @param addr 缺页地址
 * @return 是否成功处理（如果成功，不需要 panic）
 */
bool vmm_handle_kernel_page_fault(uint32_t addr);

/**
 * @brief 处理写保护异常（COW）
 * @param addr 缺页地址
 * @param error_code 错误码
 * @return 是否成功处理（如果成功，不需要 panic）
 */
bool vmm_handle_cow_page_fault(uint32_t addr, uint32_t error_code);

#endif // _MM_VMM_H_
