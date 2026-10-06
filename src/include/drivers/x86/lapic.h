#ifndef _DRIVERS_X86_LAPIC_H_
#define _DRIVERS_X86_LAPIC_H_

#include <types.h>

/**
 * @file lapic.h
 * @brief Local APIC：每个 CPU 自己的中断控制器
 *
 * 只在有几个 CPU 的时候用到它（kernel/smp.h），而且只用三样：
 *   - 启动别的 CPU（INIT 和 SIPI 两种"处理器间中断"）；
 *   - 其余 CPU 的时钟：PIT 的中断经 8259 只送到启动 CPU，别的 CPU 用自己 Local APIC 里
 *     的定时器；
 *   - 应答上面那个时钟中断。
 * 设备中断仍然走 8259（PIC），只到启动 CPU：它的 Local APIC 把 LINT0 设成"直通外部中断"。
 * 寄存器是一页内存（物理地址 0xFEE00000），各架构负责把它映射进内核。
 */

/** Local APIC 定时器用的中断向量（紧跟在 PIC 的 32-47 后面） */
#define LAPIC_TIMER_VECTOR      64
/** 伪中断向量：硬件规定要给一个，处理函数什么都不做 */
#define LAPIC_SPURIOUS_VECTOR   255
/** 寄存器的物理地址 */
#define LAPIC_PHYS_BASE         0xFEE00000u

namespace drivers {

class Lapic {
public:
    /** 这个 CPU 有没有 Local APIC（CPUID） */
    static bool available();

    /** 这个 CPU 的 APIC ID。用 CPUID 取，不需要寄存器已经映射好 */
    static uint32_t id();

    /** 寄存器映射在哪里（虚拟地址）。启动 CPU 调用一次，之后别的函数才能用 */
    static void set_base(uintptr_t virt);

    /**
     * 打开当前 CPU 的 Local APIC。
     * @param boot_cpu 是启动 CPU：PIC 的中断要继续经它送进来
     */
    static void enable(bool boot_cpu);

    /**
     * 让 APIC ID 是 apic_id 的 CPU 从物理地址 start_page（低于 1MB、页对齐）开始执行：
     * INIT，然后两次 SIPI。只是发出去，不等它真的起来
     */
    static void start_cpu(uint32_t apic_id, uint32_t start_page);

    /** 量一下定时器走得多快（启动 CPU 调用一次，要开着中断：靠 PIT 的时钟来量） */
    static void timer_calibrate();

    /** 当前 CPU 的定时器开始以 hz 的频率发 LAPIC_TIMER_VECTOR 号中断 */
    static void timer_start(uint32_t hz);

    /** 应答当前 CPU 正在处理的 Local APIC 中断 */
    static void eoi();
};

} // namespace drivers

#endif // _DRIVERS_X86_LAPIC_H_
