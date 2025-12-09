/**
 * @file hal.c
 * @brief x86_64 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for x86_64 (AMD64/Intel 64-bit) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems (GDT64, IDT64, ISR64, IRQ64, VMM).
 * 
 * **Feature: multi-arch-support, Property 1: HAL Initialization Dispatch**
 * **Validates: Requirements 1.1**
 */

#include <hal/hal.h>
#include "include/gdt64.h"
#include "include/idt64.h"
#include "include/isr64.h"
#include "include/irq64.h"
#include <mm/vmm.h>
#include <lib/klog.h>

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
 * @brief Initialize CPU architecture-specific features (x86_64)
 * 
 * Initializes GDT64 and TSS64 for x86_64 architecture.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal_cpu_init(void) {
    LOG_INFO_MSG("HAL: Initializing x86_64 CPU...\n");
    
    /* Initialize GDT with TSS
     * - Sets up 64-bit segment descriptors for kernel and user mode
     * - Configures TSS64 for privilege level transitions
     * - Default kernel stack at 0x90000, will be updated per-task
     */
    gdt64_init_with_tss(0x90000);
    
    g_hal_cpu_initialized = true;
    LOG_INFO_MSG("HAL: x86_64 CPU initialization complete\n");
}

/**
 * @brief Get current CPU ID
 * @return Always 0 for single-core x86_64 systems
 */
uint32_t hal_cpu_id(void) {
    /* Single-core implementation - always return 0 */
    /* TODO: Use CPUID/APIC ID for multi-core support */
    return 0;
}

/**
 * @brief Halt the CPU until next interrupt
 */
void hal_cpu_halt(void) {
    __asm__ volatile("hlt");
}

/* ============================================================================
 * Interrupt Management
 * ========================================================================== */

/**
 * @brief Initialize interrupt system (x86_64)
 * 
 * Initializes IDT64, ISR64 handlers, and IRQ64 handlers with PIC remapping.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal_interrupt_init(void) {
    LOG_INFO_MSG("HAL: Initializing x86_64 interrupt system...\n");
    
    /* Initialize IDT (Interrupt Descriptor Table) - 64-bit format */
    idt64_init();
    
    /* Initialize ISR (Interrupt Service Routines) for CPU exceptions 0-31 */
    isr64_init();
    
    /* Initialize IRQ (Hardware Interrupt Requests) 0-15
     * This also remaps PIC to avoid conflict with CPU exceptions */
    irq64_init();
    
    g_hal_interrupt_initialized = true;
    LOG_INFO_MSG("HAL: x86_64 interrupt system initialization complete\n");
}

/**
 * @brief Register an interrupt handler
 * @param irq IRQ number (0-15 for hardware interrupts)
 * @param handler Handler function
 * @param data User data (unused in current implementation)
 */
void hal_interrupt_register(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    (void)data;  /* Currently unused */
    
    if (irq < 16) {
        /* Hardware IRQ - use irq64_register_handler */
        irq64_register_handler((uint8_t)irq, (isr_handler_t)handler);
    } else if (irq < 32) {
        /* CPU exception - use isr64_register_handler */
        isr64_register_handler((uint8_t)irq, (isr_handler_t)handler);
    }
}

/**
 * @brief Unregister an interrupt handler
 * @param irq IRQ number
 */
void hal_interrupt_unregister(uint32_t irq) {
    if (irq < 16) {
        irq64_register_handler((uint8_t)irq, NULL);
    } else if (irq < 32) {
        isr64_register_handler((uint8_t)irq, NULL);
    }
}

/**
 * @brief Enable interrupts globally
 */
void hal_interrupt_enable(void) {
    __asm__ volatile("sti");
}

/**
 * @brief Disable interrupts globally
 */
void hal_interrupt_disable(void) {
    __asm__ volatile("cli");
}

/**
 * @brief Save interrupt state and disable interrupts
 * @return Previous RFLAGS value
 */
uint64_t hal_interrupt_save(void) {
    uint64_t flags;
    __asm__ volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

/**
 * @brief Restore interrupt state
 * @param state Previously saved RFLAGS value
 */
void hal_interrupt_restore(uint64_t state) {
    __asm__ volatile(
        "pushq %0\n\t"
        "popfq"
        :
        : "r"(state)
        : "memory"
    );
}

/**
 * @brief Send End-Of-Interrupt signal to PIC
 * @param irq IRQ number that was handled
 */
void hal_interrupt_eoi(uint32_t irq) {
    /* PIC EOI command */
    #define PIC1_COMMAND 0x20
    #define PIC2_COMMAND 0xA0
    #define PIC_EOI      0x20
    
    if (irq >= 8) {
        /* Send EOI to slave PIC */
        hal_port_write8(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master PIC */
    hal_port_write8(PIC1_COMMAND, PIC_EOI);
}

/* ============================================================================
 * MMU Initialization
 * ========================================================================== */

/**
 * @brief Initialize MMU/paging (x86_64)
 * 
 * Initializes the Virtual Memory Manager which sets up 4-level paging.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal_mmu_init(void) {
    LOG_INFO_MSG("HAL: Initializing x86_64 MMU...\n");
    
    /* Initialize VMM (Virtual Memory Manager)
     * This sets up 4-level paging with the boot PML4 */
    vmm_init();
    
    g_hal_mmu_initialized = true;
    LOG_INFO_MSG("HAL: x86_64 MMU initialization complete\n");
}

/* ============================================================================
 * Timer Interface
 * ========================================================================== */

/** Timer tick counter */
static volatile uint64_t g_timer_ticks = 0;

/** Timer frequency in Hz */
static uint32_t g_timer_frequency = 0;

/** User timer callback */
static hal_timer_callback_t g_timer_callback = NULL;

/**
 * @brief Internal timer IRQ handler
 */
static void hal_timer_irq_handler(void *regs) {
    (void)regs;
    g_timer_ticks++;
    
    if (g_timer_callback) {
        g_timer_callback();
    }
}

/**
 * @brief Initialize system timer
 * @param freq_hz Timer frequency in Hz
 * @param callback Function to call on each timer tick
 */
void hal_timer_init(uint32_t freq_hz, hal_timer_callback_t callback) {
    g_timer_frequency = freq_hz;
    g_timer_callback = callback;
    
    /* PIT (Programmable Interval Timer) configuration */
    #define PIT_CHANNEL0_DATA 0x40
    #define PIT_COMMAND       0x43
    #define PIT_BASE_FREQ     1193182
    
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / freq_hz);
    
    /* Set PIT to mode 3 (square wave generator) */
    hal_port_write8(PIT_COMMAND, 0x36);
    hal_port_write8(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    hal_port_write8(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    
    /* Register timer handler (IRQ 0) */
    hal_interrupt_register(0, hal_timer_irq_handler, NULL);
    
    LOG_INFO_MSG("HAL: Timer initialized at %u Hz\n", freq_hz);
}

/**
 * @brief Get system tick count
 * @return Number of timer ticks since boot
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
 * HAL State Query Functions
 * ========================================================================== */

/**
 * @brief Check if CPU has been initialized
 * @return true if CPU initialization has completed (GDT64/TSS64 loaded)
 * 
 * This function checks the actual system state rather than relying on
 * hal_cpu_init() being called, since the kernel may initialize the CPU
 * directly without going through the HAL wrapper.
 */
bool hal_cpu_initialized(void) {
    if (g_hal_cpu_initialized) {
        return true;
    }
    
    /* Check if GDT is loaded by verifying GDTR has a valid base address */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;
    
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    
    /* GDT is initialized if base is non-zero and limit is reasonable */
    return (gdtr.base != 0 && gdtr.limit >= 47);  /* At least 6 entries (including TSS) */
}

/**
 * @brief Check if interrupt system has been initialized
 * @return true if interrupt system has been initialized (IDT loaded)
 * 
 * This function checks the actual system state rather than relying on
 * hal_interrupt_init() being called.
 */
bool hal_interrupt_initialized(void) {
    if (g_hal_interrupt_initialized) {
        return true;
    }
    
    /* Check if IDT is loaded by verifying IDTR has a valid base address */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr;
    
    __asm__ volatile("sidt %0" : "=m"(idtr));
    
    /* IDT is initialized if base is non-zero and limit covers at least 32 entries */
    /* 64-bit IDT entries are 16 bytes each, so 32 entries = 512 bytes */
    return (idtr.base != 0 && idtr.limit >= 511);
}

/**
 * @brief Check if MMU has been initialized
 * @return true if MMU/paging has been initialized (CR0.PG set)
 * 
 * This function checks the actual system state rather than relying on
 * hal_mmu_init() being called.
 */
bool hal_mmu_initialized(void) {
    if (g_hal_mmu_initialized) {
        return true;
    }
    
    /* Check if paging is enabled by reading CR0 */
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    /* Paging is enabled if CR0.PG (bit 31) is set */
    return (cr0 & 0x80000000) != 0;
}

/* ============================================================================
 * Architecture Information
 * ========================================================================== */

/* Note: hal_arch_name() is defined in context64.c */
