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

static page_directory_t *current_dir = NULL;  ///< 当前页目录虚拟地址
static uint32_t current_dir_phys = 0;          ///< 当前页目录物理地址
extern uint32_t boot_page_directory[];         ///< 引导时的页目录

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
 */
void vmm_init(void) {
    current_dir = (page_directory_t*)boot_page_directory;
    current_dir_phys = VIRT_TO_PHYS((uint32_t)current_dir);
    
    // 检查并更新CR3寄存器
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 != current_dir_phys)
        __asm__ volatile("mov %0, %%cr3" : : "r"(current_dir_phys));
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
    
    uint32_t pd = pde_idx(virt);  // 页目录索引
    uint32_t pt = pte_idx(virt);  // 页表索引
    
    pde_t *pde = &current_dir->entries[pd];
    page_table_t *table;
    
    // 如果页表不存在，创建新页表
    if (!is_present(*pde)) {
        table = create_page_table();
        if (!table) return false;
        *pde = VIRT_TO_PHYS((uint32_t)table) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    }
    
    // 设置页表项
    table->entries[pt] = phys | flags;
    vmm_flush_tlb(virt);
    return true;
}

/**
 * @brief 取消虚拟页映射
 * @param virt 虚拟地址（页对齐）
 */
void vmm_unmap_page(uint32_t virt) {
    // 检查页对齐
    if (virt & (PAGE_SIZE-1)) return;
    
    pde_t *pde = &current_dir->entries[pde_idx(virt)];
    // 如果页表不存在，直接返回
    if (!is_present(*pde)) return;
    
    // 清除页表项
    page_table_t *table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    table->entries[pte_idx(virt)] = 0;
    vmm_flush_tlb(virt);
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
    
    // 获取虚拟地址访问
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 清空整个页目录
    memset(new_dir, 0, sizeof(page_directory_t));
    
    // 复制内核空间映射（512-1023，即 0x80000000-0xFFFFFFFF）
    // 内核空间在所有进程中共享，直接复制页目录项即可
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = current_dir->entries[i];
    }
    
    return dir_phys;
}

/**
 * @brief 克隆页目录（用于 fork）
 * @param src_dir_phys 源页目录的物理地址
 * @return 成功返回新页目录的物理地址，失败返回 0
 */
uint32_t vmm_clone_page_directory(uint32_t src_dir_phys) {
    // 分配新页目录
    uint32_t new_dir_phys = pmm_alloc_frame();
    if (!new_dir_phys) return 0;
    
    page_directory_t *src_dir = (page_directory_t*)PHYS_TO_VIRT(src_dir_phys);
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(new_dir_phys);
    
    // 清空新页目录
    memset(new_dir, 0, sizeof(page_directory_t));
    
    // 复制内核空间映射（共享）
    for (uint32_t i = 512; i < 1024; i++) {
        new_dir->entries[i] = src_dir->entries[i];
    }
    
    // 复制用户空间映射（深拷贝：复制物理页）
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(src_dir->entries[i])) {
            // 分配新页表
            uint32_t new_table_phys = pmm_alloc_frame();
            if (!new_table_phys) {
                // 分配失败，清理已分配的资源
                vmm_free_page_directory(new_dir_phys);
                return 0;
            }
            
            page_table_t *src_table = (page_table_t*)PHYS_TO_VIRT(get_frame(src_dir->entries[i]));
            page_table_t *new_table = (page_table_t*)PHYS_TO_VIRT(new_table_phys);
            
            // 清空新页表
            memset(new_table, 0, sizeof(page_table_t));
            
            // 逐页复制（深拷贝物理页，而不是共享）
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(src_table->entries[j])) {
                    // 分配新的物理页
                    uint32_t new_frame = pmm_alloc_frame();
                    if (!new_frame) {
                        // 分配失败，清理
                        pmm_free_frame(new_table_phys);
                        vmm_free_page_directory(new_dir_phys);
                        return 0;
                    }
                    
                    // 复制页面内容
                    uint32_t src_frame = get_frame(src_table->entries[j]);
                    void *src_virt = (void*)PHYS_TO_VIRT(src_frame);
                    void *dst_virt = (void*)PHYS_TO_VIRT(new_frame);
                    memcpy(dst_virt, src_virt, PAGE_SIZE);
                    
                    // 设置新页表项（保留权限标志）
                    new_table->entries[j] = new_frame | (src_table->entries[j] & 0xFFF);
                }
            }
            
            // 设置新页目录项
            new_dir->entries[i] = new_table_phys | (src_dir->entries[i] & 0xFFF);
        }
    }
    
    return new_dir_phys;
}

/**
 * @brief 释放页目录及其用户空间页表和物理页
 * @param dir_phys 页目录的物理地址
 * 
 * 注意：由于 fork 现在使用深拷贝，所以需要释放所有物理页
 */
void vmm_free_page_directory(uint32_t dir_phys) {
    if (!dir_phys) return;
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 只释放用户空间页表（0-511）
    // 内核空间页表（512-1023）是共享的，不释放
    for (uint32_t i = 0; i < 512; i++) {
        if (is_present(dir->entries[i])) {
            uint32_t table_phys = get_frame(dir->entries[i]);
            page_table_t *table = (page_table_t*)PHYS_TO_VIRT(table_phys);
            
            // 释放页表中的所有物理页
            for (uint32_t j = 0; j < 1024; j++) {
                if (is_present(table->entries[j])) {
                    uint32_t frame = get_frame(table->entries[j]);
                    pmm_free_frame(frame);
                }
            }
            
            // 释放页表本身
            pmm_free_frame(table_phys);
        }
    }
    
    // 释放页目录本身
    pmm_free_frame(dir_phys);
}

/**
 * @brief 切换到指定的页目录
 * @param dir_phys 页目录的物理地址
 */
void vmm_switch_page_directory(uint32_t dir_phys) {
    if (!dir_phys) return;
    
    current_dir_phys = dir_phys;
    current_dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    // 加载到 CR3 寄存器
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir_phys) : "memory");
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
    
    page_directory_t *dir = (page_directory_t*)PHYS_TO_VIRT(dir_phys);
    
    uint32_t pd = pde_idx(virt);
    uint32_t pt = pte_idx(virt);
    
    pde_t *pde = &dir->entries[pd];
    page_table_t *table;
    
    // 如果页表不存在，创建新页表
    if (!is_present(*pde)) {
        uint32_t table_phys = pmm_alloc_frame();
        if (!table_phys) return false;
        
        table = (page_table_t*)PHYS_TO_VIRT(table_phys);
        memset(table, 0, sizeof(page_table_t));
        
        *pde = table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        table = (page_table_t*)PHYS_TO_VIRT(get_frame(*pde));
    }
    
    // 设置页表项
    table->entries[pt] = phys | flags;
    
    // 如果是当前页目录，刷新 TLB
    if (dir_phys == current_dir_phys) {
        vmm_flush_tlb(virt);
    }
    
    return true;
}
