// ============================================================================
// timer.cpp - 可编程间隔定时器（PIT）驱动
// ============================================================================

#include <drivers/timer.h>
#include <kernel/io.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/task.h>
#include <lib/klog.h>

static volatile uint64_t timer_ticks = 0;  // 中断处理程序会修改
static uint32_t timer_frequency = 0;       // Hz

static void timer_irq(registers_t *regs) {
    (void)regs;
    timer_ticks = timer_ticks + 1;
    kernel::Scheduler::timer_tick();
}

void drivers::Timer::init(uint32_t frequency) {
    uint32_t divisor = PIT_FREQUENCY / frequency;
    if (divisor > 65535) {
        divisor = 65535;
    } else if (divisor < 1) {
        divisor = 1;
    }
    timer_frequency = PIT_FREQUENCY / divisor;

    outb(PIT_COMMAND, PIT_CMD_INIT);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register_handler(0, timer_irq);

    LOG_INFO_MSG("PIT initialized (%u Hz)\n", timer_frequency);
}

uint64_t drivers::Timer::get_ticks() {
    return timer_ticks;
}

uint32_t drivers::Timer::get_frequency() {
    return timer_frequency;
}

uint64_t drivers::Timer::get_uptime_ms() {
    if (timer_frequency == 0) {
        return 0;
    }
    return (timer_ticks * 1000) / timer_frequency;
}
