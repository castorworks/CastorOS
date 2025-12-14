/**
 * @file gic.c
 * @brief ARM Generic Interrupt Controller (GIC) Implementation
 * 
 * Implements GICv2 support for ARM64 interrupt handling.
 * 
 * Requirements: 4.4, 6.3
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */

#include "../include/gic.h"
#include <hal/hal.h>
#include <types.h>

/* Forward declaration for serial output */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);

/* ============================================================================
 * Static Data
 * ========================================================================== */

/** GIC base addresses (virtual, after MMU setup) */
static volatile uint32_t *gicd_base = (volatile uint32_t *)GICD_BASE;
static volatile uint32_t *gicc_base = (volatile uint32_t *)GICC_BASE;

/** Number of supported interrupts */
static uint32_t gic_num_interrupts = 0;

/** GIC version */
static uint32_t gic_version = 2;

/** Interrupt handler table */
typedef struct {
    hal_interrupt_handler_t handler;
    void *data;
} irq_handler_entry_t;

static irq_handler_entry_t irq_handlers[GIC_MAX_INTERRUPTS];

/* ============================================================================
 * Register Access Functions
 * ========================================================================== */

/**
 * @brief Read GICD register
 */
static inline uint32_t gicd_read(uint32_t offset) {
    return gicd_base[offset / 4];
}

/**
 * @brief Write GICD register
 */
static inline void gicd_write(uint32_t offset, uint32_t value) {
    gicd_base[offset / 4] = value;
}

/**
 * @brief Read GICC register
 */
static inline uint32_t gicc_read(uint32_t offset) {
    return gicc_base[offset / 4];
}

/**
 * @brief Write GICC register
 */
static inline void gicc_write(uint32_t offset, uint32_t value) {
    gicc_base[offset / 4] = value;
}

/* ============================================================================
 * Distributor Functions
 * ========================================================================== */

/**
 * @brief Initialize the GIC Distributor
 */
static void gicd_init(void) {
    uint32_t typer;
    uint32_t i;
    
    serial_puts("  Initializing GIC Distributor...\n");
    
    /* Disable distributor */
    gicd_write(GICD_CTLR, 0);
    
    /* Read number of interrupt lines */
    typer = gicd_read(GICD_TYPER);
    gic_num_interrupts = ((typer & GICD_TYPER_ITLINES_MASK) + 1) * 32;
    if (gic_num_interrupts > GIC_MAX_INTERRUPTS) {
        gic_num_interrupts = GIC_MAX_INTERRUPTS;
    }
    
    serial_puts("  Number of interrupts: ");
    serial_put_hex64(gic_num_interrupts);
    serial_puts("\n");
    
    /* Disable all interrupts */
    for (i = 0; i < gic_num_interrupts / 32; i++) {
        gicd_write(GICD_ICENABLER(i), 0xFFFFFFFF);
    }
    
    /* Clear all pending interrupts */
    for (i = 0; i < gic_num_interrupts / 32; i++) {
        gicd_write(GICD_ICPENDR(i), 0xFFFFFFFF);
    }
    
    /* Set all interrupts to Group 0 (secure/FIQ by default, but we configure for IRQ) */
    /* Group 0 interrupts are delivered as IRQ when GICC_CTLR.FIQEn=0 */
    for (i = 0; i < gic_num_interrupts / 32; i++) {
        gicd_write(GICD_IGROUPR(i), 0x00000000);  /* All Group 0 */
    }
    serial_puts("  Set all interrupts to Group 0\n");
    
    /* Set default priority for all interrupts (lower value = higher priority) */
    for (i = 0; i < gic_num_interrupts / 4; i++) {
        gicd_write(GICD_IPRIORITYR(i), 0x80808080);  /* Medium priority */
    }
    
    /* Set all SPIs to target CPU 0 */
    for (i = GIC_SPI_BASE / 4; i < gic_num_interrupts / 4; i++) {
        gicd_write(GICD_ITARGETSR(i), 0x01010101);
    }
    
    /* Configure all SPIs as level-triggered */
    for (i = GIC_SPI_BASE / 16; i < gic_num_interrupts / 16; i++) {
        gicd_write(GICD_ICFGR(i), 0);
    }
    
    /* Enable distributor for Group 0 only */
    gicd_write(GICD_CTLR, GICD_CTLR_ENABLE);
    
    serial_puts("  GIC Distributor initialized (Group 0 enabled)\n");
}

/**
 * @brief Initialize the GIC CPU Interface
 */
static void gicc_init(void) {
    serial_puts("  Initializing GIC CPU Interface...\n");
    
    /* Disable CPU interface */
    gicc_write(GICC_CTLR, 0);
    
    /* Set priority mask to allow all priorities */
    gicc_write(GICC_PMR, GIC_PRIORITY_MASK_ALL);
    
    /* Set binary point to 0 (all priority bits used for preemption) */
    gicc_write(GICC_BPR, 0);
    
    /* Enable CPU interface for Group 0 only, FIQEn=0 so Group 0 goes to IRQ */
    gicc_write(GICC_CTLR, GICC_CTLR_ENABLE);
    
    serial_puts("  GIC CPU Interface initialized (Group 0 enabled, FIQ disabled)\n");
}

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Initialize the GIC
 */
void gic_init(void) {
    serial_puts("Initializing GIC...\n");
    
    /* Clear handler table */
    for (uint32_t i = 0; i < GIC_MAX_INTERRUPTS; i++) {
        irq_handlers[i].handler = NULL;
        irq_handlers[i].data = NULL;
    }
    
    /* Initialize distributor and CPU interface */
    gicd_init();
    gicc_init();
    
    serial_puts("GIC initialization complete\n");
}

/**
 * @brief Enable an interrupt
 */
void gic_enable_irq(uint32_t irq) {
    if (irq >= gic_num_interrupts) {
        serial_puts("GIC: IRQ ");
        serial_put_hex64(irq);
        serial_puts(" out of range (max=");
        serial_put_hex64(gic_num_interrupts);
        serial_puts(")\n");
        return;
    }
    
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    
    serial_puts("GIC: Enabling IRQ ");
    serial_put_hex64(irq);
    serial_puts(" (reg=");
    serial_put_hex64(reg);
    serial_puts(", bit=");
    serial_put_hex64(bit);
    serial_puts(")\n");
    
    /* For PPIs (16-31), set high priority */
    if (irq >= GIC_PPI_BASE && irq < GIC_SPI_BASE) {
        gic_set_priority(irq, GIC_PRIORITY_HIGH);
        serial_puts("GIC: Set PPI priority to HIGH (0x40)\n");
    }
    
    /* Ensure interrupt is in Group 0 */
    uint32_t group_reg = irq / 32;
    uint32_t group_bit = irq % 32;
    uint32_t group = gicd_read(GICD_IGROUPR(group_reg));
    group &= ~(1 << group_bit);  /* Clear bit = Group 0 */
    gicd_write(GICD_IGROUPR(group_reg), group);
    
    /* Enable the interrupt */
    gicd_write(GICD_ISENABLER(reg), 1 << bit);
    
    /* Verify it was enabled */
    uint32_t enabled = gicd_read(GICD_ISENABLER(reg));
    serial_puts("GIC: ISENABLER[");
    serial_put_hex64(reg);
    serial_puts("] = ");
    serial_put_hex64(enabled);
    serial_puts("\n");
    
    /* Check group */
    group = gicd_read(GICD_IGROUPR(group_reg));
    serial_puts("GIC: IGROUPR[");
    serial_put_hex64(group_reg);
    serial_puts("] = ");
    serial_put_hex64(group);
    serial_puts(" (bit ");
    serial_put_hex64(group_bit);
    serial_puts(" should be 0 for Group 0)\n");
}

/**
 * @brief Disable an interrupt
 */
void gic_disable_irq(uint32_t irq) {
    if (irq >= gic_num_interrupts) {
        return;
    }
    
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    
    gicd_write(GICD_ICENABLER(reg), 1 << bit);
}

/**
 * @brief Set interrupt priority
 */
void gic_set_priority(uint32_t irq, uint8_t priority) {
    if (irq >= gic_num_interrupts) {
        return;
    }
    
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    uint32_t mask = 0xFF << shift;
    
    uint32_t val = gicd_read(GICD_IPRIORITYR(reg));
    val = (val & ~mask) | ((uint32_t)priority << shift);
    gicd_write(GICD_IPRIORITYR(reg), val);
}

/**
 * @brief Set interrupt target CPU(s)
 */
void gic_set_target(uint32_t irq, uint8_t cpu_mask) {
    if (irq < GIC_SPI_BASE || irq >= gic_num_interrupts) {
        return;  /* Only SPIs can have targets set */
    }
    
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;
    uint32_t mask = 0xFF << shift;
    
    uint32_t val = gicd_read(GICD_ITARGETSR(reg));
    val = (val & ~mask) | ((uint32_t)cpu_mask << shift);
    gicd_write(GICD_ITARGETSR(reg), val);
}

/**
 * @brief Configure interrupt as edge or level triggered
 */
void gic_set_config(uint32_t irq, bool edge) {
    if (irq < GIC_SPI_BASE || irq >= gic_num_interrupts) {
        return;  /* Only SPIs can have config changed */
    }
    
    uint32_t reg = irq / 16;
    uint32_t shift = (irq % 16) * 2 + 1;  /* Config is in bit 1 of each 2-bit field */
    
    uint32_t val = gicd_read(GICD_ICFGR(reg));
    if (edge) {
        val |= (1 << shift);
    } else {
        val &= ~(1 << shift);
    }
    gicd_write(GICD_ICFGR(reg), val);
}

/**
 * @brief Acknowledge an interrupt
 */
uint32_t gic_acknowledge_irq(void) {
    return gicc_read(GICC_IAR) & GICC_IAR_INTID_MASK;
}

/**
 * @brief Signal end of interrupt handling
 */
void gic_end_irq(uint32_t irq) {
    gicc_write(GICC_EOIR, irq);
}

/**
 * @brief Send a Software Generated Interrupt (SGI)
 */
void gic_send_sgi(uint32_t irq, uint8_t target_list, uint8_t filter) {
    if (irq >= GIC_SGI_COUNT) {
        return;
    }
    
    uint32_t val = irq | ((uint32_t)target_list << 16) | ((uint32_t)filter << 24);
    gicd_write(GICD_SGIR, val);
}

/**
 * @brief Handle IRQ (called from exception handler)
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
void gic_handle_irq(void) {
    uint32_t irq;
    
    /* Acknowledge interrupt */
    irq = gic_acknowledge_irq();
    
    /* Check for spurious interrupt (1022 or 1023) */
    if (irq >= 1020) {
        /* Debug: Check why we're getting spurious interrupts */
        static int spurious_count = 0;
        if (spurious_count < 3) {
            serial_puts("GIC: Spurious IRQ ");
            serial_put_hex64(irq);
            serial_puts(", checking state...\n");
            
            /* Check GICD state */
            uint32_t pending = gicd_read(GICD_ISPENDR(0));
            serial_puts("  GICD_ISPENDR[0] = ");
            serial_put_hex64(pending);
            serial_puts("\n");
            
            uint32_t enabled = gicd_read(GICD_ISENABLER(0));
            serial_puts("  GICD_ISENABLER[0] = ");
            serial_put_hex64(enabled);
            serial_puts("\n");
            
            uint32_t ctlr = gicd_read(GICD_CTLR);
            serial_puts("  GICD_CTLR = ");
            serial_put_hex64(ctlr);
            serial_puts("\n");
            
            /* Check GICC state */
            uint32_t gicc_ctlr = gicc_read(GICC_CTLR);
            serial_puts("  GICC_CTLR = ");
            serial_put_hex64(gicc_ctlr);
            serial_puts("\n");
            
            uint32_t pmr = gicc_read(GICC_PMR);
            serial_puts("  GICC_PMR = ");
            serial_put_hex64(pmr);
            serial_puts("\n");
            
            uint32_t hppir = gicc_read(GICC_HPPIR);
            serial_puts("  GICC_HPPIR = ");
            serial_put_hex64(hppir);
            serial_puts("\n");
            
            spurious_count++;
        }
        return;
    }
    
    /* Dispatch to registered handler */
    if (irq < GIC_MAX_INTERRUPTS && irq_handlers[irq].handler != NULL) {
        irq_handlers[irq].handler(irq_handlers[irq].data);
    } else {
        serial_puts("Unhandled IRQ: ");
        serial_put_hex64(irq);
        serial_puts("\n");
    }
    
    /* Signal end of interrupt */
    gic_end_irq(irq);
}

/**
 * @brief Register an interrupt handler
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
 */
void gic_register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    if (irq >= GIC_MAX_INTERRUPTS) {
        return;
    }
    
    irq_handlers[irq].handler = handler;
    irq_handlers[irq].data = data;
}

/**
 * @brief Unregister an interrupt handler
 */
void gic_unregister_handler(uint32_t irq) {
    if (irq >= GIC_MAX_INTERRUPTS) {
        return;
    }
    
    irq_handlers[irq].handler = NULL;
    irq_handlers[irq].data = NULL;
}

/**
 * @brief Get GIC version
 */
uint32_t gic_get_version(void) {
    return gic_version;
}

/**
 * @brief Get number of supported interrupts
 */
uint32_t gic_get_num_interrupts(void) {
    return gic_num_interrupts;
}
