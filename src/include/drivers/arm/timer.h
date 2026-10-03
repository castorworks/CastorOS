/**
 * @file timer.h
 * @brief ARM64 Generic Timer Driver Header
 * 
 * This header defines the interface for the ARM Generic Timer driver.
 * The Generic Timer is part of the ARM architecture and provides a
 * system-wide time reference and timer functionality.
 * 
 * Requirements: 9.3 - ARM64 device discovery and drivers
 */

#ifndef _DRIVERS_ARM_TIMER_H_
#define _DRIVERS_ARM_TIMER_H_

#include <types.h>

/* ============================================================================
 * Timer IRQ Number
 * ========================================================================== */

/**
 * ARM Generic Timer physical timer IRQ number
 * 
 * On QEMU virt machine and most ARM64 systems:
 * - Physical timer: PPI 14 (IRQ 30 = 16 + 14)
 * - Virtual timer: PPI 11 (IRQ 27 = 16 + 11)
 * - Hypervisor timer: PPI 10 (IRQ 26 = 16 + 10)
 * - Secure physical timer: PPI 13 (IRQ 29 = 16 + 13)
 */
#define ARM_TIMER_PHYS_IRQ      30  /**< Physical timer IRQ (PPI 14) */
#define ARM_TIMER_VIRT_IRQ      27  /**< Virtual timer IRQ (PPI 11) */

/**
 * @brief Timer callback function type
 * @param data User data passed to callback
 */
typedef void (*timer_callback_t)(void *data);

namespace drivers {

/**
 * @brief 系统定时器（x86: PIT；ARM64: Generic Timer）
 */
class Timer {
public:
    /* ============================================================================
     * Initialization
     * ========================================================================== */

    /**
     * @brief Initialize the ARM Generic Timer
     * 
     * Configures the physical timer to generate periodic interrupts at the
     * specified frequency.
     * 
     * @param frequency Target frequency in Hz (e.g., 100 for 100 Hz / 10ms ticks)
     */
    static void init(uint32_t frequency);

    /**
     * @brief Check if timer is initialized
     * @return true if initialized, false otherwise
     */
    static bool is_initialized();

    /* ============================================================================
     * Time Queries
     * ========================================================================== */

    /**
     * @brief Get the counter frequency
     * @return Counter frequency in Hz (from CNTFRQ_EL0)
     */
    static uint64_t get_counter_frequency();

    /**
     * @brief Get the current counter value
     * @return Current counter value (from CNTPCT_EL0)
     */
    static uint64_t get_counter();

    /**
     * @brief Get system uptime in milliseconds
     * @return Milliseconds since boot
     */
    static uint64_t get_uptime_ms();

    /**
     * @brief Get system uptime in seconds
     * @return Seconds since boot
     */
    static uint32_t get_uptime_sec();

    /**
     * @brief Get timer tick count
     * @return Number of timer interrupts since initialization
     */
    static uint64_t get_ticks();

    /**
     * @brief Get timer frequency
     * @return Timer frequency in Hz
     */
    static uint32_t get_frequency();

    /* ============================================================================
     * Delay Functions
     * ========================================================================== */

    /**
     * @brief Busy-wait delay in milliseconds
     * @param ms Milliseconds to wait
     */
    static void wait(uint32_t ms);

    /**
     * @brief Busy-wait delay in microseconds
     * @param us Microseconds to wait
     */
    static void udelay(uint32_t us);

    /* ============================================================================
     * Timer Callbacks
     * ========================================================================== */

    /**
     * @brief Register a timer callback
     * 
     * @param callback Callback function
     * @param data User data passed to callback
     * @param interval_ms Interval in milliseconds
     * @param repeat Whether to repeat (true) or one-shot (false)
     * @return Timer ID (1-based), or 0 on failure
     */
    static uint32_t register_callback(timer_callback_t callback, void *data,
                                      uint32_t interval_ms, bool repeat);

    /**
     * @brief Unregister a timer callback
     * @param timer_id Timer ID to unregister
     * @return true on success, false on failure
     */
    static bool unregister_callback(uint32_t timer_id);

    /**
     * @brief Get number of active timer callbacks
     * @return Number of active timers
     */
    static uint32_t get_active_count();

    /* ============================================================================
     * Timer Control
     * ========================================================================== */

    /**
     * @brief Enable the timer
     */
    static void enable();

    /**
     * @brief Disable the timer
     */
    static void disable();

    /**
     * @brief Check if timer interrupt is pending
     * @return true if interrupt is pending, false otherwise
     */
    static bool interrupt_pending();

    /**
     * @brief Mask the timer interrupt
     */
    static void mask_interrupt();

    /**
     * @brief Unmask the timer interrupt
     */
    static void unmask_interrupt();

    /* ============================================================================
     * IRQ Handler (called from interrupt context)
     * ========================================================================== */

    /**
     * @brief Timer interrupt handler
     * 
     * Called from the GIC interrupt handler when the timer interrupt fires.
     * Increments the tick counter, reloads the timer, and processes callbacks.
     */
    static void irq_handler();
};

} // namespace drivers

#endif /* _DRIVERS_ARM_TIMER_H_ */
