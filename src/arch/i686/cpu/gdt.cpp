// ============================================================================
// gdt.c - Global Descriptor Table 实现 (i686)
// ============================================================================

#include <hal/hal.h>
#include <kernel/smp.h>
#include <kernel/gdt.h>
#include <lib/string.h>
#include <lib/klog.h>

/* 每个 CPU 有自己的 GDT 和 TSS（kernel/smp.h）：TSS 里记着这个 CPU 从用户态进内核时换到
 * 哪个内核栈，后面跟着它正在运行的任务的 I/O 许可位图，这两样每个 CPU 都不一样。
 * GDT 里只有 TSS 那一项因此不同。
 *
 * I/O 许可位图紧跟在 TSS 后面：每个端口一位，0 表示用户态可以直接用 in/out 访问这个端口，
 * 1 表示不行（#GP）。CPU 一次最多查跨两个字节的位，所以位图后面还要多一个全 1 的字节。
 * 平时全是 1；换一个用户任务上 CPU 时，把许可给它的端口打开（tss_io_allow）。 */
#define IOMAP_BYTES (65536 / 8)
static struct cpu_tables {
    struct gdt_entry gdt[5 + MAX_CPUS]; /* 空 + 内核代码/数据 + 用户代码/数据，后面是 TSS：
                                         * 每个 CPU 的 TSS 描述符放在不同的位置上 */
    struct gdt_ptr pointer;
    struct {
        tss_entry_t tss;
        uint8_t iomap[IOMAP_BYTES + 1];
    } __attribute__((packed)) tss_area;
} tables[MAX_CPUS];

/* 每个 CPU 把自己的 TSS 描述符放在自己的位置上（第 5 + cpu 项），所以每个 CPU 的任务
 * 寄存器里的选择子都不一样。CPU 就靠它知道自己是哪一个（gdt_current_cpu）：读任务寄存器
 * 只是一条很便宜的指令 */
#define TSS_INDEX(cpu)      (5 + (cpu))
#define TSS_SELECTOR(cpu)   ((uint16_t)(TSS_INDEX(cpu) << 3))

uint32_t gdt_current_cpu(void) {
    uint16_t selector;
    __asm__ volatile("str %0" : "=r"(selector));
    // 还什么都没装（刚启动）：那就是启动 CPU
    return selector < TSS_SELECTOR(0) ? 0 : (uint32_t)(selector - TSS_SELECTOR(0)) / 8;
}

/* gdt_init_cpu 正在给哪个 CPU 建表：它的任务寄存器装好之前，这个 CPU 分不清自己是谁。
 * 其余时候是 -1 */
static int init_cpu = -1;

/* 下面这些名字指的都是"当前这个 CPU 的" */
#define this_cpu_tables (tables[init_cpu >= 0 ? (uint32_t)init_cpu : gdt_current_cpu()])
#define gdt_entries (this_cpu_tables.gdt)
#define gdt_pointer (this_cpu_tables.pointer)
#define tss_area    (this_cpu_tables.tss_area)
#define tss         (tss_area.tss)

/* 声明汇编函数 */
extern "C" void gdt_flush(uint32_t gdt_ptr_addr);
extern "C" void tss_load(uint16_t tss_selector);

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

    /* 准备 gdt_pointer（还未 lgdt）*/
    gdt_pointer.limit = sizeof(gdt_entries) - 1;
    gdt_pointer.base  = (uint32_t)&gdt_entries;
}

/* 把内存中的 GDT 加载到 GDTR 并刷新段寄存器 */
void gdt_install(void) {
    gdt_flush((uint32_t)&gdt_pointer);
}

/* 写入 TSS 描述符（覆盖 GDT[5]）*/
static void gdt_write_tss_descriptor(uint32_t cpu, uint32_t base, uint32_t limit) {
    gdt_set_gate(TSS_INDEX(cpu), base, limit, GDT_ACCESS_TSS, 0x00);
}

/* TSS 初始化（在调用 gdt_build_with_tss 之后） */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss) {
    memset(&tss, 0, sizeof(tss));
    memset(tss_area.iomap, 0xFF, sizeof(tss_area.iomap));

    tss.ss0 = kernel_ss;
    tss.esp0 = kernel_stack;
    tss.iomap_base = sizeof(tss);       // 位图紧跟在 TSS 后面

    LOG_DEBUG_MSG("  TSS addr=0x%x size=%u\n", (uint32_t)&tss, (uint32_t)sizeof(tss));
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
uint32_t tss_get_size(void)    { return (uint32_t)sizeof(tss_area); }      // 含 I/O 许可位图

void tss_io_allow(uint32_t first, uint32_t count, bool allow) {
    for (uint32_t port = first; port < first + count && port < 65536; port++) {
        if (allow) {
            tss_area.iomap[port / 8] &= (uint8_t)~(1u << (port % 8));
        } else {
            tss_area.iomap[port / 8] |= (uint8_t)(1u << (port % 8));
        }
    }
}

/* 一次性初始化接口（推荐） */
void gdt_init_cpu(uint32_t cpu, uint32_t kernel_stack, uint16_t kernel_ss) {
    init_cpu = (int)cpu;
    gdt_build_with_tss();
    tss_init(kernel_stack, kernel_ss);
    gdt_write_tss_descriptor(cpu, tss_get_address(), tss_get_size() - 1);
    gdt_install(); /* lgdt & reload segments */
    tss_load(TSS_SELECTOR(cpu));
    init_cpu = -1;      // 从这里起，任务寄存器说明了我们是谁
    LOG_INFO_MSG("GDT+TSS installed and loaded for CPU %u\n", cpu);
}
