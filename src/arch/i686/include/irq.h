// ============================================================================
// irq.h - Hardware Interrupt Requests (i686)
// ============================================================================

#ifndef _ARCH_I686_IRQ_H_
#define _ARCH_I686_IRQ_H_

#include <types.h>
#include <isr.h>

/**
 * 硬件中断请求（IRQ）
 * 
 * 处理外部设备中断（32-47 号中断）
 * 需要重映射 PIC 以避免与 CPU 异常冲突
 */

/* IRQ 号定义（重映射后） */
#define IRQ0  32    // 定时器
#define IRQ1  33    // 键盘
#define IRQ2  34    // 级联（从 PIC）
#define IRQ3  35    // COM2/COM4
#define IRQ4  36    // COM1/COM3
#define IRQ5  37    // LPT2
#define IRQ6  38    // 软盘
#define IRQ7  39    // LPT1
#define IRQ8  40    // 实时时钟（RTC）
#define IRQ9  41    // 自由
#define IRQ10 42    // 自由
#define IRQ11 43    // 自由
#define IRQ12 44    // PS/2 鼠标
#define IRQ13 45    // 数学协处理器
#define IRQ14 46    // 主 IDE
#define IRQ15 47    // 副 IDE

/**
 * 初始化 IRQ
 * 重映射 PIC 并注册所有 IRQ 处理程序
 */
void irq_init(void);

/**
 * 注册 IRQ 处理函数
 * @param irq IRQ 号（0-15）
 * @param handler 处理函数
 */
void irq_register_handler(uint8_t irq, isr_handler_t handler);

/**
 * 禁用（屏蔽）指定 IRQ 线路
 * @param irq IRQ 号（0-15）
 */
void irq_disable_line(uint8_t irq);

/**
 * 启用（取消屏蔽）指定 IRQ 线路
 * @param irq IRQ 号（0-15）
 */
void irq_enable_line(uint8_t irq);

/* IRQ 处理程序（汇编入口点） */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/**
 * 获取特定 IRQ 的触发次数
 * @param irq IRQ 号 (0-15)
 * @return 触发次数
 */
uint64_t irq_get_count(uint8_t irq);

 /**
  * 获取定时器滴答数
  * @return 定时器滴答数
  */
uint64_t irq_get_timer_ticks(void);

#endif // _ARCH_I686_IRQ_H_
