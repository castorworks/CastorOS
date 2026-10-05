/**
 * @file hal.c
 * @brief i686 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for i686 (x86 32-bit) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems (GDT, IDT, ISR, IRQ, VMM).
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */

#include <hal/hal.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/isr.h>
#include <kernel/irq.h>
#include <mm/vmm.h>
#include <lib/klog.h>

/* ============================================================================
 * HAL Initialization State Tracking
 * ========================================================================== */

/** Flags to track initialization state */
static bool g_hal_cpu_initialized = false;
static bool g_hal_interrupt_initialized = false;
/* ============================================================================
 * CPU Initialization
 * ========================================================================== */

/**
 * @brief Initialize CPU architecture-specific features (i686)
 * 
 * Initializes GDT and TSS for i686 architecture.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal::Cpu::init() {
    LOG_INFO_MSG("HAL: Initializing i686 CPU...\n");
    
    /* Initialize GDT with TSS
     * - Sets up segment descriptors for kernel and user mode
     * - Configures TSS for privilege level transitions
     * - Default kernel stack at 0x90000, kernel data segment 0x10
     */
    gdt_init_all_with_tss(0x90000, 0x10);
    
    g_hal_cpu_initialized = true;
    LOG_INFO_MSG("HAL: i686 CPU initialization complete\n");
}

/**
 * @brief Halt the CPU until next interrupt
 */
void hal::Cpu::halt() {
    __asm__ volatile("hlt");
}

void hal::Cpu::idle() {
    /* sti only takes effect after the following instruction, so no interrupt
     * can slip in between enabling interrupts and halting */
    __asm__ volatile("sti; hlt");
}

/* ============================================================================
 * Interrupt Management
 * ========================================================================== */

/**
 * @brief Initialize interrupt system (i686)
 * 
 * Initializes IDT, ISR handlers, and IRQ handlers with PIC remapping.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal::Interrupt::init() {
    LOG_INFO_MSG("HAL: Initializing i686 interrupt system...\n");
    
    /* Initialize IDT (Interrupt Descriptor Table) */
    idt_init();
    
    /* Initialize ISR (Interrupt Service Routines) for CPU exceptions 0-31 */
    isr_init();
    
    /* Initialize IRQ (Hardware Interrupt Requests) 0-15
     * This also remaps PIC to avoid conflict with CPU exceptions */
    irq_init();
    
    g_hal_interrupt_initialized = true;
    LOG_INFO_MSG("HAL: i686 interrupt system initialization complete\n");
}

/**
 * @brief Enable interrupts globally
 */
void hal::Interrupt::enable() {
    __asm__ volatile("sti");
}

bool hal::Interrupt::irq_is_free(uint32_t irq) {
    /* IRQ 2 is the cascade line of the slave PIC */
    return irq < 16 && irq != 2 && !irq_has_handler((uint8_t)irq);
}

void hal::Interrupt::mask_irq(uint32_t irq) {
    irq_disable_line((uint8_t)irq);
}

void hal::Interrupt::unmask_irq(uint32_t irq) {
    if (irq >= 8) {
        irq_enable_line(2);     /* slave PIC interrupts arrive through the cascade line */
    }
    irq_enable_line((uint8_t)irq);
}

/* ============================================================================
 * Cache Maintenance Operations (i686)
 * 
 * On x86, caches are DMA-coherent (snooped), so explicit cache maintenance
 * is not required for DMA operations. These functions are no-ops.
 * 
 * @see Requirements 10.2
 * ========================================================================== */

/**
 * @brief Get architecture name string
 * @return Architecture name
 */
const char *hal_arch_name(void) {
    return "i686";
}
