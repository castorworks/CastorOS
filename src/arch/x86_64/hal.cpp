/**
 * @file hal.c
 * @brief x86_64 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for x86_64 (AMD64/Intel 64-bit) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems (GDT64, IDT64, ISR64, IRQ64, VMM).
 */

#include <hal/hal.h>
#include <kernel/hw_access.h>
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
void hal::Cpu::init() {
    LOG_INFO_MSG("HAL: Initializing x86_64 CPU...\n");
    
    /* Initialize GDT with TSS
     * - Sets up 64-bit segment descriptors for kernel and user mode
     * - Configures TSS64 for privilege level transitions
     * - Default kernel stack at 0x90000, will be updated per-task
     */
    gdt64_init_cpu(0, 0x90000);
    
    g_hal_cpu_initialized = true;
    LOG_INFO_MSG("HAL: x86_64 CPU initialization complete\n");
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
 * @brief Initialize interrupt system (x86_64)
 * 
 * Initializes IDT64, ISR64 handlers, and IRQ64 handlers with PIC remapping.
 * 
 * Requirements: 1.1 - HAL initialization dispatch
 */
void hal::Interrupt::init() {
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
 * Architecture Information
 * ========================================================================== */


/**
 * @brief Get architecture name string
 * @return Architecture name
 */
const char *hal_arch_name(void) {
    return "x86_64";
}

/**
 * x86 没有固件给的设备表：设备由驱动自己探测（扫 PCI 配置空间）
 */
bool hal::Platform::find_device(struct device_info *info) {
    (void)info;
    return false;
}

bool hal::Platform::port_read(uint16_t port, uint32_t width, uint32_t *value) {
    *value = width == 1 ? hal::Port::read8(port) : width == 2 ? hal::Port::read16(port) : hal::Port::read32(port);
    return true;
}

bool hal::Platform::port_write(uint16_t port, uint32_t width, uint32_t value) {
    if (width == 1) hal::Port::write8(port, (uint8_t)value);
    else if (width == 2) hal::Port::write16(port, (uint16_t)value);
    else hal::Port::write32(port, value);
    return true;
}

void hal::Platform::set_user_ports(const struct hw_range *allowed, uint32_t count, bool allow) {
    for (uint32_t i = 0; i < count; i++) {
        if (allowed[i].kind == HW_PORTS) {
            tss64_io_allow((uint32_t)allowed[i].start, (uint32_t)allowed[i].count, allow);
        }
    }
}
