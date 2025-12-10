/**
 * @file dtb.h
 * @brief ARM64 Device Tree Blob (DTB) Parser Interface
 * 
 * This file defines the interface for parsing Device Tree Blob (DTB) data
 * structures used by ARM64 systems to describe hardware configuration.
 * 
 * The Device Tree is a data structure for describing hardware that is
 * passed to the kernel by the bootloader (UEFI/U-Boot). It contains
 * information about:
 *   - Memory regions and their sizes
 *   - Interrupt controllers and routing
 *   - Device addresses and properties
 *   - CPU topology
 * 
 * Requirements: 4.3 - Parse device information from Device Tree Blob (DTB)
 */

#ifndef _ARM64_DTB_H_
#define _ARM64_DTB_H_

#include <types.h>

/* ============================================================================
 * DTB Header Constants
 * ========================================================================== */

/** DTB magic number (big-endian: 0xD00DFEED) */
#define DTB_MAGIC           0xD00DFEED

/** DTB version we support */
#define DTB_VERSION_MIN     16
#define DTB_VERSION_MAX     17

/* ============================================================================
 * DTB Token Types (in structure block)
 * ========================================================================== */

#define FDT_BEGIN_NODE      0x00000001  /**< Start of a node */
#define FDT_END_NODE        0x00000002  /**< End of a node */
#define FDT_PROP            0x00000003  /**< Property */
#define FDT_NOP             0x00000004  /**< No operation (padding) */
#define FDT_END             0x00000009  /**< End of structure block */

/* ============================================================================
 * DTB Header Structure
 * ========================================================================== */

/**
 * @brief DTB Header (Flattened Device Tree Header)
 * 
 * All fields are stored in big-endian format.
 */
typedef struct {
    uint32_t magic;             /**< Magic number (0xD00DFEED) */
    uint32_t totalsize;         /**< Total size of DTB in bytes */
    uint32_t off_dt_struct;     /**< Offset to structure block */
    uint32_t off_dt_strings;    /**< Offset to strings block */
    uint32_t off_mem_rsvmap;    /**< Offset to memory reservation block */
    uint32_t version;           /**< DTB version */
    uint32_t last_comp_version; /**< Last compatible version */
    uint32_t boot_cpuid_phys;   /**< Physical CPU ID of boot CPU */
    uint32_t size_dt_strings;   /**< Size of strings block */
    uint32_t size_dt_struct;    /**< Size of structure block */
} __attribute__((packed)) dtb_header_t;

/* ============================================================================
 * Memory Region Information
 * ========================================================================== */

/** Maximum number of memory regions we can track */
#define DTB_MAX_MEMORY_REGIONS  8

/**
 * @brief Memory region descriptor
 */
typedef struct {
    uint64_t base;      /**< Base physical address */
    uint64_t size;      /**< Size in bytes */
} dtb_memory_region_t;

/* ============================================================================
 * Interrupt Controller Information
 * ========================================================================== */

/**
 * @brief GIC (Generic Interrupt Controller) information
 */
typedef struct {
    uint64_t distributor_base;  /**< GICD base address */
    uint64_t cpu_interface_base; /**< GICC base address (GICv2) */
    uint64_t redistributor_base; /**< GICR base address (GICv3) */
    uint32_t version;           /**< GIC version (2 or 3) */
    bool     found;             /**< Whether GIC was found in DTB */
} dtb_gic_info_t;

/* ============================================================================
 * Device Information
 * ========================================================================== */

/** Maximum number of devices we can track */
#define DTB_MAX_DEVICES     16

/** Maximum device name length */
#define DTB_MAX_NAME_LEN    32

/**
 * @brief Device descriptor
 */
typedef struct {
    char     name[DTB_MAX_NAME_LEN];  /**< Device name/compatible string */
    uint64_t base_addr;               /**< Base address */
    uint64_t size;                    /**< Size of memory region */
    uint32_t irq;                     /**< Primary IRQ number */
    bool     valid;                   /**< Whether this entry is valid */
} dtb_device_t;

/* ============================================================================
 * Parsed DTB Information
 * ========================================================================== */

/**
 * @brief Complete parsed DTB information
 */
typedef struct {
    /* DTB validity */
    bool valid;                 /**< Whether DTB was successfully parsed */
    
    /* Memory information */
    uint32_t num_memory_regions;
    dtb_memory_region_t memory[DTB_MAX_MEMORY_REGIONS];
    uint64_t total_memory;      /**< Total memory size in bytes */
    
    /* Interrupt controller */
    dtb_gic_info_t gic;
    
    /* Devices */
    uint32_t num_devices;
    dtb_device_t devices[DTB_MAX_DEVICES];
    
    /* Timer information */
    uint32_t timer_irq;         /**< ARM Generic Timer IRQ */
    bool     timer_found;
    
    /* Serial/UART information */
    uint64_t uart_base;         /**< Primary UART base address */
    uint32_t uart_irq;          /**< UART IRQ number */
    bool     uart_found;
} dtb_info_t;

/* ============================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Initialize and parse a Device Tree Blob
 * 
 * @param dtb_addr Physical address of the DTB (passed by bootloader)
 * @return Pointer to parsed DTB information, or NULL on failure
 * 
 * This function validates the DTB header, parses the structure block,
 * and extracts relevant hardware information including:
 *   - Memory regions
 *   - Interrupt controller configuration
 *   - Device addresses and IRQs
 */
dtb_info_t *dtb_parse(void *dtb_addr);

/**
 * @brief Get the parsed DTB information
 * 
 * @return Pointer to the global DTB info structure, or NULL if not parsed
 */
dtb_info_t *dtb_get_info(void);

/**
 * @brief Check if DTB has been successfully parsed
 * 
 * @return true if DTB is valid and parsed, false otherwise
 */
bool dtb_is_valid(void);

/**
 * @brief Find a device by compatible string
 * 
 * @param compatible The compatible string to search for
 * @return Pointer to device info, or NULL if not found
 */
const dtb_device_t *dtb_find_device(const char *compatible);

/**
 * @brief Get total system memory size
 * 
 * @return Total memory in bytes, or 0 if DTB not parsed
 */
uint64_t dtb_get_total_memory(void);

/**
 * @brief Get memory region by index
 * 
 * @param index Region index (0 to num_memory_regions-1)
 * @return Pointer to memory region, or NULL if index out of range
 */
const dtb_memory_region_t *dtb_get_memory_region(uint32_t index);

/**
 * @brief Get GIC information
 * 
 * @return Pointer to GIC info, or NULL if not found
 */
const dtb_gic_info_t *dtb_get_gic_info(void);

/**
 * @brief Print DTB summary to serial console (for debugging)
 */
void dtb_print_info(void);

#endif /* _ARM64_DTB_H_ */
