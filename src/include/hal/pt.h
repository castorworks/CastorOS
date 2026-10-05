/**
 * @file pt.h
 * @brief 页表格式：通用页表代码 (src/mm/pagetable.cpp) 向各架构要的东西
 *
 * 多级页表在三个架构上是同一种结构：一棵固定深度的树，按虚拟地址的若干位逐级
 * 取下标，最后一级的表项指向物理页。不同的只是级数、每级的位数和表项里各个位的
 * 含义。遍历、建立映射、克隆（写时复制）、销毁写成一份通用代码；这里声明的函数
 * 由各架构实现（src/arch/<arch>/mm/），回答"表项怎么编码"这类问题。
 *
 * 级别的编号：0 是最后一级（表项指向 4KB 页），PT_LEVELS - 1 是顶层。
 */

#ifndef _HAL_PT_H_
#define _HAL_PT_H_

#include <types.h>
#include <mm/mm_types.h>
#include <mm/pgtable.h>
#include <hal/hal.h>

#if defined(ARCH_I686)
#define PT_LEVELS               2
#define PT_INDEX_BITS           10
#define PT_USER_ROOT_ENTRIES    512     /* 顶层表的前一半是用户空间 (0 - 2GB) */
#else
#define PT_LEVELS               4
#define PT_INDEX_BITS           9
#define PT_USER_ROOT_ENTRIES    256     /* 顶层表的前一半是用户空间 */
#endif
#define PT_TABLE_SIZE              (1u << PT_INDEX_BITS)
#define PT_TOP                  (PT_LEVELS - 1)

namespace pt {

/** 虚拟地址能不能出现在页表里（x86_64 要求规范地址） */
bool valid_vaddr(vaddr_t virt);

/** 这个虚拟地址在某个地址空间里归哪张顶层表管（arm64 的内核地址走 TTBR1 的那张） */
pte_t *root(hal_addr_space_t space, vaddr_t virt);

bool present(pte_t entry);
/** 表项直接指向内存（最后一级，或者上面几级的大页/块），而不是下一级表 */
bool is_leaf(pte_t entry, int level);
/** 表项里的物理地址（下一级表，或者被映射的页） */
paddr_t addr(pte_t entry);

/** 指向下一级表的表项。hal_flags 是正在建立的那个映射的标志（x86 的中间表项要带 USER 位） */
pte_t make_table(paddr_t table_phys, uint32_t hal_flags);
/** 已有的中间表项要让一个用户映射通过时怎么改（x86 加 USER 位，arm64 不用动） */
pte_t table_for_user(pte_t entry);

/** 最后一级的表项 */
pte_t make_leaf(paddr_t phys, uint32_t hal_flags);
/** 表项的属性，换算成 HAL_PAGE_* */
uint32_t leaf_flags(pte_t entry);
/** 在已有的表项上置位 / 清除一些 HAL_PAGE_* 属性，其余不动 */
pte_t apply_delta(pte_t entry, uint32_t set_flags, uint32_t clear_flags);

/** 用户页表里的纯内核映射：克隆时原样共享，不参与引用计数（只有 arm64 有这种东西） */
bool kernel_only_leaf(pte_t entry);

/** 新地址空间的顶层表（已经清零）：填上内核那一半 */
void init_root(pte_t *new_root);
/** 顶层表里新建了一项之后（i686 借此把新的内核页表记进主内核页目录） */
void top_entry_created(pte_t *root, uint32_t index);

} // namespace pt

#endif // _HAL_PT_H_
