#ifndef _KERNEL_TSS_H_
#define _KERNEL_TSS_H_

#include <types.h>

/**
 * 任务状态段（TSS）
 * 
 * TSS 用于硬件任务切换和特权级切换
 * 在 CastorOS 中主要用于：
 * 1. 存储内核栈指针（从用户态返回内核态时使用）
 * 2. 为后续的多任务做准备
 */

/**
 * TSS 结构
 * 32 位保护模式 TSS 布局
 */
typedef struct {
    uint16_t prev_tss;   // 前一个 TSS 的选择子（硬件任务切换用）
    uint16_t __reserved0;
    uint32_t esp0;       // Ring 0 栈指针
    uint16_t ss0;        // Ring 0 栈段选择子
    uint16_t __reserved1;
    uint32_t esp1;       // Ring 1 栈指针
    uint16_t ss1;        // Ring 1 栈段选择子
    uint16_t __reserved2;
    uint32_t esp2;       // Ring 2 栈指针
    uint16_t ss2;        // Ring 2 栈段选择子
    uint16_t __reserved3;
    uint32_t cr3;        // 页目录基址（PDBR）
    uint32_t eip;        // 指令指针
    uint32_t eflags;     // 标志寄存器
    uint32_t eax;        // 通用寄存器
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;        // 栈指针
    uint32_t ebp;        // 基址指针
    uint32_t esi;        // 源索引
    uint32_t edi;        // 目标索引
    uint16_t es;         // 段寄存器
    uint16_t __reserved4;
    uint16_t cs;
    uint16_t __reserved5;
    uint16_t ss;
    uint16_t __reserved6;
    uint16_t ds;
    uint16_t __reserved7;
    uint16_t fs;
    uint16_t __reserved8;
    uint16_t gs;
    uint16_t __reserved9;
    uint16_t ldt;        // LDT 段选择子
    uint16_t __reserved10;
    uint16_t trap;       // 陷阱标志
    uint16_t iomap_base; // I/O 位图基址
} __attribute__((packed)) tss_entry_t;

/**
 * 初始化 TSS
 * @param kernel_stack 内核栈顶地址
 * @param kernel_ss 内核栈段选择子
 */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss);

/**
 * 设置内核栈
 * 用于任务切换时更新内核栈
 * @param kernel_stack 新的内核栈顶地址
 */
void tss_set_kernel_stack(uint32_t kernel_stack);

/**
 * 获取 TSS 结构地址
 * @return TSS 结构的地址
 */
uint32_t tss_get_address(void);

/**
 * 获取 TSS 大小
 * @return TSS 结构的大小
 */
uint32_t tss_get_size(void);

/**
 * 加载 TSS 到任务寄存器，汇编实现
 * @param tss_selector TSS 段选择子
 */
void tss_flush(uint16_t tss_selector);

#endif // _KERNEL_TSS_H_
