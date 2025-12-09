/**
 * @file apic.c
 * @brief Advanced Programmable Interrupt Controller Implementation (x86_64)
 * 
 * This file implements Local APIC and I/O APIC support for x86_64.
 * 
 * The APIC provides:
 *   - More interrupt vectors (256 vs 16 for PIC)
 *   - Per-CPU interrupt handling (essential for SMP)
 *   - Local timer for each CPU
 *   - Inter-Processor Interrupts (IPI)
 * 
 * Requirements: 6.3 - Initialize APIC on x86_64
 */

#include "apic.h"
#include <kernel/io.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

/* ============================================================================
 * Static Data
 * ========================================================================== */

/* Local APIC base address (virtual) */
static volatile uint32_t *lapic_base = NULL;

/* I/O APIC base address (virtual) */
static volatile uint32_t *ioapic_base = NULL;

/* APIC timer calibration value */
static uint32_t lapic_timer_ticks_per_ms = 0;

/* ============================================================================
 * MSR Access Functions
 * ========================================================================== */

/**
 * @brief Read Model Specific Register
 */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/**
 * @brief Write Model Specific Register
 */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/* ============================================================================
 * CPUID Functions
 * ========================================================================== */

/**
 * @brief Execute CPUID instruction
 */
static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, 
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

/* ============================================================================
 * Local APIC Register Access
 * ========================================================================== */

/**
 * @brief Read Local APIC register
 */
static inline uint32_t lapic_read(uint32_t reg) {
    return lapic_base[reg / 4];
}

/**
 * @brief Write Local APIC register
 */
static inline void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4] = value;
    /* Read back to ensure write completes (memory barrier) */
    (void)lapic_base[LAPIC_ID / 4];
}

/* ============================================================================
 * I/O APIC Register Access
 * ========================================================================== */

/**
 * @brief Read I/O APIC register
 */
static inline uint32_t ioapic_read(uint32_t reg) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_REGWIN / 4];
}

/**
 * @brief Write I/O APIC register
 */
static inline void ioapic_write(uint32_t reg, uint32_t value) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_REGWIN / 4] = value;
}

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Check if APIC is available
 */
bool apic_is_available(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (edx & (1 << 9)) != 0;  /* APIC bit in CPUID.01H:EDX[9] */
}

/**
 * @brief Disable legacy PIC
 */
void pic_disable(void) {
    /* Mask all interrupts on both PICs */
    outb(0x21, 0xFF);  /* Master PIC data port */
    outb(0xA1, 0xFF);  /* Slave PIC data port */
    LOG_DEBUG_MSG("  Legacy PIC disabled\n");
}

/**
 * @brief Initialize Local APIC
 */
void lapic_init(void) {
    LOG_INFO_MSG("Initializing Local APIC...\n");
    
    if (!apic_is_available()) {
        LOG_ERROR_MSG("APIC not available on this CPU!\n");
        return;
    }
    
    /* Get APIC base address from MSR */
    uint64_t apic_msr = rdmsr(MSR_APIC_BASE);
    uint64_t apic_phys = apic_msr & 0xFFFFF000;
    
    LOG_DEBUG_MSG("  APIC MSR: 0x%llx\n", (unsigned long long)apic_msr);
    LOG_DEBUG_MSG("  APIC physical base: 0x%llx\n", (unsigned long long)apic_phys);
    
    /* For now, use identity mapping (assumes APIC is in low memory)
     * In a full implementation, this should be properly mapped via VMM */
    lapic_base = (volatile uint32_t *)apic_phys;
    
    /* Enable APIC via MSR if not already enabled */
    if (!(apic_msr & MSR_APIC_BASE_ENABLE)) {
        apic_msr |= MSR_APIC_BASE_ENABLE;
        wrmsr(MSR_APIC_BASE, apic_msr);
        LOG_DEBUG_MSG("  APIC enabled via MSR\n");
    }
    
    /* Set spurious interrupt vector and enable APIC */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | APIC_SPURIOUS_VECTOR);
    
    /* Clear task priority to allow all interrupts */
    lapic_write(LAPIC_TPR, 0);
    
    /* Disable all LVT entries initially */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LAPIC_LVT_MASKED);
    
    /* Clear any pending errors */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);
    
    /* Send EOI to clear any pending interrupts */
    lapic_write(LAPIC_EOI, 0);
    
    uint32_t version = lapic_read(LAPIC_VERSION);
    uint32_t id = lapic_read(LAPIC_ID) >> 24;
    
    LOG_INFO_MSG("Local APIC initialized (ID=%u, Version=0x%x)\n", id, version & 0xFF);
}

/**
 * @brief Send End of Interrupt to Local APIC
 */
void lapic_eoi(void) {
    if (lapic_base) {
        lapic_write(LAPIC_EOI, 0);
    }
}

/**
 * @brief Get Local APIC ID
 */
uint32_t lapic_get_id(void) {
    if (lapic_base) {
        return lapic_read(LAPIC_ID) >> 24;
    }
    return 0;
}

/**
 * @brief Initialize I/O APIC
 */
void ioapic_init(void) {
    LOG_INFO_MSG("Initializing I/O APIC...\n");
    
    /* Use default I/O APIC base address
     * In a full implementation, this should be read from ACPI MADT */
    ioapic_base = (volatile uint32_t *)IOAPIC_DEFAULT_BASE;
    
    uint32_t id = (ioapic_read(IOAPIC_ID) >> 24) & 0x0F;
    uint32_t version = ioapic_read(IOAPIC_VERSION);
    uint32_t max_redir = ((version >> 16) & 0xFF) + 1;
    
    LOG_DEBUG_MSG("  I/O APIC ID: %u\n", id);
    LOG_DEBUG_MSG("  I/O APIC Version: 0x%x\n", version & 0xFF);
    LOG_DEBUG_MSG("  Max redirection entries: %u\n", max_redir);
    
    /* Mask all I/O APIC interrupts initially */
    for (uint32_t i = 0; i < max_redir; i++) {
        ioapic_mask_irq((uint8_t)i);
    }
    
    LOG_INFO_MSG("I/O APIC initialized\n");
}

/**
 * @brief Set I/O APIC redirection entry
 */
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, uint32_t flags) {
    if (!ioapic_base) return;
    
    uint32_t reg_low = IOAPIC_REDTBL_BASE + (irq * 2);
    uint32_t reg_high = reg_low + 1;
    
    /* Build redirection entry */
    uint64_t entry = vector | flags;
    entry |= ((uint64_t)dest_apic_id << 56);  /* Destination APIC ID */
    
    /* Write high dword first (contains destination) */
    ioapic_write(reg_high, (uint32_t)(entry >> 32));
    /* Write low dword (contains vector and flags) */
    ioapic_write(reg_low, (uint32_t)entry);
}

/**
 * @brief Mask an I/O APIC IRQ
 */
void ioapic_mask_irq(uint8_t irq) {
    if (!ioapic_base) return;
    
    uint32_t reg_low = IOAPIC_REDTBL_BASE + (irq * 2);
    uint32_t value = ioapic_read(reg_low);
    value |= IOAPIC_REDIR_MASKED;
    ioapic_write(reg_low, value);
}

/**
 * @brief Unmask an I/O APIC IRQ
 */
void ioapic_unmask_irq(uint8_t irq) {
    if (!ioapic_base) return;
    
    uint32_t reg_low = IOAPIC_REDTBL_BASE + (irq * 2);
    uint32_t value = ioapic_read(reg_low);
    value &= ~IOAPIC_REDIR_MASKED;
    ioapic_write(reg_low, value);
}

/**
 * @brief Calibrate APIC timer using PIT
 */
static void lapic_timer_calibrate(void) {
    /* Use PIT channel 2 for calibration */
    /* Set PIT to one-shot mode, ~10ms */
    #define PIT_FREQ 1193182
    #define CALIBRATE_MS 10
    uint16_t pit_count = (PIT_FREQ * CALIBRATE_MS) / 1000;
    
    /* Configure PIT channel 2 */
    outb(0x61, (inb(0x61) & 0xFD) | 0x01);  /* Enable speaker gate, disable speaker */
    outb(0x43, 0xB0);  /* Channel 2, lobyte/hibyte, mode 0 */
    outb(0x42, pit_count & 0xFF);
    outb(0x42, (pit_count >> 8) & 0xFF);
    
    /* Set APIC timer to max count */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);
    
    /* Wait for PIT to count down */
    while ((inb(0x61) & 0x20) == 0);
    
    /* Read APIC timer count */
    uint32_t apic_count = lapic_read(LAPIC_TIMER_CCR);
    uint32_t elapsed = 0xFFFFFFFF - apic_count;
    
    /* Calculate ticks per millisecond */
    lapic_timer_ticks_per_ms = elapsed / CALIBRATE_MS;
    
    LOG_DEBUG_MSG("  APIC timer calibrated: %u ticks/ms\n", lapic_timer_ticks_per_ms);
}

/**
 * @brief Initialize APIC timer
 */
void lapic_timer_init(uint32_t frequency_hz) {
    if (!lapic_base) return;
    
    LOG_INFO_MSG("Initializing APIC timer at %u Hz...\n", frequency_hz);
    
    /* Calibrate timer first */
    lapic_timer_calibrate();
    
    if (lapic_timer_ticks_per_ms == 0) {
        LOG_ERROR_MSG("APIC timer calibration failed!\n");
        return;
    }
    
    /* Calculate initial count for desired frequency */
    uint32_t ticks_per_interrupt = (lapic_timer_ticks_per_ms * 1000) / frequency_hz;
    
    /* Set up timer in periodic mode */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, APIC_TIMER_VECTOR | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_ICR, ticks_per_interrupt);
    
    LOG_INFO_MSG("APIC timer initialized (ICR=%u)\n", ticks_per_interrupt);
}
