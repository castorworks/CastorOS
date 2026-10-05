/**
 * @file timer.cpp
 * @brief ARM Generic Timer driver (EL1 physical timer)
 */

#include <drivers/arm/timer.h>
#include <types.h>

#define CNTP_CTL_ENABLE     (1ULL << 0)

static inline uint64_t read_cntfrq_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_cntpct_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline void write_cntp_tval_el0(uint64_t val) {
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(val));
}

static inline void write_cntp_ctl_el0(uint64_t val) {
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(val));
}

static uint64_t counter_frequency = 0;      /* counter Hz (CNTFRQ_EL0) */
static uint32_t timer_frequency = 0;        /* tick Hz */
static uint64_t ticks_per_interrupt = 0;
static volatile uint64_t timer_ticks = 0;
static uint64_t boot_counter_value = 0;

void drivers::Timer::init(uint32_t frequency) {
    counter_frequency = read_cntfrq_el0();
    if (counter_frequency == 0 || frequency == 0) {
        return;
    }

    timer_frequency = frequency;
    ticks_per_interrupt = counter_frequency / frequency;
    boot_counter_value = read_cntpct_el0();

    write_cntp_ctl_el0(0);
    write_cntp_tval_el0(ticks_per_interrupt);
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
}

uint64_t drivers::Timer::get_uptime_ms() {
    if (counter_frequency == 0) {
        return 0;
    }
    uint64_t elapsed = read_cntpct_el0() - boot_counter_value;
    return elapsed / (counter_frequency / 1000);
}

uint32_t drivers::Timer::get_frequency() {
    return timer_frequency;
}

void drivers::Timer::irq_handler() {
    timer_ticks = timer_ticks + 1;
    write_cntp_tval_el0(ticks_per_interrupt);
}
