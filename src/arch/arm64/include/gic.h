/**
 * @file gic.h
 * @brief ARM Generic Interrupt Controller (GIC) Definitions
 * 
 * Supports GICv2 and GICv3 interrupt controllers.
 * 
 * Requirements: 4.4, 6.3
 * 
 * **Feature: multi-arch-support, Property 8: Interrupt Handler Registration API Consistency**
 * **Validates: Requirements 6.4**
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

/** GICv3 base addresses for QEMU virt machine */
#define GICR_BASE           0x080A0000ULL   /**< Redistributor base */

/* ============================================================================
 * GIC Distributor (GICD) Registers
 * ========================================================================== */

/** GICD register offsets */
#define GICD_CTLR           0x000   /**< Distributor Control Register */
#define GICD_TYPER          0x004   /**< Interrupt Controller Type Register */
#define GICD_IIDR           0x008   /**< Distributor Implementer ID Register */
#define GICD_IGROUPR(n)     (0x080 + ((n) * 4))  /**< Interrupt Group Registers */
#define GICD_ISENABLER(n)   (0x100 + ((n) * 4))  /**< Interrupt Set-Enable Registers */
#define GICD_ICENABLER(n)   (0x180 + ((n) * 4))  /**< Interrupt Clear-Enable Registers */
#define GICD_ISPENDR(n)     (0x200 + ((n) * 4))  /**< Interrupt Set-Pending Registers */
#define GICD_ICPENDR(n)     (0x280 + ((n) * 4))  /**< Interrupt Clear-Pending Registers */
#define GICD_ISACTIVER(n)   (0x300 + ((n) * 4))  /**< Interrupt Set-Active Registers */
#define GICD_ICACTIVER(n)   (0x380 + ((n) * 4))  /**< Interrupt Clear-Active Registers */
#define GICD_IPRIORITYR(n)  (0x400 + ((n) * 4))  /**< Interrupt Priority Registers */
#define GICD_ITARGETSR(n)   (0x800 + ((n) * 4))  /**< Interrupt Processor Targets Registers */
#define GICD_ICFGR(n)       (0xC00 + ((n) * 4))  /**< Interrupt Configuration Registers */
#define GICD_SGIR           0xF00   /**< Software Generated Interrupt Register */

/** GICD_CTLR bits */
#define GICD_CTLR_ENABLE    (1 << 0)    /**< Enable Group 0 interrupts */
#define GICD_CTLR_ENABLE_G1 (1 << 1)    /**< Enable Group 1 interrupts */

/** GICD_TYPER bits */
#define GICD_TYPER_ITLINES_MASK     0x1F    /**< IT Lines Number mask */
#define GICD_TYPER_CPUNUM_SHIFT     5       /**< CPU Number shift */
#define GICD_TYPER_CPUNUM_MASK      0x7     /**< CPU Number mask */

/* ============================================================================
 * GIC CPU Interface (GICC) Registers - GICv2
 * ========================================================================== */

/** GICC register offsets */
#define GICC_CTLR           0x000   /**< CPU Interface Control Register */
#define GICC_PMR            0x004   /**< Interrupt Priority Mask Register */
#define GICC_BPR            0x008   /**< Binary Point Register */
#define GICC_IAR            0x00C   /**< Interrupt Acknowledge Register */
#define GICC_EOIR           0x010   /**< End of Interrupt Register */
#define GICC_RPR            0x014   /**< Running Priority Register */
#define GICC_HPPIR          0x018   /**< Highest Priority Pending Interrupt Register */
#define GICC_ABPR           0x01C   /**< Aliased Binary Point Register */
#define GICC_AIAR           0x020   /**< Aliased Interrupt Acknowledge Register */
#define GICC_AEOIR          0x024   /**< Aliased End of Interrupt Register */
#define GICC_AHPPIR         0x028   /**< Aliased Highest Priority Pending Interrupt Register */
#define GICC_IIDR           0x0FC   /**< CPU Interface Implementer ID Register */
#define GICC_DIR            0x1000  /**< Deactivate Interrupt Register */

/** GICC_CTLR bits */
#define GICC_CTLR_ENABLE    (1 << 0)    /**< Enable signaling of interrupts */
#define GICC_CTLR_ENABLE_G1 (1 << 1)    /**< Enable signaling of Group 1 interrupts */
#define GICC_CTLR_ACKCTL    (1 << 2)    /**< Acknowledge control */
#define GICC_CTLR_FIQEN     (1 << 3)    /**< FIQ enable */
#define GICC_CTLR_CBPR      (1 << 4)    /**< Common Binary Point Register */
#define GICC_CTLR_EOIMODE   (1 << 9)    /**< EOI mode */

/** GICC_IAR bits */
#define GICC_IAR_INTID_MASK     0x3FF   /**< Interrupt ID mask */
#define GICC_IAR_CPUID_SHIFT    10      /**< CPU ID shift */
#define GICC_IAR_CPUID_MASK     0x7     /**< CPU ID mask */
#define GICC_IAR_SPURIOUS       1023    /**< Spurious interrupt ID */

/* ============================================================================
 * Interrupt Numbers
 * ========================================================================== */

/** Interrupt number ranges */
#define GIC_SGI_BASE        0       /**< Software Generated Interrupts (0-15) */
#define GIC_SGI_COUNT       16
#define GIC_PPI_BASE        16      /**< Private Peripheral Interrupts (16-31) */
#define GIC_PPI_COUNT       16
#define GIC_SPI_BASE        32      /**< Shared Peripheral Interrupts (32+) */

/** Common interrupt numbers for QEMU virt machine */
#define GIC_INTID_VTIMER    27      /**< Virtual timer PPI */
#define GIC_INTID_PTIMER    30      /**< Physical timer PPI */
#define GIC_INTID_UART0     33      /**< UART0 SPI */

/** Maximum number of interrupts */
#define GIC_MAX_INTERRUPTS  1020

/* ============================================================================
 * Interrupt Priority
 * ========================================================================== */

/** Priority levels (lower value = higher priority) */
#define GIC_PRIORITY_HIGHEST    0x00
#define GIC_PRIORITY_HIGH       0x40
#define GIC_PRIORITY_MEDIUM     0x80
#define GIC_PRIORITY_LOW        0xC0
#define GIC_PRIORITY_LOWEST     0xF0

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

/**
 * @brief Enable an interrupt
 * @param irq Interrupt number
 */
void gic_enable_irq(uint32_t irq);

/**
 * @brief Disable an interrupt
 * @param irq Interrupt number
 */
void gic_disable_irq(uint32_t irq);

/**
 * @brief Set interrupt priority
 * @param irq Interrupt number
 * @param priority Priority value (0-255, lower = higher priority)
 */
void gic_set_priority(uint32_t irq, uint8_t priority);

/**
 * @brief Set interrupt target CPU(s)
 * @param irq Interrupt number
 * @param cpu_mask Bitmask of target CPUs
 */
void gic_set_target(uint32_t irq, uint8_t cpu_mask);

/**
 * @brief Configure interrupt as edge or level triggered
 * @param irq Interrupt number
 * @param edge true for edge-triggered, false for level-triggered
 */
void gic_set_config(uint32_t irq, bool edge);

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
 * @brief Send a Software Generated Interrupt (SGI)
 * @param irq SGI number (0-15)
 * @param target_list Target CPU list
 * @param filter Target filter (0=list, 1=all except self, 2=self only)
 */
void gic_send_sgi(uint32_t irq, uint8_t target_list, uint8_t filter);

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

/**
 * @brief Unregister an interrupt handler
 * @param irq Interrupt number
 */
void gic_unregister_handler(uint32_t irq);

/**
 * @brief Get GIC version
 * @return GIC version (2 or 3)
 */
uint32_t gic_get_version(void);

/**
 * @brief Get number of supported interrupts
 * @return Maximum interrupt number + 1
 */
uint32_t gic_get_num_interrupts(void);

#endif /* _ARCH_ARM64_GIC_H_ */
