#ifndef _KERNEL_INTERRUPT_H_
#define _KERNEL_INTERRUPT_H_

#include <types.h>

/**
 * 禁用中断
 * @return 之前的中断标志状态
 */
static inline bool interrupts_disable(void) __attribute__((unused));
static inline bool interrupts_disable(void) {
#if defined(ARCH_X86_64)
    uint64_t rflags;
    __asm__ volatile("pushfq; popq %0" : "=r"(rflags));
    __asm__ volatile("cli");
    return (rflags & 0x200) != 0;  // IF 标志
#elif defined(ARCH_ARM64)
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    return (daif & 0x80) == 0;  // IRQ 未屏蔽
#else
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));
    __asm__ volatile("cli");
    return (eflags & 0x200) != 0;  // IF 标志
#endif
}

/**
 * 启用中断
 */
static inline void interrupts_enable(void) __attribute__((unused));
static inline void interrupts_enable(void) {
#if defined(ARCH_ARM64)
    __asm__ volatile("msr daifclr, #0xf" ::: "memory");
#else
    __asm__ volatile("sti");
#endif
}

/**
 * 恢复中断状态
 * @param state 之前保存的状态
 */
static inline void interrupts_restore(bool state) __attribute__((unused));
static inline void interrupts_restore(bool state) {
    if (state) {
        interrupts_enable();
    }
}

/**
 * 标记进入中断上下文
 * 嵌套中断会增加计数
 */
void interrupt_enter(void);

/**
 * 标记退出中断上下文
 * 计数归零后视为离开中断
 */
void interrupt_exit(void);

/**
 * 判断当前是否位于中断上下文
 */
bool in_interrupt(void);

#endif // _KERNEL_INTERRUPT_H_
