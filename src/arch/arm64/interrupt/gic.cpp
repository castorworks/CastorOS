/**
 * @file gic.c
 * @brief ARM Generic Interrupt Controller (GIC) Implementation
 * 
 * Implements GICv2 support for ARM64 interrupt handling.
 */

#include "../include/gic.h"
#include <hal/hal.h>
#include <kernel/user_irq.h>
#include <types.h>

/* Forward declaration for serial output */
extern "C" void serial_puts(const char *str);
extern "C" void serial_put_hex64(uint64_t value);

/* ============================================================================
 * Static Data
 * ========================================================================== */

/** GIC base addresses (virtual, after MMU setup) */
/* 设备寄存器经内核高半区映射访问：进程的 TTBR0 里没有设备映射 */
/* 地址来自设备树（gic_set_bases）；没有设置时用 QEMU virt 上的地址 */
static uint64_t gicd_phys = GICD_BASE;
static uint64_t gicc_phys = GICC_BASE;
static volatile uint32_t *gicd_base = (volatile uint32_t *)PHYS_TO_VIRT(GICD_BASE);
static volatile uint32_t *gicc_base = (volatile uint32_t *)PHYS_TO_VIRT(GICC_BASE);

void gic_set_bases(uint64_t distributor_phys, uint64_t cpu_interface_phys) {
    gicd_phys = distributor_phys;
    gicc_phys = cpu_interface_phys;
    gicd_base = (volatile uint32_t *)PHYS_TO_VIRT(distributor_phys);
    gicc_base = (volatile uint32_t *)PHYS_TO_VIRT(cpu_interface_phys);
}

uint64_t gic_distributor_base(void) {
    return gicd_phys;
}

uint64_t gic_cpu_interface_base(void) {
    return gicc_phys;
}

/** Number of supported interrupts */
static uint32_t gic_num_interrupts = 0;

/** GIC version */

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
 * @brief Set up the GIC for a CPU other than the boot CPU
 *
 * The distributor is shared and already running. What is per CPU: the registers
 * for interrupts 0-31 (SGIs and PPIs; each CPU sees its own copy at the same
 * address) and the CPU interface.
 */
void gic_init_secondary(void) {
    gicd_write(GICD_ICENABLER(0), 0xFFFFFFFF);
    gicd_write(GICD_ICPENDR(0), 0xFFFFFFFF);
    gicd_write(GICD_IGROUPR(0), 0x00000000);
    for (uint32_t i = 0; i < GIC_SPI_BASE / 4; i++) {
        gicd_write(GICD_IPRIORITYR(i), 0x80808080);
    }
    gicc_init();
}

/**
 * @brief Interrupt every other CPU with a software-generated interrupt (SGI 0-15)
 */
void gic_send_sgi_to_others(uint32_t sgi) {
    /* GICD_SGIR: bits [25:24] = 1 means "all CPUs except the requesting one" */
    gicd_write(0xF00, (1u << 24) | (sgi & 0xF));
}

/**
 * @brief Enable an interrupt
 *
 * For an SGI or a PPI this enables it on the calling CPU only.
 */
void gic_enable_irq(uint32_t irq) {
    if (irq >= gic_num_interrupts) {
        return;
    }

    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;

    /* For PPIs (16-31), set high priority */
    if (irq >= GIC_PPI_BASE && irq < GIC_SPI_BASE) {
        gic_set_priority(irq, GIC_PRIORITY_HIGH);
    }

    /* Ensure interrupt is in Group 0 */
    uint32_t group = gicd_read(GICD_IGROUPR(reg));
    group &= ~(1 << bit);
    gicd_write(GICD_IGROUPR(reg), group);

    gicd_write(GICD_ISENABLER(reg), 1 << bit);
}

bool gic_has_handler(uint32_t irq) {
    return irq < GIC_MAX_INTERRUPTS && irq_handlers[irq].handler != NULL;
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
 * @brief Handle IRQ (called from exception handler)
 */
void gic_handle_irq(void) {
    /* Acknowledge interrupt. For an SGI the value also names the CPU that sent it,
     * and the end-of-interrupt write has to give the whole value back. */
    uint32_t iar = gicc_read(GICC_IAR);
    uint32_t irq = iar & GICC_IAR_INTID_MASK;
    
    /* Check for spurious interrupt (1022 or 1023) */
    if (irq >= 1020) {
        return;
    }
    
    /* Dispatch to registered handler */
    if (irq < GIC_MAX_INTERRUPTS && irq_handlers[irq].handler != NULL) {
        irq_handlers[irq].handler(irq_handlers[irq].data);
    } else if (!kernel::UserIrq::raise(irq)) {
        /* Neither the kernel nor a user-space driver owns it: keep it quiet */
        gic_disable_irq(irq);
        serial_puts("Unhandled IRQ: ");
        serial_put_hex64(irq);
        serial_puts("\n");
    }
    
    /* Signal end of interrupt */
    gicc_write(GICC_EOIR, iar);
}

/**
 * @brief Register an interrupt handler
 */
void gic_register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data) {
    if (irq >= GIC_MAX_INTERRUPTS) {
        return;
    }
    
    irq_handlers[irq].handler = handler;
    irq_handlers[irq].data = data;
}

