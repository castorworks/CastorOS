// ============================================================================
// gdt.c - 全局描述符表实现
// ============================================================================

#include <kernel/gdt.h>
#include <lib/string.h>
#include <lib/klog.h>

/* GDT 表（6个表项：空 + 内核代码/数据 + 用户代码/数据 + TSS） */
static struct gdt_entry gdt_entries[6];
static struct gdt_ptr gdt_pointer;

/**
 * 设置 GDT 表项
 * 
 * @param num 表项索引
 * @param base 段基址
 * @param limit 段界限
 * @param access 访问字节
 * @param gran 粒度字节
 */
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, 
                         uint8_t access, uint8_t gran) {
    /* 设置段基址 */
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    /* 设置段界限 */
    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    /* 设置粒度和访问标志 */
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access = access;
}

/**
 * 初始化 GDT
 * 使用扁平内存模型：所有段基址 0，界限 4GB
 */
void gdt_init(void) {
    LOG_INFO_MSG("Initializing GDT...\n");

    /* 设置 GDT 指针 */
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gdt_pointer.base  = (uint32_t)&gdt_entries;

    /* 0: 空描述符（必须存在） */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 1: 内核代码段 */
    /* 基址 0, 界限 4GB, Ring 0, 可执行, 可读, 32位, 4KB 粒度 */
    gdt_set_gate(1, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING0 | 
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXECUTABLE | 
                 GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    /* 2: 内核数据段 */
    /* 基址 0, 界限 4GB, Ring 0, 不可执行, 可写, 32位, 4KB 粒度 */
    gdt_set_gate(2, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING0 | 
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    /* 3: 用户代码段 */
    /* 基址 0, 界限 4GB, Ring 3, 可执行, 可读, 32位, 4KB 粒度 */
    gdt_set_gate(3, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING3 | 
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXECUTABLE | 
                 GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    /* 4: 用户数据段 */
    /* 基址 0, 界限 4GB, Ring 3, 不可执行, 可写, 32位, 4KB 粒度 */
    gdt_set_gate(4, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING3 | 
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    /* 加载 GDT */
    gdt_flush((uint32_t)&gdt_pointer);

    LOG_INFO_MSG("GDT initialized successfully\n");
    LOG_DEBUG_MSG("  GDT base: %x\n", gdt_pointer.base);
    LOG_DEBUG_MSG("  GDT limit: %u bytes\n", gdt_pointer.limit + 1);
}

/**
 * 添加 TSS 描述符到 GDT
 */
 void gdt_add_tss_descriptor(uint32_t base, uint32_t limit) {
    /* TSS 描述符在索引 5 */
    gdt_set_gate(5, base, limit, GDT_ACCESS_TSS, 0x00);
    
    /* 重新加载 GDT */
    gdt_flush((uint32_t)&gdt_pointer);
    
    LOG_DEBUG_MSG("TSS descriptor added to GDT\n");
    LOG_DEBUG_MSG("  Base: %x, Limit: %x\n", base, limit);
}
