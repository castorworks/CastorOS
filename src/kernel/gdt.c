#include <kernel/gdt.h>
#include <lib/string.h>
#include <lib/klog.h>

/* GDT 表（6个表项：空 + 内核代码/数据 + 用户代码/数据 + TSS） */
static struct gdt_entry gdt_entries[6];
static struct gdt_ptr gdt_pointer;
static tss_entry_t tss;

/* 声明汇编函数 */
extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_load(uint16_t tss_selector);

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, 
                         uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= (gran & 0xF0);

    gdt_entries[num].access = access;
}

/* 在内存中构建 GDT（含 TSS descriptor），但不执行 lgdt */
static void gdt_build_with_tss(void) {
    /* 0..4 跟你原来一致 */
    gdt_set_gate(0, 0, 0, 0, 0);

    gdt_set_gate(1, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING0 |
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXECUTABLE |
                 GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    gdt_set_gate(2, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING0 |
                 /* data desc */ GDT_ACCESS_CODE_DATA | GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    gdt_set_gate(3, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING3 |
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXECUTABLE |
                 GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    gdt_set_gate(4, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_PRIV_RING3 |
                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_READABLE,
                 GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    /* TSS descriptor 在索引 5 - 这里暂用 base=0 limit=0，实际在 tss_init 时写入 */
    /* 为安全起见，这里先写一个空 TSS descriptor（会被 write_tss 覆盖） */
    gdt_set_gate(5, 0, 0, GDT_ACCESS_TSS, 0x00);

    /* 准备 gdt_pointer（还未 lgdt）*/
    gdt_pointer.limit = sizeof(gdt_entries) - 1;
    gdt_pointer.base  = (uint32_t)&gdt_entries;
}

/* 把内存中的 GDT 加载到 GDTR 并刷新段寄存器 */
void gdt_install(void) {
    gdt_flush((uint32_t)&gdt_pointer);
}

/* 写入 TSS 描述符（覆盖 GDT[5]）*/
void gdt_write_tss_descriptor(uint32_t base, uint32_t limit) {
    gdt_set_gate(5, base, limit, GDT_ACCESS_TSS, 0x00);
}

/* TSS 初始化（在调用 gdt_build_with_tss 之后） */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss) {
    memset(&tss, 0, sizeof(tss));

    tss.ss0 = kernel_ss;
    tss.esp0 = kernel_stack;
    tss.iomap_base = sizeof(tss);

    LOG_DEBUG_MSG("  TSS addr=%x size=%u\n", (uint32_t)&tss, (uint32_t)sizeof(tss));
}

/**
 * 更新 TSS 的内核栈指针（ESP0）
 * 当内核栈发生变化时可以调用此函数
 *
 * @param kernel_stack 新的内核栈顶地址
 */
void tss_set_kernel_stack(uint32_t kernel_stack) {
    tss.esp0 = kernel_stack;
}

/* 供外部读取 TSS 地址/大小 */
uint32_t tss_get_address(void) { return (uint32_t)&tss; }
uint32_t tss_get_size(void)    { return (uint32_t)sizeof(tss); }

/* 一次性初始化接口（推荐） */
void gdt_init_all_with_tss(uint32_t kernel_stack, uint16_t kernel_ss) {
    gdt_build_with_tss();
    tss_init(kernel_stack, kernel_ss);
    gdt_write_tss_descriptor(tss_get_address(), tss_get_size() - 1);
    gdt_install(); /* lgdt & reload segments */
    /* TSS selector = index 5 << 3 = 0x28 */
    tss_load( (5 << 3) );
    LOG_INFO_MSG("GDT+TSS installed and loaded\n");
}
