/**
 * @file pagetable.cpp
 * @brief 多级页表的通用操作：hal::Mmu 里和页表结构打交道的那部分
 *
 * 查询、建立/撤销映射、改属性、创建/克隆/销毁地址空间在三个架构上是同一套逻辑，
 * 写在这里；表项的编码由各架构通过 <hal/pt.h> 里的函数提供。
 * 这些函数都不刷新 TLB（克隆除外，见 clone_space），由调用者决定。
 */

#include <hal/hal.h>
#include <hal/pt.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/string.h>

static inline uint32_t index_of(vaddr_t virt, int level) {
    return (uint32_t)((virt >> (PAGE_SHIFT + PT_INDEX_BITS * level)) & (PT_TABLE_SIZE - 1));
}

static inline pte_t *table_at(paddr_t phys) {
    return (pte_t *)PADDR_TO_KVADDR(phys);
}

/** 分配一张清零的页表 */
static paddr_t alloc_table(void) {
    paddr_t frame = mm::Pmm::alloc_frame();
    if (frame != PADDR_INVALID) {
        memset(table_at(frame), 0, PAGE_SIZE);
    }
    return frame;
}

/**
 * 沿着页表走到映射 virt 的那个表项。
 * @param level 得到它所在的级别（0 = 4KB 页；更高 = 大页）
 * @return 表项的地址；没有映射返回 NULL
 */
static pte_t *find_leaf(hal_addr_space_t space, vaddr_t virt, int *level) {
    if (!pt::valid_vaddr(virt)) {
        return NULL;
    }
    pte_t *table = pt::root(space, virt);
    for (int l = PT_TOP; ; l--) {
        pte_t *entry = &table[index_of(virt, l)];
        if (!pt::present(*entry)) {
            return NULL;
        }
        if (pt::is_leaf(*entry, l)) {
            if (l == PT_TOP && l != 0) {
                return NULL;        // 顶层没有大页：不是下一级表的表项当作无效
            }
            *level = l;
            return entry;
        }
        table = table_at(pt::addr(*entry));
    }
}

bool hal::Mmu::query(hal_addr_space_t space, vaddr_t virt, paddr_t *phys, uint32_t *flags) {
    int level = 0;
    pte_t *entry = find_leaf(space, virt, &level);
    if (!entry) {
        return false;
    }
    if (phys) {
        // 大页：加上页内的偏移，得到 virt 对应的那个地址
        vaddr_t span = (vaddr_t)1 << (PAGE_SHIFT + PT_INDEX_BITS * level);
        *phys = pt::addr(*entry) | (level > 0 ? (paddr_t)(virt & (span - 1)) : 0);
    }
    if (flags) {
        *flags = pt::leaf_flags(*entry);
    }
    return true;
}

bool hal::Mmu::map(hal_addr_space_t space, vaddr_t virt, paddr_t phys, uint32_t flags) {
    if (!IS_VADDR_ALIGNED(virt) || !IS_PADDR_ALIGNED(phys) || !pt::valid_vaddr(virt)) {
        return false;
    }
    pte_t *table = pt::root(space, virt);
    for (int l = PT_TOP; l > 0; l--) {
        uint32_t i = index_of(virt, l);
        if (!pt::present(table[i])) {
            paddr_t next = alloc_table();
            if (next == PADDR_INVALID) {
                return false;
            }
            table[i] = pt::make_table(next, flags);
            if (l == PT_TOP) {
                pt::top_entry_created(table, i);
            }
        } else if (pt::is_leaf(table[i], l)) {
            LOG_ERROR_MSG("hal::Mmu::map: 0x%llx is covered by a large page\n", (unsigned long long)virt);
            return false;
        } else if (flags & HAL_PAGE_USER) {
            table[i] = pt::table_for_user(table[i]);
        }
        table = table_at(pt::addr(table[i]));
    }
    table[index_of(virt, 0)] = pt::make_leaf(phys, flags);
    return true;
}

paddr_t hal::Mmu::unmap(hal_addr_space_t space, vaddr_t virt) {
    int level = 0;
    pte_t *entry = find_leaf(space, virt, &level);
    if (!entry) {
        return PADDR_INVALID;
    }
    if (level != 0) {
        LOG_ERROR_MSG("hal::Mmu::unmap: 0x%llx is covered by a large page\n", (unsigned long long)virt);
        return PADDR_INVALID;
    }
    paddr_t phys = pt::addr(*entry);
    *entry = 0;
    return phys;
}

bool hal::Mmu::protect(hal_addr_space_t space, vaddr_t virt, uint32_t set_flags, uint32_t clear_flags) {
    int level = 0;
    pte_t *entry = find_leaf(space, virt, &level);
    if (!entry) {
        return false;
    }
    *entry = pt::apply_delta(*entry, set_flags, clear_flags);
    return true;
}

hal_addr_space_t hal::Mmu::create_space() {
    paddr_t root = alloc_table();
    if (root == PADDR_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    pt::init_root(table_at(root));
    return (hal_addr_space_t)root;
}

/**
 * 释放一张表和它下面的所有东西。level 是这张表里的表项所在的级别。
 * 物理页由引用计数管理：Pmm::free_frame 只在计数归零时真正释放，所以和别的
 * 地址空间共享的页（写时复制、共享映射）只减计数；PMM 不管的设备内存它直接忽略。
 */
static void free_table(paddr_t table_phys, int level) {
    pte_t *table = table_at(table_phys);
    for (uint32_t i = 0; i < PT_TABLE_SIZE; i++) {
        pte_t entry = table[i];
        if (!pt::present(entry)) {
            continue;
        }
        if (!pt::is_leaf(entry, level)) {
            free_table(pt::addr(entry), level - 1);
        } else if (pt::kernel_only_leaf(entry)) {
            // 不是这个地址空间的东西
        } else if (level == 0) {
            mm::Pmm::free_frame(pt::addr(entry));
        } else if (mm::Pmm::frame_get_refcount(pt::addr(entry)) > 0) {
            mm::Pmm::frame_ref_dec(pt::addr(entry));
        }
    }
    mm::Pmm::free_frame(table_phys);
}

void hal::Mmu::destroy_space(hal_addr_space_t space) {
    if (space == HAL_ADDR_SPACE_INVALID || space == 0) {
        return;
    }
    if (space == hal::Mmu::current_space()) {
        LOG_ERROR_MSG("hal::Mmu::destroy_space: cannot destroy the current address space\n");
        return;
    }
    // 只有用户那一半是这个地址空间自己的；内核那一半的页表是共享的
    pte_t *root = table_at(space);
    for (uint32_t i = 0; i < PT_USER_ROOT_ENTRIES; i++) {
        if (pt::present(root[i]) && !pt::is_leaf(root[i], PT_TOP)) {
            free_table(pt::addr(root[i]), PT_TOP - 1);
        }
    }
    mm::Pmm::free_frame(space);
}

/**
 * 把 src 表的内容克隆进 dst 表（已经清零）。level 是表项所在的级别。
 *
 * 下一级表各复制一份；指向物理页的表项两边共用同一个页，引用计数加一。可写的页
 * 在两边都改成只读并打上 COW 标记，第一次写入时由缺页处理复制——共享映射
 * (HAL_PAGE_SHARED) 除外，它们就是要两边看到同一份内容。
 *
 * 失败（内存不够）时 dst 里已经填好的部分是自洽的，调用者把整棵树销毁即可。
 */
static bool clone_table(pte_t *src, pte_t *dst, int level) {
    for (uint32_t i = 0; i < PT_TABLE_SIZE; i++) {
        pte_t entry = src[i];
        if (!pt::present(entry)) {
            continue;
        }
        if (!pt::is_leaf(entry, level)) {
            paddr_t child = alloc_table();
            if (child == PADDR_INVALID) {
                return false;
            }
            // 新表项沿用原来的属性位，只换地址
            dst[i] = (pte_t)child | (entry & (pte_t)(PAGE_SIZE - 1));
            if (!clone_table(table_at(pt::addr(entry)), table_at(child), level - 1)) {
                return false;
            }
            continue;
        }
        if (pt::kernel_only_leaf(entry)) {
            dst[i] = entry;
            continue;
        }
        uint32_t flags = pt::leaf_flags(entry);
        if ((flags & HAL_PAGE_WRITE) && !(flags & HAL_PAGE_SHARED)) {
            entry = pt::apply_delta(entry, HAL_PAGE_COW, HAL_PAGE_WRITE);
            src[i] = entry;
        }
        if (level == 0) {
            mm::Pmm::frame_ref_share(pt::addr(entry));
        } else {
            mm::Pmm::frame_ref_inc(pt::addr(entry));
        }
        dst[i] = entry;
    }
    return true;
}

hal_addr_space_t hal::Mmu::clone_space(hal_addr_space_t src) {
    if (src == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    paddr_t src_phys = (src == HAL_ADDR_SPACE_CURRENT || src == 0)
                       ? hal::Mmu::get_current_page_table()
                       : (paddr_t)src;

    hal_addr_space_t copy = hal::Mmu::create_space();
    if (copy == HAL_ADDR_SPACE_INVALID) {
        return HAL_ADDR_SPACE_INVALID;
    }
    pte_t *src_root = table_at(src_phys);
    pte_t *new_root = table_at(copy);

    bool ok = true;
    for (uint32_t i = 0; ok && i < PT_USER_ROOT_ENTRIES; i++) {
        pte_t entry = src_root[i];
        if (!pt::present(entry) || pt::is_leaf(entry, PT_TOP)) {
            continue;
        }
        paddr_t child = alloc_table();
        if (child == PADDR_INVALID) {
            ok = false;
            break;
        }
        new_root[i] = (pte_t)child | (entry & (pte_t)(PAGE_SIZE - 1));
        ok = clone_table(table_at(pt::addr(entry)), table_at(child), PT_TOP - 1);
    }

    // 源地址空间的表项被改成了只读：它正在使用的话，旧的 TLB 项必须作废
    if (src_phys == hal::Mmu::get_current_page_table()) {
        hal::Mmu::flush_tlb_all();
    }

    if (!ok) {
        // 源这边已经打上的 COW 标记不用撤销：引用计数为 1 的 COW 页在写入时直接恢复可写
        hal::Mmu::destroy_space(copy);
        return HAL_ADDR_SPACE_INVALID;
    }
    return copy;
}
