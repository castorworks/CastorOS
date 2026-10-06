#ifndef _KERNEL_SMP_H_
#define _KERNEL_SMP_H_

#include <types.h>

/**
 * @file smp.h
 * @brief 多个 CPU：每个 CPU 自己的那点状态，和一把内核大锁
 *
 * 内核原本的假设是只有一个 CPU：关了中断就没有别人。多个 CPU 的时候这个假设靠一把锁
 * 留住：任何 CPU 执行内核代码之前都要拿到内核锁（kernel::KernelLock），所以同一时刻
 * 仍然只有一个 CPU 在内核里，内核里的数据结构不用逐个加锁。用户态的代码不受这把锁
 * 约束，几个 CPU 可以同时运行几个用户进程——这是多 CPU 带来的全部好处，也是这种做法的
 * 上限：内核本身不并行。
 *
 * 锁在这些时候拿和放：
 *   - 从用户态进内核（中断、异常、要拿锁的系统调用）时拿，回用户态之前放。
 *   - 中断打断的是已经拿着锁的内核代码：只是计数加一，回去时减一。
 *   - 中断打断的是没拿锁的内核代码（idle 的等待、不拿锁的系统调用）：处理函数自己拿、
 *     自己放。
 * 锁跟着任务走：一个拿着锁的任务被换下 CPU 时（它在内核里阻塞了，或者时间片用完了），
 * 调度器记下它拿着几层，把锁放掉；它被换回来时——可能在另一个 CPU 上——再拿回来。
 * 所以锁保证的是"同一时刻只有一个正在运行的任务在拿着它执行内核代码"，睡着的任务不占它。
 *
 * 不是所有内核代码都在这把锁里：一部分系统调用（内存、IPC）不拿它，调度器也不靠它
 * （调度器有自己的锁，见 sched.cpp）。见 docs/reference/smp.md。
 *
 * 每个 CPU 有一个编号（hal::Cpu::id()，从 0 开始，0 是启动时的那个）。"当前任务"、
 * idle 任务、中断嵌套计数这些东西每个 CPU 各有一份，按编号放在数组里。
 */

/** 最多支持多少个 CPU */
#define MAX_CPUS    8

namespace kernel {

class KernelLock {
public:
    /** 进内核：这个 CPU 还没拿着锁就去拿（等到拿到为止），已经拿着就只把嵌套计数加一 */
    static void enter();

    /** 和 enter 配对：嵌套计数减一，减到 0 就放锁 */
    static void leave();

    /**
     * 把锁放掉，不管嵌套了几层。用在两个地方：即将回到用户态时，和 idle 任务停下来等中断
     * 之前。这两个时候这个 CPU 一定不该再拿着锁；用"清零"而不是"减一"，是因为有的路径
     * 进来了就不再按原路回去（进程在异常处理里退出，换上来的是别的任务）。
     */
    static void release();

    /** 这个 CPU 现在拿着几层（0 = 没拿） */
    static uint32_t depth();

    /**
     * 任务切换用：让这个 CPU 拿锁的层数变成 depth。原来拿着、现在不该拿就放；原来没拿、
     * 现在该拿就去拿（等到拿到为止）。换上来的任务被换下去时拿着几层，现在就恢复成几层。
     */
    static void adopt(uint32_t depth);
};

class Smp {
public:
    /** 把其余的 CPU 启动起来，让它们各自进入自己的 idle 任务。在启动 CPU 上调用一次 */
    static void start_secondaries();

    /** 正在运行的 CPU 个数（包括启动时的那个） */
    static uint32_t cpu_count();

    /** 一个刚启动的 CPU 的 C 入口（由各架构的启动代码调用，不返回） */
    static void secondary_main() __attribute__((noreturn));
};

} // namespace kernel

#endif // _KERNEL_SMP_H_
