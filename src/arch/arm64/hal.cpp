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
#include <drivers/timer.h>
#include <types.h>
#include "include/exception.h"
#include "include/gic.h"

/* Forward declaration for serial output (defined in stubs.c) */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

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
void hal::Cpu::init() {
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
uint32_t hal::Cpu::id() {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFF);  /* Aff0 field */
}

/**
 * @brief Halt the CPU until next interrupt
 */
void hal::Cpu::halt() {
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
void hal::Interrupt::init() {
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
void hal::Interrupt::register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    gic_register_handler(irq, handler, data);
    gic_enable_irq(irq);
}

/**
 * @brief Unregister an interrupt handler
 * @param irq IRQ number
 */
void hal::Interrupt::unregister_handler(uint32_t irq) {
    gic_disable_irq(irq);
    gic_unregister_handler(irq);
}

/**
 * @brief Enable interrupts globally
 */
void hal::Interrupt::enable() {
    __asm__ volatile("msr daifclr, #0xf" ::: "memory");
}

/**
 * @brief Disable interrupts globally
 */
void hal::Interrupt::disable() {
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
}

/**
 * @brief Save interrupt state and disable interrupts
 * @return Previous DAIF value
 */
uint64_t hal::Interrupt::save() {
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
void hal::Interrupt::restore(uint64_t state) {
    __asm__ volatile("msr daif, %0" : : "r"(state) : "memory");
}

/**
 * @brief Send End-Of-Interrupt signal to GIC
 * @param irq IRQ number that was handled
 */
void hal::Interrupt::eoi(uint32_t irq) {
    gic_end_irq(irq);
}

bool hal::Interrupt::irq_is_free(uint32_t irq) {
    /* Only shared peripheral interrupts; SGIs/PPIs belong to the kernel */
    return irq >= 32 && irq < 1020 && !gic_has_handler(irq);
}

void hal::Interrupt::mask_irq(uint32_t irq) {
    gic_disable_irq(irq);
}

void hal::Interrupt::unmask_irq(uint32_t irq) {
    gic_enable_irq(irq);
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
 * @brief Internal timer IRQ handler
 * @param data User data (unused)
 */
static void hal_timer_irq_handler(void *data) {
    (void)data;
    
    /* Increment software tick counter */
    g_timer_ticks = g_timer_ticks + 1;
    
    /* Let the timer driver handle the hardware FIRST: it reloads the timer for the
     * next tick (which clears the interrupt condition), advances its own tick
     * counter. */
    drivers::Timer::irq_handler();
    
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
void hal::Timer::init(uint32_t freq_hz, hal_timer_callback_t callback) {
    g_timer_frequency = freq_hz;
    g_timer_callback = callback;

    /* The HAL owns the IRQ registration and the scheduler callback;
     * the driver programs and enables the timer itself. */
    hal::Interrupt::register_handler(ARM_TIMER_IRQ, hal_timer_irq_handler, NULL);
    drivers::Timer::init(freq_hz);
}

/**
 * @brief Get system tick count
 * @return Number of timer ticks since boot (software counter)
 */
uint64_t hal::Timer::get_ticks() {
    return g_timer_ticks;
}

/**
 * @brief Get timer frequency
 * @return Timer frequency in Hz
 */
uint32_t hal::Timer::get_frequency() {
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

/* ============================================================================
 * I/O Operations
 * ========================================================================== */

/* Note: MMIO functions are defined as inline in hal.h */

/* ============================================================================
 * Initialization State Queries
 * ========================================================================== */

bool hal::Cpu::initialized() {
    return g_hal_cpu_initialized;
}

bool hal::Interrupt::initialized() {
    return g_hal_interrupt_initialized;
}

bool hal::Mmu::initialized() {
    if (g_hal_mmu_initialized) {
        return true;
    }

    /* Check the actual system state: SCTLR_EL1.M (bit 0) is set once the MMU is on */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    return (sctlr & 1) != 0;
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
