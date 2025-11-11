#ifndef _KERNEL_GDT_H_
#define _KERNEL_GDT_H_

#include <types.h>

/**
 * 全局描述符表（GDT）
 * 
 * GDT 定义了内存段的属性和访问权限
 * x86 保护模式必须使用分段机制，即使我们主要依赖分页
 */

/**
 * GDT 表项结构（段描述符）
 * 共 64 位（8 字节）
 */
struct gdt_entry {
    uint16_t limit_low;    // 段界限 0-15 位
    uint16_t base_low;     // 段基址 0-15 位
    uint8_t  base_middle;  // 段基址 16-23 位
    uint8_t  access;       // 访问字节
    uint8_t  granularity;  // 粒度字节（包含界限 16-19 位和标志）
    uint8_t  base_high;    // 段基址 24-31 位
} __attribute__((packed));

/**
 * GDT 指针结构
 * 用于 LGDT 指令
 */
struct gdt_ptr {
    uint16_t limit;        // GDT 大小 - 1
    uint32_t base;         // GDT 起始地址
} __attribute__((packed));

/* GDT 访问字节标志 */
#define GDT_ACCESS_PRESENT      0x80  // 段存在
#define GDT_ACCESS_PRIV_RING0   0x00  // Ring 0 (内核)
#define GDT_ACCESS_PRIV_RING3   0x60  // Ring 3 (用户)
#define GDT_ACCESS_CODE_DATA    0x10  // 代码/数据段
#define GDT_ACCESS_EXECUTABLE   0x08  // 可执行（代码段）
#define GDT_ACCESS_READABLE     0x02  // 代码段可读 / 数据段可写
#define GDT_ACCESS_ACCESSED     0x01  // CPU 访问标志

/* GDT 粒度字节标志 */
#define GDT_GRANULARITY_4K      0x80  // 4KB 粒度
#define GDT_GRANULARITY_32BIT   0x40  // 32 位保护模式

/* TSS 访问字节 */
#define GDT_ACCESS_TSS          0x89  // Present, Ring 0, TSS (32-bit available)

/* 段选择子 */
#define GDT_KERNEL_CODE_SEGMENT 0x08  // 内核代码段选择子（索引 1）
#define GDT_KERNEL_DATA_SEGMENT 0x10  // 内核数据段选择子（索引 2）
#define GDT_USER_CODE_SEGMENT   0x18  // 用户代码段选择子（索引 3）
#define GDT_USER_DATA_SEGMENT   0x20  // 用户数据段选择子（索引 4）
#define GDT_TSS_SEGMENT         0x28  // TSS 段选择子（索引 5）

/**
 * 初始化 GDT
 * 设置扁平内存模型（所有段基址 0，界限 4GB）
 */
void gdt_init(void);

/**
 * 加载 GDT（汇编实现）
 * @param gdt_ptr GDT 指针结构地址
 */
extern void gdt_flush(uint32_t gdt_ptr);

/**
 * 添加 TSS 描述符到 GDT
 * @param base TSS 结构的基址
 * @param limit TSS 结构的大小
 */
void gdt_add_tss_descriptor(uint32_t base, uint32_t limit);

#endif // _KERNEL_GDT_H_
