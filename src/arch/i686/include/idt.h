// ============================================================================
// idt.h - Interrupt Descriptor Table (i686)
// ============================================================================

#ifndef _ARCH_I686_IDT_H_
#define _ARCH_I686_IDT_H_

#include <types.h>

/**
 * 中断描述符表（IDT）
 * 
 * IDT 定义了 CPU 如何响应中断和异常
 * 共支持 256 个中断向量（0-255）
 */

/**
 * IDT 表项结构（中断门描述符）
 * 共 64 位（8 字节）
 */
struct idt_entry {
    uint16_t base_low;     // 中断处理程序地址 0-15 位
    uint16_t selector;     // 代码段选择子
    uint8_t  zero;         // 必须为 0
    uint8_t  flags;        // 标志字节
    uint16_t base_high;    // 中断处理程序地址 16-31 位
} __attribute__((packed));

/**
 * IDT 指针结构
 * 用于 LIDT 指令
 */
struct idt_ptr {
    uint16_t limit;        // IDT 大小 - 1
    uint32_t base;         // IDT 起始地址
} __attribute__((packed));

/* IDT 标志 */
#define IDT_FLAG_PRESENT    0x80  // 段存在
#define IDT_FLAG_RING0      0x00  // Ring 0
#define IDT_FLAG_RING3      0x60  // Ring 3
#define IDT_FLAG_GATE_32BIT 0x0E  // 32 位中断门
#define IDT_FLAG_GATE_TRAP  0x0F  // 32 位陷阱门

/**
 * 初始化 IDT
 * 设置所有 256 个中断向量
 */
void idt_init(void);

/**
 * 设置 IDT 表项
 * @param num 中断向量号
 * @param base 中断处理程序地址
 * @param selector 代码段选择子
 * @param flags 标志字节
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

/**
 * 加载 IDT（汇编实现）
 * @param idt_ptr IDT 指针结构地址
 */
extern void idt_flush(uint32_t idt_ptr);

#endif // _ARCH_I686_IDT_H_
