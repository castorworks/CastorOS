#ifndef _KERNEL_INTERRUPT_H_
#define _KERNEL_INTERRUPT_H_

#include <types.h>

/**
 * 标记进入中断上下文
 * 嵌套中断会增加计数
 */
extern "C" void interrupt_enter(void);

/**
 * 标记退出中断上下文
 * 计数归零后视为离开中断
 */
extern "C" void interrupt_exit(void);

/**
 * 判断当前是否位于中断上下文
 */
bool in_interrupt(void);

/**
 * 抢占点专用：把当前的中断嵌套计数取走并清零 / 之后放回。
 *
 * 计数是全局的，而它描述的是“当前这条执行流在某个中断帧里”。在中断返回
 * 用户态的抢占点切换任务时，这一层计数属于被换下的任务的中断帧，不能带给
 * 换上来的任务（否则它的 Mutex::lock 会被当成在中断里睡眠）；该任务被换回来、
 * 继续走完中断返回路径之前再放回去。
 */
uint32_t interrupt_depth_suspend(void);
void interrupt_depth_resume(uint32_t depth);

/**
 * 在可能睡眠的操作入口调用（Mutex::lock、Semaphore::wait、Scheduler::block/sleep）。
 * 中断上下文里不允许睡眠：被打断的任务还没有保存成可恢复的状态，
 * 而且按任务身份判定持有者的锁会把中断误认成被打断的任务。
 *
 * @param what 操作名，用于诊断输出
 */
void assert_may_sleep(const char *what);

namespace kernel {

/**
 * @brief CPU 中断开关（保存/恢复中断状态）
 */
class Interrupts {
public:
    /**
     * 禁用中断
     * @return 之前的中断标志状态
     */
    static inline bool disable() {
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
    static inline void enable() {
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
    static inline void restore(bool state) {
        if (state) {
            kernel::Interrupts::enable();
        }
    }
};

/**
 * @brief 作用域内关中断，离开时恢复进入前的状态
 *
 * 用于必须不被中断处理函数打断的短临界区（例如一次完整的控制台输出）。
 */
class InterruptGuard {
public:
    InterruptGuard() : was_enabled_(Interrupts::disable()) {}
    ~InterruptGuard() { Interrupts::restore(was_enabled_); }
    InterruptGuard(const InterruptGuard &) = delete;
    InterruptGuard &operator=(const InterruptGuard &) = delete;
private:
    bool was_enabled_;
};

} // namespace kernel

#endif // _KERNEL_INTERRUPT_H_
