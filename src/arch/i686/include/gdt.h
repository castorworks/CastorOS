// ============================================================================
// gdt.h - Global Descriptor Table & Task State Segment (i686)
// ============================================================================

#ifndef _ARCH_I686_GDT_H_
#define _ARCH_I686_GDT_H_

#include <types.h>

// ---------------------------------------------------------------------------
// 段选择子（Selector）
// ---------------------------------------------------------------------------
#define GDT_KERNEL_CODE_SEGMENT  0x08
#define GDT_KERNEL_DATA_SEGMENT  0x10
#define GDT_USER_CODE_SEGMENT    0x18
#define GDT_USER_DATA_SEGMENT    0x20
#define GDT_TSS_SEGMENT          0x28

// ---------------------------------------------------------------------------
// GDT Entry (8 bytes, packed)
// ---------------------------------------------------------------------------
struct gdt_entry {
    uint16_t limit_low;     // 段界限 0:15
    uint16_t base_low;      // 段基址 0:15
    uint8_t  base_middle;   // 段基址 16:23
    uint8_t  access;        // Access Byte
    uint8_t  granularity;   // G | D | 0 | AVL | limit(16:19)
    uint8_t  base_high;     // 段基址 24:31
} __attribute__((packed));

// ---------------------------------------------------------------------------
// GDTR
// ---------------------------------------------------------------------------
struct gdt_ptr {
    uint16_t limit;  // GDT 大小 - 1
    uint32_t base;   // GDT 基地址
} __attribute__((packed));

// ---------------------------------------------------------------------------
// TSS 结构（32-bit x86 标准格式）
// 不使用硬件任务切换，仅用于特权级转换（用户→内核）
// ---------------------------------------------------------------------------
typedef struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;          // 内核栈指针（进入内核时使用）
    uint32_t ss0;           // 内核栈段
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

// ---------------------------------------------------------------------------
// Access 字节
// ---------------------------------------------------------------------------
// 通用标志
#define GDT_ACCESS_PRESENT       0x80
#define GDT_ACCESS_PRIV_RING0    0x00
#define GDT_ACCESS_PRIV_RING3    0x60
#define GDT_ACCESS_CODE_DATA     0x10
#define GDT_ACCESS_EXECUTABLE    0x08
#define GDT_ACCESS_DIRECTION     0x04
#define GDT_ACCESS_READABLE      0x02
#define GDT_ACCESS_WRITABLE      0x02
#define GDT_ACCESS_ACCESSED      0x01

// TSS Access 字节（0x89 = 10001001b = Present + System + 32-bit TSS）
#define GDT_ACCESS_TSS           0x89

// ---------------------------------------------------------------------------
// Granularity 字节
// ---------------------------------------------------------------------------
#define GDT_GRANULARITY_4K       0x80
#define GDT_GRANULARITY_32BIT    0x40

// ---------------------------------------------------------------------------
// API（gdt.c 提供实现）
// ---------------------------------------------------------------------------

/* 一次性初始化 GDT 和 TSS */
void gdt_init_all_with_tss(uint32_t kernel_stack, uint16_t kernel_ss);

// 添加 TSS 描述符
void gdt_add_tss_descriptor(uint32_t base, uint32_t limit);

// TSS 相关
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss);

void tss_set_kernel_stack(uint32_t kernel_stack);

uint32_t tss_get_address(void);
uint32_t tss_get_size(void);

// 汇编接口
extern void gdt_flush(uint32_t gdt_ptr);
extern void tss_flush(uint16_t selector);

#endif // _ARCH_I686_GDT_H_
