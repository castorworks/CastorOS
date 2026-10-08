/**
 * @file hal.c
 * @brief i686 Hardware Abstraction Layer Implementation
 * 
 * This file implements the HAL interface for i686 (x86 32-bit) architecture.
 * It provides unified initialization routines that dispatch to architecture-
 * specific subsystems (GDT, IDT, ISR, IRQ, VMM).
 */

#include <hal/hal.h>
#include <kernel/hw_access.h>
#include <drivers/x86/power.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/isr.h>
#include <kernel/irq.h>
#include <kernel/panic.h>
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
    gdt_init_cpu(0, 0x90000, 0x10);

    /* Floating point and SSE for user programs. The kernel saves and restores
     * these registers with FXSAVE/FXRSTOR when it switches user tasks, so the
     * CPU has to have them (every CPU model QEMU offers by default does). */
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    (void)ebx;
    if (!(edx & (1u << 24)) || !(edx & (1u << 25))) {
        PANIC("this CPU has no FXSAVE/SSE: cannot save the floating-point state of user tasks");
    }
    uint32_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1u << 2) | (1u << 3));    /* EM, TS: x87/SSE instructions do not trap */
    cr0 |= (1u << 1) | (1u << 5);       /* MP, NE: x87 errors arrive as #MF */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9) | (1u << 10);      /* OSFXSR, OSXMMEXCPT: SSE and FXSAVE allowed, SIMD errors as #XM */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    __asm__ volatile("fninit");
    
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
 * ========================================================================== */

/**
 * @brief Get architecture name string
 * @return Architecture name
 */
const char *hal_arch_name(void) {
    return "i686";
}

/**
 * x86 没有固件给的设备表：设备由 init 自己探测（扫 PCI 配置空间）。唯一的例外是电源键，
 * 它在哪里写在 ACPI 的表里
 */
bool hal::Platform::find_device(struct device_info *info) {
    return drivers::Power::find_device(info);
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
            tss_io_allow((uint32_t)allowed[i].start, (uint32_t)allowed[i].count, allow);
        }
    }
}

void hal::Platform::power_off() {
    drivers::Power::off();
}

void hal::Platform::reboot() {
    drivers::Power::reboot();
}
