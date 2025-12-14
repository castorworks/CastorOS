/**
 * @file hal.c
 * @brief ARM64 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for ARM64 (AArch64) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems.
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */

#include <hal/hal.h>
#include <types.h>
#include "include/exception.h"
#include "include/gic.h"

/* Forward declaration for serial output (defined in stubs.c) */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);

/* ============================================================================
 * HAL Initialization State Tracking
 * ========================================================================== */

/** Flags to track initialization state */
static bool g_hal_cpu_initialized = false;
static bool g_hal_interrupt_initialized = false;
static bool g_hal_mmu_initialized = false;

/* ============================================================================
 * CPU Initialization
 * ========================================================================== */

/**
 * @brief Initialize CPU architecture-specific features (ARM64)
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal_cpu_init(void) {
    serial_puts("HAL: Initializing ARM64 CPU...\n");
    
    /* ARM64 CPU initialization:
     * - Exception level should already be EL1 (set by boot code)
     * - System registers configured by boot code
     */
    
    /* Enable FP/SIMD access for EL0 and EL1
     * CPACR_EL1.FPEN[21:20] = 0b11 enables FP/SIMD for both EL0 and EL1
     */
    uint64_t cpacr;
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL << 20);  /* FPEN = 0b11 */
    __asm__ volatile("msr cpacr_el1, %0" : : "r"(cpacr));
    __asm__ volatile("isb");
    serial_puts("HAL: FP/SIMD enabled for EL0 and EL1\n");
    
    g_hal_cpu_initialized = true;
    serial_puts("HAL: ARM64 CPU initialization complete\n");
}

/**
 * @brief Get current CPU ID
 * @return CPU ID from MPIDR_EL1 register
 */
uint32_t hal_cpu_id(void) {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFF);  /* Aff0 field */
}

/**
 * @brief Halt the CPU until next interrupt
 */
void hal_cpu_halt(void) {
    __asm__ volatile("wfi");
}

/* ============================================================================
 * Interrupt Management
 * ========================================================================== */

/**
 * @brief Initialize interrupt system (ARM64)
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal_interrupt_init(void) {
    serial_puts("HAL: Initializing ARM64 interrupt system...\n");
    
    /* Initialize exception vectors (VBAR_EL1) */
    arm64_exception_init();
    
    /* Initialize GIC (Generic Interrupt Controller) */
    gic_init();
    
    g_hal_interrupt_initialized = true;
    serial_puts("HAL: ARM64 interrupt system initialization complete\n");
}

/**
 * @brief Register an interrupt handler
 * @param irq IRQ number
 * @param handler Handler function
 * @param data User data
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
void hal_interrupt_register(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    gic_register_handler(irq, handler, data);
    gic_enable_irq(irq);
}

/**
 * @brief Unregister an interrupt handler
 * @param irq IRQ number
 */
void hal_interrupt_unregister(uint32_t irq) {
    gic_disable_irq(irq);
    gic_unregister_handler(irq);
}

/**
 * @brief Enable interrupts globally
 */
void hal_interrupt_enable(void) {
    serial_puts("HAL: Enabling interrupts...\n");
    
    /* Debug: Print current SP before enabling interrupts */
    uint64_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    serial_puts("  Current SP: ");
    serial_put_hex64(sp);
    serial_puts("\n");
    
    __asm__ volatile("msr daifclr, #0xf" ::: "memory");
    serial_puts("HAL: Interrupts enabled\n");
}

/**
 * @brief Disable interrupts globally
 */
void hal_interrupt_disable(void) {
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
}

/**
 * @brief Save interrupt state and disable interrupts
 * @return Previous DAIF value
 */
uint64_t hal_interrupt_save(void) {
    uint64_t daif;
    __asm__ volatile(
        "mrs %0, daif\n\t"
        "msr daifset, #0xf"
        : "=r"(daif)
        :
        : "memory"
    );
    return daif;
}

/**
 * @brief Restore interrupt state
 * @param state Previously saved DAIF value
 */
void hal_interrupt_restore(uint64_t state) {
    __asm__ volatile("msr daif, %0" : : "r"(state) : "memory");
}

/**
 * @brief Send End-Of-Interrupt signal to GIC
 * @param irq IRQ number that was handled
 */
void hal_interrupt_eoi(uint32_t irq) {
    gic_end_irq(irq);
}

/* ============================================================================
 * MMU Functions (delegated to mmu.c)
 * ========================================================================== */

/* Note: Most MMU functions are implemented in src/arch/arm64/mm/mmu.c */

/* ============================================================================
 * Timer Functions
 * ========================================================================== */

/** Timer tick counter (software counter incremented by timer IRQ) */
static volatile uint64_t g_timer_ticks = 0;

/** Timer frequency in Hz (requested frequency) */
static uint32_t g_timer_frequency = 0;

/** User timer callback */
static hal_timer_callback_t g_timer_callback = NULL;

/** ARM Generic Timer IRQ number (typically 30 for physical timer) */
#define ARM_TIMER_IRQ   30

/**
 * @brief Read ARM Generic Timer counter frequency
 * @return Counter frequency in Hz
 */
static inline uint64_t read_cntfrq_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/**
 * @brief Read ARM Generic Timer physical counter
 * @return Current counter value
 */
static inline uint64_t read_cntpct_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

/**
 * @brief Write ARM Generic Timer physical timer value
 * @param val Timer value (countdown)
 */
static inline void write_cntp_tval_el0(uint64_t val) {
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(val));
}

/**
 * @brief Read ARM Generic Timer physical timer control
 * @return Control register value
 */
static inline uint64_t read_cntp_ctl_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    return val;
}

/**
 * @brief Write ARM Generic Timer physical timer control
 * @param val Control register value
 */
static inline void write_cntp_ctl_el0(uint64_t val) {
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(val));
}

/** CNTP_CTL_EL0 bits */
#define CNTP_CTL_ENABLE     (1ULL << 0)  /**< Timer enable */
#define CNTP_CTL_IMASK      (1ULL << 1)  /**< Interrupt mask */
#define CNTP_CTL_ISTATUS    (1ULL << 2)  /**< Interrupt status */

/**
 * @brief Internal timer IRQ handler
 * @param data User data (unused)
 */
static void hal_timer_irq_handler(void *data) {
    (void)data;
    
    /* Increment software tick counter */
    g_timer_ticks++;
    
    /* Reload timer for next tick FIRST - this clears the interrupt condition */
    if (g_timer_frequency > 0) {
        uint64_t cntfrq = read_cntfrq_el0();
        uint64_t tval = cntfrq / g_timer_frequency;
        write_cntp_tval_el0(tval);
    }
    
    /* Debug: Print tick count periodically */
    if (g_timer_ticks <= 10 || (g_timer_ticks % 100) == 0) {
        serial_puts("[TIMER] Tick ");
        serial_put_hex64(g_timer_ticks);
        
        /* Check timer state after reload */
        uint64_t ctl = read_cntp_ctl_el0();
        serial_puts(" CTL=");
        serial_put_hex64(ctl);
        serial_puts("\n");
    }
    
    /* Call user callback if registered */
    if (g_timer_callback) {
        g_timer_callback();
    }
}

/**
 * @brief Initialize system timer (ARM64)
 * @param freq_hz Timer frequency in Hz
 * @param callback Timer callback function
 * 
 * Configures the ARM Generic Timer (physical timer) to generate
 * periodic interrupts at the specified frequency.
 */
void hal_timer_init(uint32_t freq_hz, hal_timer_callback_t callback) {
    serial_puts("HAL: Initializing ARM64 timer...\n");
    
    g_timer_frequency = freq_hz;
    g_timer_callback = callback;
    
    /* Get the counter frequency */
    uint64_t cntfrq = read_cntfrq_el0();
    serial_puts("  Counter frequency: ");
    serial_put_hex64(cntfrq);
    serial_puts(" Hz\n");
    
    if (cntfrq == 0) {
        serial_puts("  WARNING: Counter frequency is 0, timer may not work\n");
        return;
    }
    
    /* Calculate timer value for desired frequency */
    uint64_t tval = cntfrq / freq_hz;
    serial_puts("  Timer value: ");
    serial_put_hex64(tval);
    serial_puts("\n");
    
    /* Register timer IRQ handler */
    serial_puts("  Registering timer IRQ handler for IRQ ");
    serial_put_hex64(ARM_TIMER_IRQ);
    serial_puts("\n");
    hal_interrupt_register(ARM_TIMER_IRQ, hal_timer_irq_handler, NULL);
    
    /* Set timer value and enable timer */
    write_cntp_tval_el0(tval);
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);  /* Enable, unmask interrupt */
    
    /* Verify timer is enabled */
    uint64_t ctl = read_cntp_ctl_el0();
    serial_puts("  Timer control: ");
    serial_put_hex64(ctl);
    serial_puts(" (ENABLE=");
    serial_puts((ctl & CNTP_CTL_ENABLE) ? "1" : "0");
    serial_puts(", IMASK=");
    serial_puts((ctl & CNTP_CTL_IMASK) ? "1" : "0");
    serial_puts(", ISTATUS=");
    serial_puts((ctl & CNTP_CTL_ISTATUS) ? "1" : "0");
    serial_puts(")\n");
    
    /* Check current DAIF state */
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    serial_puts("  Current DAIF: ");
    serial_put_hex64(daif);
    serial_puts(" (I=");
    serial_puts((daif & (1 << 7)) ? "masked" : "enabled");
    serial_puts(")\n");
    
    /* Read current counter value */
    uint64_t cnt_before = read_cntpct_el0();
    serial_puts("  Counter before delay: ");
    serial_put_hex64(cnt_before);
    serial_puts("\n");
    
    /* Wait a bit and check if timer interrupt is pending */
    for (volatile int i = 0; i < 10000000; i++) {
        __asm__ volatile("nop");
    }
    
    uint64_t cnt_after = read_cntpct_el0();
    serial_puts("  Counter after delay: ");
    serial_put_hex64(cnt_after);
    serial_puts(" (diff=");
    serial_put_hex64(cnt_after - cnt_before);
    serial_puts(")\n");
    
    ctl = read_cntp_ctl_el0();
    serial_puts("  After delay - Timer control: ");
    serial_put_hex64(ctl);
    serial_puts(" (ISTATUS=");
    serial_puts((ctl & CNTP_CTL_ISTATUS) ? "1" : "0");
    serial_puts(")\n");
    
    serial_puts("HAL: ARM64 timer initialization complete\n");
}

/**
 * @brief Get system tick count
 * @return Number of timer ticks since boot (software counter)
 */
uint64_t hal_timer_get_ticks(void) {
    return g_timer_ticks;
}

/**
 * @brief Get timer frequency
 * @return Timer frequency in Hz
 */
uint32_t hal_timer_get_frequency(void) {
    return g_timer_frequency;
}

/* ============================================================================
 * Memory Barrier Operations (ARM64)
 * 
 * ARM64 provides several memory barrier instructions:
 *   - DMB (Data Memory Barrier): Ensures ordering of memory accesses
 *   - DSB (Data Synchronization Barrier): Ensures completion of memory accesses
 *   - ISB (Instruction Synchronization Barrier): Flushes pipeline
 * 
 * Shareability domains:
 *   - SY: Full system (all observers)
 *   - ISH: Inner Shareable (typically same CPU cluster)
 *   - OSH: Outer Shareable (typically all CPUs)
 *   - NSH: Non-shareable (single CPU)
 * 
 * Access types:
 *   - LD: Load operations only
 *   - ST: Store operations only
 *   - (none): Both load and store
 * 
 * Requirements: 9.1 - MMIO memory barriers
 * ========================================================================== */

/**
 * @brief Data Memory Barrier - Full System
 * 
 * Ensures that all explicit memory accesses that appear in program order
 * before the DMB instruction are observed before any explicit memory
 * accesses that appear in program order after the DMB instruction.
 */
void hal_dmb_sy(void) {
    __asm__ volatile("dmb sy" ::: "memory");
}

/**
 * @brief Data Memory Barrier - Inner Shareable
 * 
 * Same as DMB SY but only affects observers in the Inner Shareable domain.
 */
void hal_dmb_ish(void) {
    __asm__ volatile("dmb ish" ::: "memory");
}

/**
 * @brief Data Memory Barrier - Inner Shareable, Store only
 * 
 * Ensures ordering of store operations within the Inner Shareable domain.
 */
void hal_dmb_ishst(void) {
    __asm__ volatile("dmb ishst" ::: "memory");
}

/**
 * @brief Data Memory Barrier - Inner Shareable, Load only
 * 
 * Ensures ordering of load operations within the Inner Shareable domain.
 */
void hal_dmb_ishld(void) {
    __asm__ volatile("dmb ishld" ::: "memory");
}

/**
 * @brief Data Synchronization Barrier - Full System
 * 
 * Ensures that all explicit memory accesses that appear in program order
 * before the DSB instruction complete before the DSB instruction completes.
 * Also ensures that any context-altering operations complete.
 */
void hal_dsb_sy(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

/**
 * @brief Data Synchronization Barrier - Inner Shareable
 * 
 * Same as DSB SY but only affects observers in the Inner Shareable domain.
 */
void hal_dsb_ish(void) {
    __asm__ volatile("dsb ish" ::: "memory");
}

/**
 * @brief Data Synchronization Barrier - Inner Shareable, Store only
 * 
 * Ensures completion of store operations within the Inner Shareable domain.
 */
void hal_dsb_ishst(void) {
    __asm__ volatile("dsb ishst" ::: "memory");
}

/**
 * @brief Instruction Synchronization Barrier
 * 
 * Flushes the pipeline and ensures that all instructions following the ISB
 * are fetched from cache or memory after the ISB has completed.
 * 
 * Required after:
 *   - Modifying instruction memory
 *   - Changing system registers that affect instruction execution
 *   - TLB maintenance operations
 */
void hal_isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

/* ============================================================================
 * I/O Operations
 * ========================================================================== */

/* Note: MMIO functions are defined as inline in hal.h */

/* ============================================================================
 * Initialization State Queries
 * ========================================================================== */

bool hal_cpu_initialized(void) {
    return g_hal_cpu_initialized;
}

bool hal_interrupt_initialized(void) {
    return g_hal_interrupt_initialized;
}

bool hal_mmu_initialized(void) {
    return g_hal_mmu_initialized;
}

void hal_set_mmu_initialized(bool state) {
    g_hal_mmu_initialized = state;
}

/* ============================================================================
 * Architecture Information
 * ========================================================================== */

/**
 * @brief Get architecture name string
 * @return "arm64"
 */
const char *hal_arch_name(void) {
    return "arm64";
}
