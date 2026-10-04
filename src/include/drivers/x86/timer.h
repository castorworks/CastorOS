#ifndef _DRIVERS_X86_TIMER_H_
#define _DRIVERS_X86_TIMER_H_

#include <types.h>

/* PIT 端口 */
#define PIT_CHANNEL0    0x40    // 通道 0 数据端口
#define PIT_COMMAND     0x43    // 命令寄存器

/* PIT 输入频率（约 1.19 MHz） */
#define PIT_FREQUENCY   1193182

/* 通道 0，先低后高字节，模式 3（方波），二进制 */
#define PIT_CMD_INIT    0x36

namespace drivers {

/**
 * 系统时钟：PIT 通道 0，每个 tick 驱动一次调度器
 */
class Timer {
public:
    /** 以 frequency Hz 启动时钟中断 (IRQ 0) */
    static void init(uint32_t frequency);

    /** 启动以来的毫秒数 */
    static uint64_t get_uptime_ms();

    /** 启动以来的 tick 数 */
    static uint64_t get_ticks();

    /** 实际的 tick 频率 (Hz) */
    static uint32_t get_frequency();
};

} // namespace drivers

#endif // _DRIVERS_X86_TIMER_H_
