/**
 * @file gic.h
 * @brief ARM Generic Interrupt Controller (GIC) Definitions
 * 
 * Supports GICv2 and GICv3 interrupt controllers.
 */

#ifndef _ARCH_ARM64_GIC_H_
#define _ARCH_ARM64_GIC_H_

#include <types.h>
#include <hal/hal.h>

/* ============================================================================
 * GIC Base Addresses (QEMU virt machine)
 * ========================================================================== */

/** GICv2 base addresses for QEMU virt machine */
#define GICD_BASE           0x08000000ULL   /**< Distributor base */
#define GICC_BASE           0x08010000ULL   /**< CPU Interface base */


/* ============================================================================
 * GIC Distributor (GICD) Registers
 * ========================================================================== */

/** GICD register offsets */
#define GICD_CTLR           0x000   /**< Distributor Control Register */
#define GICD_TYPER          0x004   /**< Interrupt Controller Type Register */
#define GICD_IGROUPR(n)     (0x080 + ((n) * 4))  /**< Interrupt Group Registers */
#define GICD_ISENABLER(n)   (0x100 + ((n) * 4))  /**< Interrupt Set-Enable Registers */
#define GICD_ICENABLER(n)   (0x180 + ((n) * 4))  /**< Interrupt Clear-Enable Registers */
#define GICD_ICPENDR(n)     (0x280 + ((n) * 4))  /**< Interrupt Clear-Pending Registers */
#define GICD_IPRIORITYR(n)  (0x400 + ((n) * 4))  /**< Interrupt Priority Registers */
#define GICD_ITARGETSR(n)   (0x800 + ((n) * 4))  /**< Interrupt Processor Targets Registers */
#define GICD_ICFGR(n)       (0xC00 + ((n) * 4))  /**< Interrupt Configuration Registers */

/** GICD_CTLR bits */
#define GICD_CTLR_ENABLE    (1 << 0)    /**< Enable Group 0 interrupts */

/** GICD_TYPER bits */
#define GICD_TYPER_ITLINES_MASK     0x1F    /**< IT Lines Number mask */

/* ============================================================================
 * GIC CPU Interface (GICC) Registers - GICv2
 * ========================================================================== */

/** GICC register offsets */
#define GICC_CTLR           0x000   /**< CPU Interface Control Register */
#define GICC_PMR            0x004   /**< Interrupt Priority Mask Register */
#define GICC_BPR            0x008   /**< Binary Point Register */
#define GICC_IAR            0x00C   /**< Interrupt Acknowledge Register */
#define GICC_EOIR           0x010   /**< End of Interrupt Register */

/** GICC_CTLR bits */
#define GICC_CTLR_ENABLE    (1 << 0)    /**< Enable signaling of interrupts */

/** GICC_IAR bits */
#define GICC_IAR_INTID_MASK     0x3FF   /**< Interrupt ID mask */

/* ============================================================================
 * Interrupt Numbers
 * ========================================================================== */

#define GIC_PPI_BASE        16      /**< Private Peripheral Interrupts (16-31) */
#define GIC_SPI_BASE        32      /**< Shared Peripheral Interrupts (32+) */


/** Maximum number of interrupts */
#define GIC_MAX_INTERRUPTS  1020

/* ============================================================================
 * Interrupt Priority
 * ========================================================================== */

#define GIC_PRIORITY_HIGH       0x40

/** Default priority mask (allow all priorities) */
#define GIC_PRIORITY_MASK_ALL   0xFF

/* ============================================================================
 * Function Declarations
 * ========================================================================== */

/**
 * @brief Initialize the GIC
 * 
 * Initializes both the Distributor and CPU Interface.
 * Detects GIC version and configures appropriately.
 */
void gic_init(void);

/** Per-CPU part of the GIC setup, on a CPU other than the boot CPU (after gic_init) */
void gic_init_secondary(void);

/**
 * @brief Tell the driver where the GIC is (physical addresses from the device tree)
 *
 * Call before gic_init(). Without it the driver uses the QEMU virt addresses.
 */
void gic_set_bases(uint64_t distributor_phys, uint64_t cpu_interface_phys);

/** Physical addresses in use */
uint64_t gic_distributor_base(void);
uint64_t gic_cpu_interface_base(void);

/**
 * @brief Enable an interrupt
 * @param irq Interrupt number
 */
void gic_enable_irq(uint32_t irq);

/** Send software-generated interrupt `sgi` (0-15) to every CPU except the calling one */
void gic_send_sgi_to_others(uint32_t sgi);

/**
 * @brief Disable an interrupt
 * @param irq Interrupt number
 */
void gic_disable_irq(uint32_t irq);

/** Whether the kernel itself has a handler registered for this interrupt */
bool gic_has_handler(uint32_t irq);

/**
 * @brief Set interrupt priority
 * @param irq Interrupt number
 * @param priority Priority value (0-255, lower = higher priority)
 */
void gic_set_priority(uint32_t irq, uint8_t priority);

/**
 * @brief Acknowledge an interrupt
 * 
 * Reads GICC_IAR to acknowledge the highest priority pending interrupt.
 * 
 * @return Interrupt number, or GICC_IAR_SPURIOUS if no interrupt pending
 */
uint32_t gic_acknowledge_irq(void);

/**
 * @brief Signal end of interrupt handling
 * @param irq Interrupt number that was handled
 */
void gic_end_irq(uint32_t irq);

/**
 * @brief Handle IRQ (called from exception handler)
 * 
 * Acknowledges the interrupt, dispatches to registered handler,
 * and signals end of interrupt.
 */
void gic_handle_irq(void);

/**
 * @brief Register an interrupt handler
 * @param irq Interrupt number
 * @param handler Handler function
 * @param data User data to pass to handler
 */
void gic_register_handler(uint32_t irq, hal_interrupt_handler_t handler, void *data);

#endif /* _ARCH_ARM64_GIC_H_ */
