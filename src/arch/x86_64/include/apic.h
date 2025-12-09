/**
 * @file apic.h
 * @brief Advanced Programmable Interrupt Controller (x86_64)
 * 
 * This header defines the Local APIC and I/O APIC interfaces for x86_64.
 * 
 * The APIC system consists of:
 *   - Local APIC: One per CPU core, handles local interrupts and IPI
 *   - I/O APIC: Routes external interrupts to Local APICs
 * 
 * Requirements: 6.3 - Initialize APIC on x86_64
 */

#ifndef _ARCH_X86_64_APIC_H_
#define _ARCH_X86_64_APIC_H_

#include <types.h>

/* ============================================================================
 * Local APIC Registers (memory-mapped at APIC_BASE)
 * ========================================================================== */

/* Default Local APIC base address (can be changed via MSR) */
#define LAPIC_DEFAULT_BASE      0xFEE00000

/* Local APIC register offsets */
#define LAPIC_ID                0x020   /* Local APIC ID */
#define LAPIC_VERSION           0x030   /* Local APIC Version */
#define LAPIC_TPR               0x080   /* Task Priority Register */
#define LAPIC_APR               0x090   /* Arbitration Priority Register */
#define LAPIC_PPR               0x0A0   /* Processor Priority Register */
#define LAPIC_EOI               0x0B0   /* End of Interrupt */
#define LAPIC_RRD               0x0C0   /* Remote Read Register */
#define LAPIC_LDR               0x0D0   /* Logical Destination Register */
#define LAPIC_DFR               0x0E0   /* Destination Format Register */
#define LAPIC_SVR               0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ISR               0x100   /* In-Service Register (8 x 32-bit) */
#define LAPIC_TMR               0x180   /* Trigger Mode Register (8 x 32-bit) */
#define LAPIC_IRR               0x200   /* Interrupt Request Register (8 x 32-bit) */
#define LAPIC_ESR               0x280   /* Error Status Register */
#define LAPIC_CMCI              0x2F0   /* LVT Corrected Machine Check Interrupt */
#define LAPIC_ICR_LOW           0x300   /* Interrupt Command Register (low 32 bits) */
#define LAPIC_ICR_HIGH          0x310   /* Interrupt Command Register (high 32 bits) */
#define LAPIC_LVT_TIMER         0x320   /* LVT Timer Register */
#define LAPIC_LVT_THERMAL       0x330   /* LVT Thermal Sensor Register */
#define LAPIC_LVT_PERF          0x340   /* LVT Performance Counter Register */
#define LAPIC_LVT_LINT0         0x350   /* LVT LINT0 Register */
#define LAPIC_LVT_LINT1         0x360   /* LVT LINT1 Register */
#define LAPIC_LVT_ERROR         0x370   /* LVT Error Register */
#define LAPIC_TIMER_ICR         0x380   /* Timer Initial Count Register */
#define LAPIC_TIMER_CCR         0x390   /* Timer Current Count Register */
#define LAPIC_TIMER_DCR         0x3E0   /* Timer Divide Configuration Register */

/* Spurious Vector Register bits */
#define LAPIC_SVR_ENABLE        0x100   /* APIC Software Enable */
#define LAPIC_SVR_FOCUS         0x200   /* Focus Processor Checking */

/* LVT Entry bits */
#define LAPIC_LVT_MASKED        0x10000 /* Interrupt masked */
#define LAPIC_LVT_LEVEL         0x08000 /* Level triggered */
#define LAPIC_LVT_REMOTE_IRR    0x04000 /* Remote IRR (read-only) */
#define LAPIC_LVT_ACTIVE_LOW    0x02000 /* Active low polarity */
#define LAPIC_LVT_SEND_PENDING  0x01000 /* Send pending (read-only) */

/* LVT Delivery Mode */
#define LAPIC_LVT_DM_FIXED      0x000   /* Fixed delivery */
#define LAPIC_LVT_DM_SMI        0x200   /* SMI delivery */
#define LAPIC_LVT_DM_NMI        0x400   /* NMI delivery */
#define LAPIC_LVT_DM_INIT       0x500   /* INIT delivery */
#define LAPIC_LVT_DM_EXTINT     0x700   /* ExtINT delivery */

/* Timer Divide Configuration values */
#define LAPIC_TIMER_DIV_1       0x0B
#define LAPIC_TIMER_DIV_2       0x00
#define LAPIC_TIMER_DIV_4       0x01
#define LAPIC_TIMER_DIV_8       0x02
#define LAPIC_TIMER_DIV_16      0x03
#define LAPIC_TIMER_DIV_32      0x08
#define LAPIC_TIMER_DIV_64      0x09
#define LAPIC_TIMER_DIV_128     0x0A

/* Timer Mode */
#define LAPIC_TIMER_ONESHOT     0x00000 /* One-shot mode */
#define LAPIC_TIMER_PERIODIC    0x20000 /* Periodic mode */
#define LAPIC_TIMER_TSC_DEADLINE 0x40000 /* TSC-Deadline mode */

/* ============================================================================
 * I/O APIC Registers
 * ========================================================================== */

/* Default I/O APIC base address */
#define IOAPIC_DEFAULT_BASE     0xFEC00000

/* I/O APIC register select (indirect access) */
#define IOAPIC_REGSEL           0x00    /* Register Select */
#define IOAPIC_REGWIN           0x10    /* Register Window */

/* I/O APIC registers (accessed via REGSEL/REGWIN) */
#define IOAPIC_ID               0x00    /* I/O APIC ID */
#define IOAPIC_VERSION          0x01    /* I/O APIC Version */
#define IOAPIC_ARB              0x02    /* Arbitration ID */
#define IOAPIC_REDTBL_BASE      0x10    /* Redirection Table (entries 0-23) */

/* Redirection Table Entry bits (64-bit entry) */
#define IOAPIC_REDIR_MASKED     (1ULL << 16)    /* Interrupt masked */
#define IOAPIC_REDIR_LEVEL      (1ULL << 15)    /* Level triggered */
#define IOAPIC_REDIR_ACTIVE_LOW (1ULL << 13)    /* Active low polarity */
#define IOAPIC_REDIR_LOGICAL    (1ULL << 11)    /* Logical destination mode */

/* ============================================================================
 * MSR Definitions
 * ========================================================================== */

#define MSR_APIC_BASE           0x1B    /* APIC Base Address MSR */
#define MSR_APIC_BASE_ENABLE    (1ULL << 11)    /* Global APIC enable */
#define MSR_APIC_BASE_BSP       (1ULL << 8)     /* Bootstrap processor */
#define MSR_APIC_BASE_X2APIC    (1ULL << 10)    /* x2APIC mode enable */

/* ============================================================================
 * Interrupt Vectors
 * ========================================================================== */

#define APIC_SPURIOUS_VECTOR    0xFF    /* Spurious interrupt vector */
#define APIC_ERROR_VECTOR       0xFE    /* APIC error vector */
#define APIC_TIMER_VECTOR       0x20    /* APIC timer vector (same as PIC timer) */

/* ============================================================================
 * API Functions
 * ========================================================================== */

/**
 * @brief Check if APIC is available
 * @return true if APIC is supported by CPU
 */
bool apic_is_available(void);

/**
 * @brief Initialize Local APIC
 * 
 * Enables the Local APIC and configures basic settings.
 * Must be called before using any APIC features.
 */
void lapic_init(void);

/**
 * @brief Send End of Interrupt to Local APIC
 */
void lapic_eoi(void);

/**
 * @brief Get Local APIC ID
 * @return APIC ID of current CPU
 */
uint32_t lapic_get_id(void);

/**
 * @brief Initialize I/O APIC
 * 
 * Configures the I/O APIC for routing external interrupts.
 */
void ioapic_init(void);

/**
 * @brief Set I/O APIC redirection entry
 * @param irq IRQ number (0-23)
 * @param vector Interrupt vector to deliver
 * @param dest_apic_id Destination APIC ID
 * @param flags Additional flags (masked, level, etc.)
 */
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, uint32_t flags);

/**
 * @brief Mask an I/O APIC IRQ
 * @param irq IRQ number (0-23)
 */
void ioapic_mask_irq(uint8_t irq);

/**
 * @brief Unmask an I/O APIC IRQ
 * @param irq IRQ number (0-23)
 */
void ioapic_unmask_irq(uint8_t irq);

/**
 * @brief Initialize APIC timer
 * @param frequency_hz Desired timer frequency in Hz
 */
void lapic_timer_init(uint32_t frequency_hz);

/**
 * @brief Disable legacy PIC
 * 
 * Masks all PIC interrupts to prevent conflicts with APIC.
 */
void pic_disable(void);

#endif /* _ARCH_X86_64_APIC_H_ */
