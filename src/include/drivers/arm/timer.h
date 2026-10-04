#ifndef _DRIVERS_ARM_TIMER_H_
#define _DRIVERS_ARM_TIMER_H_

#include <types.h>

#define ARM_TIMER_PHYS_IRQ      30  /**< Physical timer IRQ (PPI 14) */

namespace drivers {

/**
 * System clock: ARM Generic Timer (EL1 physical timer).
 * The IRQ itself is wired up by hal::Timer::init().
 */
class Timer {
public:
    /** Program the timer to fire at `frequency` Hz */
    static void init(uint32_t frequency);

    /** Milliseconds since init() */
    static uint64_t get_uptime_ms();

    /** Ticks since init() */
    static uint64_t get_ticks();

    /** Tick frequency in Hz */
    static uint32_t get_frequency();

    /** Called from the timer IRQ: count the tick and re-arm the timer */
    static void irq_handler();
};

} // namespace drivers

#endif /* _DRIVERS_ARM_TIMER_H_ */
