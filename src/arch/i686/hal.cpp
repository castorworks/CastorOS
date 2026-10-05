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
static bool g_hal_mmu_initialized = false;

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
 * @brief Get current CPU ID
 * @return Always 0 for single-core i686 systems
 */
uint32_t hal::Cpu::id() {
    /* Single-core implementation - always return 0 */
    return 0;
}

/**
 * @brief Halt the CPU until next interrupt
 */
void hal::Cpu::halt() {
    __asm__ volatile("hlt");
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
 * @brief Register an interrupt handler
 * @param irq IRQ number (0-15 for hardware interrupts)
 * @param handler Handler function
 * @param data User data (unused in current implementation)
 */
void hal::Interrupt::register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    (void)data;  /* Currently unused */
    
    if (irq < 16) {
        /* Hardware IRQ - use irq_register_handler */
        irq_register_handler((uint8_t)irq, (isr_handler_t)handler);
    } else if (irq < 32) {
        /* CPU exception - use isr_register_handler */
        isr_register_handler((uint8_t)irq, (isr_handler_t)handler);
    }
}

/**
 * @brief Unregister an interrupt handler
 * @param irq IRQ number
 */
void hal::Interrupt::unregister_handler(uint32_t irq) {
    if (irq < 16) {
        irq_register_handler((uint8_t)irq, NULL);
    } else if (irq < 32) {
        isr_register_handler((uint8_t)irq, NULL);
    }
}

/**
 * @brief Enable interrupts globally
 */
void hal::Interrupt::enable() {
    __asm__ volatile("sti");
}

/**
 * @brief Disable interrupts globally
 */
void hal::Interrupt::disable() {
    __asm__ volatile("cli");
}

/**
 * @brief Save interrupt state and disable interrupts
 * @return Previous EFLAGS value
 */
uint64_t hal::Interrupt::save() {
    uint32_t flags;
    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
    return (uint64_t)flags;
}

/**
 * @brief Restore interrupt state
 * @param state Previously saved EFLAGS value
 */
void hal::Interrupt::restore(uint64_t state) {
    __asm__ volatile(
        "pushl %0\n\t"
        "popfl"
        :
        : "r"((uint32_t)state)
        : "memory"
    );
}

/**
 * @brief Send End-Of-Interrupt signal to PIC
 * @param irq IRQ number that was handled
 */
void hal::Interrupt::eoi(uint32_t irq) {
    /* PIC EOI command */
    #define PIC1_COMMAND 0x20
    #define PIC2_COMMAND 0xA0
    #define PIC_EOI      0x20
    
    if (irq >= 8) {
        /* Send EOI to slave PIC */
        hal::Port::write8(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master PIC */
    hal::Port::write8(PIC1_COMMAND, PIC_EOI);
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
 * MMU Initialization
 * ========================================================================== */

/**
 * @brief Initialize MMU/paging (i686)
 * 
 * Initializes the Virtual Memory Manager which sets up paging.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal::Mmu::init() {
    LOG_INFO_MSG("HAL: Initializing i686 MMU...\n");
    
    /* Initialize VMM (Virtual Memory Manager)
     * This sets up paging with the boot page directory */
    mm::Vmm::init();
    
    g_hal_mmu_initialized = true;
    LOG_INFO_MSG("HAL: i686 MMU initialization complete\n");
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
    g_timer_ticks = g_timer_ticks + 1;
    
    if (g_timer_callback) {
        g_timer_callback();
    }
}

/**
 * @brief Initialize system timer
 * @param freq_hz Timer frequency in Hz
 * @param callback Function to call on each timer tick
 */
void hal::Timer::init(uint32_t freq_hz, hal_timer_callback_t callback) {
    g_timer_frequency = freq_hz;
    g_timer_callback = callback;
    
    /* PIT (Programmable Interval Timer) configuration */
    #define PIT_CHANNEL0_DATA 0x40
    #define PIT_COMMAND       0x43
    #define PIT_BASE_FREQ     1193182
    
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / freq_hz);
    
    /* Set PIT to mode 3 (square wave generator) */
    hal::Port::write8(PIT_COMMAND, 0x36);
    hal::Port::write8(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    hal::Port::write8(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    
    /* Register timer handler (IRQ 0) */
    hal::Interrupt::register_handler(0, hal_timer_irq_handler, NULL);
    
    LOG_INFO_MSG("HAL: Timer initialized at %u Hz\n", freq_hz);
}

/**
 * @brief Get system tick count
 * @return Number of timer ticks since boot
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
 * HAL State Query Functions
 * ========================================================================== */

/**
 * @brief Check if CPU has been initialized
 * @return true if CPU initialization has completed (GDT/TSS loaded)
 * 
 * This function checks the actual system state rather than relying on
 * hal::Cpu::init() being called, since the kernel may initialize the CPU
 * directly without going through the HAL wrapper.
 */
bool hal::Cpu::initialized() {
    if (g_hal_cpu_initialized) {
        return true;
    }
    
    /* Check if GDT is loaded by verifying GDTR has a valid base address */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) gdtr;
    
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    
    /* GDT is initialized if base is non-zero and limit is reasonable */
    return (gdtr.base != 0 && gdtr.limit >= 23);  /* At least 3 entries */
}

/**
 * @brief Check if interrupt system has been initialized
 * @return true if interrupt system has been initialized (IDT loaded)
 * 
 * This function checks the actual system state rather than relying on
 * hal::Interrupt::init() being called.
 */
bool hal::Interrupt::initialized() {
    if (g_hal_interrupt_initialized) {
        return true;
    }
    
    /* Check if IDT is loaded by verifying IDTR has a valid base address */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;
    
    __asm__ volatile("sidt %0" : "=m"(idtr));
    
    /* IDT is initialized if base is non-zero and limit covers at least 32 entries */
    return (idtr.base != 0 && idtr.limit >= 255);  /* At least 32 entries * 8 bytes */
}

/**
 * @brief Check if MMU has been initialized
 * @return true if MMU/paging has been initialized (CR0.PG set)
 * 
 * This function checks the actual system state rather than relying on
 * hal::Mmu::init() being called.
 */
bool hal::Mmu::initialized() {
    if (g_hal_mmu_initialized) {
        return true;
    }
    
    /* Check if paging is enabled by reading CR0 */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    /* Paging is enabled if CR0.PG (bit 31) is set */
    return (cr0 & 0x80000000) != 0;
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
 * @brief Clean cache for a memory region (i686 - no-op)
 * 
 * On x86, caches are DMA-coherent, so this is a no-op.
 * 
 * @param addr Virtual address of the region start
 * @param size Size of the region in bytes
 */
void hal::Cache::clean(void *addr, size_t size) {
    (void)addr;
    (void)size;
    /* x86 caches are DMA-coherent - no action needed */
}

/**
 * @brief Invalidate cache for a memory region (i686 - no-op)
 * 
 * On x86, caches are DMA-coherent, so this is a no-op.
 * 
 * @param addr Virtual address of the region start
 * @param size Size of the region in bytes
 */
void hal::Cache::invalidate(void *addr, size_t size) {
    (void)addr;
    (void)size;
    /* x86 caches are DMA-coherent - no action needed */
}

/**
 * @brief Clean and invalidate cache for a memory region (i686 - no-op)
 * 
 * On x86, caches are DMA-coherent, so this is a no-op.
 * 
 * @param addr Virtual address of the region start
 * @param size Size of the region in bytes
 */
void hal::Cache::clean_invalidate(void *addr, size_t size) {
    (void)addr;
    (void)size;
    /* x86 caches are DMA-coherent - no action needed */
}
