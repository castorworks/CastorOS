/**
 * @file timer.c
 * @brief ARM64 Generic Timer Driver
 * 
 * This driver implements the ARM Generic Timer, which provides a system-wide
 * time reference and timer functionality. The Generic Timer is part of the
 * ARM architecture and is available on all ARMv8-A processors.
 * 
 * The ARM Generic Timer provides:
 * - A system counter (CNTPCT_EL0) that increments at a fixed frequency
 * - Physical and virtual timers with compare and countdown modes
 * - Per-CPU timer interrupts
 * 
 * This driver uses the physical timer (CNTP) for system tick generation.
 * 
 * Requirements: 9.3 - ARM64 device discovery and drivers
 */

#include <drivers/arm/timer.h>
#include <types.h>

/* Forward declarations for serial output */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);
extern void serial_put_dec(uint64_t value);

/* ============================================================================
 * ARM Generic Timer System Registers
 * 
 * The Generic Timer uses the following system registers:
 * - CNTFRQ_EL0: Counter frequency (read-only, set by firmware)
 * - CNTPCT_EL0: Physical counter value (read-only)
 * - CNTP_TVAL_EL0: Physical timer value (countdown)
 * - CNTP_CTL_EL0: Physical timer control
 * - CNTP_CVAL_EL0: Physical timer compare value
 * ========================================================================== */

/* CNTP_CTL_EL0 bits */
#define CNTP_CTL_ENABLE     (1ULL << 0)  /**< Timer enable */
#define CNTP_CTL_IMASK      (1ULL << 1)  /**< Interrupt mask (1=masked) */
#define CNTP_CTL_ISTATUS    (1ULL << 2)  /**< Interrupt status (read-only) */

/* ============================================================================
 * System Register Access Functions
 * ========================================================================== */

/**
 * @brief Read the counter frequency register
 * @return Counter frequency in Hz
 */
static inline uint64_t read_cntfrq_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/**
 * @brief Read the physical counter value
 * @return Current counter value
 */
static inline uint64_t read_cntpct_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

/**
 * @brief Write the physical timer value (countdown)
 * @param val Timer value (number of counter ticks until interrupt)
 */
static inline void write_cntp_tval_el0(uint64_t val) {
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(val));
}

/**
 * @brief Read the physical timer value
 * @return Current timer value
 */
static inline uint64_t read_cntp_tval_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_tval_el0" : "=r"(val));
    return val;
}

/**
 * @brief Write the physical timer compare value
 * @param val Compare value (absolute counter value for interrupt)
 */
static inline void write_cntp_cval_el0(uint64_t val) {
    __asm__ volatile("msr cntp_cval_el0, %0" : : "r"(val));
}

/**
 * @brief Read the physical timer compare value
 * @return Current compare value
 */
static inline uint64_t read_cntp_cval_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_cval_el0" : "=r"(val));
    return val;
}

/**
 * @brief Write the physical timer control register
 * @param val Control register value
 */
static inline void write_cntp_ctl_el0(uint64_t val) {
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(val));
}

/**
 * @brief Read the physical timer control register
 * @return Control register value
 */
static inline uint64_t read_cntp_ctl_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    return val;
}

/* ============================================================================
 * Timer State
 * ========================================================================== */

/** Counter frequency in Hz (from CNTFRQ_EL0) */
static uint64_t counter_frequency = 0;

/** Requested timer interrupt frequency in Hz */
static uint32_t timer_frequency = 0;

/** Counter ticks per timer interrupt */
static uint64_t ticks_per_interrupt = 0;

/** Software tick counter (incremented by timer IRQ) */
static volatile uint64_t timer_ticks = 0;

/** Timer initialized flag */
static bool timer_initialized = false;

/** Counter value at boot (for uptime calculation) */
static uint64_t boot_counter_value = 0;

/* ============================================================================
 * Timer Callback Support
 * ========================================================================== */

/** Maximum number of timer callbacks */
#define MAX_TIMER_CALLBACKS 8

/** Timer callback entry */
typedef struct {
    timer_callback_t callback;  /**< Callback function */
    void *data;                 /**< User data */
    uint32_t interval_ticks;    /**< Interval in timer ticks */
    uint32_t remaining_ticks;   /**< Ticks until next callback */
    bool repeat;                /**< Repeat flag */
    bool active;                /**< Active flag */
} timer_callback_entry_t;

/** Timer callback table */
static timer_callback_entry_t timer_callbacks[MAX_TIMER_CALLBACKS];

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Initialize the ARM Generic Timer
 * 
 * Configures the physical timer to generate periodic interrupts at the
 * specified frequency.
 * 
 * @param frequency Target frequency in Hz (e.g., 100 for 100 Hz / 10ms ticks)
 */
void timer_init(uint32_t frequency) {
    serial_puts("Timer: Initializing ARM Generic Timer...\n");
    
    /* Read counter frequency from system register */
    counter_frequency = read_cntfrq_el0();
    serial_puts("  Counter frequency: ");
    serial_put_dec(counter_frequency);
    serial_puts(" Hz\n");
    
    if (counter_frequency == 0) {
        serial_puts("  ERROR: Counter frequency is 0!\n");
        return;
    }
    
    /* Store requested frequency */
    timer_frequency = frequency;
    
    /* Calculate ticks per interrupt */
    ticks_per_interrupt = counter_frequency / frequency;
    serial_puts("  Ticks per interrupt: ");
    serial_put_dec(ticks_per_interrupt);
    serial_puts("\n");
    
    /* Record boot counter value */
    boot_counter_value = read_cntpct_el0();
    
    /* Initialize callback table */
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        timer_callbacks[i].active = false;
    }
    
    /* Disable timer while configuring */
    write_cntp_ctl_el0(0);
    
    /* Set timer value for first interrupt */
    write_cntp_tval_el0(ticks_per_interrupt);
    
    /* Enable timer (unmask interrupt) */
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
    
    timer_initialized = true;
    
    serial_puts("  Timer initialized at ");
    serial_put_dec(frequency);
    serial_puts(" Hz\n");
}

/**
 * @brief Check if timer is initialized
 * @return true if initialized, false otherwise
 */
bool timer_is_initialized(void) {
    return timer_initialized;
}

/**
 * @brief Get the counter frequency
 * @return Counter frequency in Hz
 */
uint64_t timer_get_counter_frequency(void) {
    return counter_frequency;
}

/**
 * @brief Get the current counter value
 * @return Current counter value
 */
uint64_t timer_get_counter(void) {
    return read_cntpct_el0();
}

/**
 * @brief Get system uptime in milliseconds
 * @return Milliseconds since boot
 */
uint64_t timer_get_uptime_ms(void) {
    if (counter_frequency == 0) {
        return 0;
    }
    
    uint64_t elapsed = read_cntpct_el0() - boot_counter_value;
    /* Convert to milliseconds: elapsed * 1000 / frequency */
    /* Use division first to avoid overflow */
    return (elapsed / (counter_frequency / 1000));
}

/**
 * @brief Get system uptime in seconds
 * @return Seconds since boot
 */
uint32_t timer_get_uptime_sec(void) {
    if (counter_frequency == 0) {
        return 0;
    }
    
    uint64_t elapsed = read_cntpct_el0() - boot_counter_value;
    return (uint32_t)(elapsed / counter_frequency);
}

/**
 * @brief Get timer tick count
 * @return Number of timer interrupts since initialization
 */
uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

/**
 * @brief Get timer frequency
 * @return Timer frequency in Hz
 */
uint32_t timer_get_frequency(void) {
    return timer_frequency;
}

/**
 * @brief Busy-wait delay in milliseconds
 * @param ms Milliseconds to wait
 */
void timer_wait(uint32_t ms) {
    if (counter_frequency == 0) {
        return;
    }
    
    uint64_t ticks_to_wait = (counter_frequency / 1000) * ms;
    uint64_t start = read_cntpct_el0();
    
    while ((read_cntpct_el0() - start) < ticks_to_wait) {
        __asm__ volatile("nop");
    }
}

/**
 * @brief Busy-wait delay in microseconds
 * @param us Microseconds to wait
 */
void timer_udelay(uint32_t us) {
    if (counter_frequency == 0) {
        return;
    }
    
    uint64_t ticks_to_wait = (counter_frequency / 1000000) * us;
    if (ticks_to_wait == 0) {
        ticks_to_wait = 1;  /* Minimum 1 tick */
    }
    
    uint64_t start = read_cntpct_el0();
    
    while ((read_cntpct_el0() - start) < ticks_to_wait) {
        __asm__ volatile("nop");
    }
}

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
                                  uint32_t interval_ms, bool repeat) {
    if (!callback || interval_ms == 0 || timer_frequency == 0) {
        return 0;
    }
    
    /* Find a free slot */
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (!timer_callbacks[i].active) {
            /* Calculate interval in timer ticks */
            uint32_t interval_ticks = (timer_frequency * interval_ms) / 1000;
            if (interval_ticks == 0) {
                interval_ticks = 1;
            }
            
            timer_callbacks[i].callback = callback;
            timer_callbacks[i].data = data;
            timer_callbacks[i].interval_ticks = interval_ticks;
            timer_callbacks[i].remaining_ticks = interval_ticks;
            timer_callbacks[i].repeat = repeat;
            timer_callbacks[i].active = true;
            
            return (uint32_t)(i + 1);  /* Return 1-based ID */
        }
    }
    
    return 0;  /* No free slots */
}

/**
 * @brief Unregister a timer callback
 * @param timer_id Timer ID to unregister
 * @return true on success, false on failure
 */
bool timer_unregister_callback(uint32_t timer_id) {
    if (timer_id == 0 || timer_id > MAX_TIMER_CALLBACKS) {
        return false;
    }
    
    uint32_t index = timer_id - 1;
    if (!timer_callbacks[index].active) {
        return false;
    }
    
    timer_callbacks[index].active = false;
    return true;
}

/**
 * @brief Get number of active timer callbacks
 * @return Number of active timers
 */
uint32_t timer_get_active_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (timer_callbacks[i].active) {
            count++;
        }
    }
    return count;
}

/* ============================================================================
 * Timer IRQ Handler
 * 
 * This function should be called from the timer interrupt handler.
 * ========================================================================== */

/**
 * @brief Timer interrupt handler
 * 
 * Called from the GIC interrupt handler when the timer interrupt fires.
 * Increments the tick counter, reloads the timer, and processes callbacks.
 */
void timer_irq_handler(void) {
    /* Increment tick counter */
    timer_ticks++;
    
    /* Reload timer for next interrupt */
    write_cntp_tval_el0(ticks_per_interrupt);
    
    /* Process callbacks */
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (timer_callbacks[i].active) {
            timer_callbacks[i].remaining_ticks--;
            
            if (timer_callbacks[i].remaining_ticks == 0) {
                /* Call the callback */
                timer_callbacks[i].callback(timer_callbacks[i].data);
                
                if (timer_callbacks[i].repeat) {
                    /* Reset for next interval */
                    timer_callbacks[i].remaining_ticks = timer_callbacks[i].interval_ticks;
                } else {
                    /* One-shot: deactivate */
                    timer_callbacks[i].active = false;
                }
            }
        }
    }
}

/**
 * @brief Enable the timer
 */
void timer_enable(void) {
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);
}

/**
 * @brief Disable the timer
 */
void timer_disable(void) {
    write_cntp_ctl_el0(0);
}

/**
 * @brief Check if timer interrupt is pending
 * @return true if interrupt is pending, false otherwise
 */
bool timer_interrupt_pending(void) {
    return (read_cntp_ctl_el0() & CNTP_CTL_ISTATUS) != 0;
}

/**
 * @brief Mask the timer interrupt
 */
void timer_mask_interrupt(void) {
    uint64_t ctl = read_cntp_ctl_el0();
    ctl |= CNTP_CTL_IMASK;
    write_cntp_ctl_el0(ctl);
}

/**
 * @brief Unmask the timer interrupt
 */
void timer_unmask_interrupt(void) {
    uint64_t ctl = read_cntp_ctl_el0();
    ctl &= ~CNTP_CTL_IMASK;
    write_cntp_ctl_el0(ctl);
}
