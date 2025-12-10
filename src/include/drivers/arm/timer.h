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
void timer_init(uint32_t frequency);

/**
 * @brief Check if timer is initialized
 * @return true if initialized, false otherwise
 */
bool timer_is_initialized(void);

/* ============================================================================
 * Time Queries
 * ========================================================================== */

/**
 * @brief Get the counter frequency
 * @return Counter frequency in Hz (from CNTFRQ_EL0)
 */
uint64_t timer_get_counter_frequency(void);

/**
 * @brief Get the current counter value
 * @return Current counter value (from CNTPCT_EL0)
 */
uint64_t timer_get_counter(void);

/**
 * @brief Get system uptime in milliseconds
 * @return Milliseconds since boot
 */
uint64_t timer_get_uptime_ms(void);

/**
 * @brief Get system uptime in seconds
 * @return Seconds since boot
 */
uint32_t timer_get_uptime_sec(void);

/**
 * @brief Get timer tick count
 * @return Number of timer interrupts since initialization
 */
uint64_t timer_get_ticks(void);

/**
 * @brief Get timer frequency
 * @return Timer frequency in Hz
 */
uint32_t timer_get_frequency(void);

/* ============================================================================
 * Delay Functions
 * ========================================================================== */

/**
 * @brief Busy-wait delay in milliseconds
 * @param ms Milliseconds to wait
 */
void timer_wait(uint32_t ms);

/**
 * @brief Busy-wait delay in microseconds
 * @param us Microseconds to wait
 */
void timer_udelay(uint32_t us);

/* ============================================================================
 * Timer Callbacks
 * ========================================================================== */

/**
 * @brief Timer callback function type
 * @param data User data passed to callback
 */
typedef void (*timer_callback_t)(void *data);

/**
 * @brief Register a timer callback
 * 
 * @param callback Callback function
 * @param data User data passed to callback
 * @param interval_ms Interval in milliseconds
 * @param repeat Whether to repeat (true) or one-shot (false)
 * @return Timer ID (1-based), or 0 on failure
 */
uint32_t timer_register_callback(timer_callback_t callback, void *data,
                                  uint32_t interval_ms, bool repeat);

/**
 * @brief Unregister a timer callback
 * @param timer_id Timer ID to unregister
 * @return true on success, false on failure
 */
bool timer_unregister_callback(uint32_t timer_id);

/**
 * @brief Get number of active timer callbacks
 * @return Number of active timers
 */
uint32_t timer_get_active_count(void);

/* ============================================================================
 * Timer Control
 * ========================================================================== */

/**
 * @brief Enable the timer
 */
void timer_enable(void);

/**
 * @brief Disable the timer
 */
void timer_disable(void);

/**
 * @brief Check if timer interrupt is pending
 * @return true if interrupt is pending, false otherwise
 */
bool timer_interrupt_pending(void);

/**
 * @brief Mask the timer interrupt
 */
void timer_mask_interrupt(void);

/**
 * @brief Unmask the timer interrupt
 */
void timer_unmask_interrupt(void);

/* ============================================================================
 * IRQ Handler (called from interrupt context)
 * ========================================================================== */

/**
 * @brief Timer interrupt handler
 * 
 * Called from the GIC interrupt handler when the timer interrupt fires.
 * Increments the tick counter, reloads the timer, and processes callbacks.
 */
void timer_irq_handler(void);

#endif /* _DRIVERS_ARM_TIMER_H_ */
