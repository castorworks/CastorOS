/**
 * @file boot_info.h
 * @brief Standardized Boot Information Structure
 * 
 * This file defines architecture-independent boot information structures
 * that abstract away differences between bootloader protocols:
 *   - Multiboot (i686)
 *   - Multiboot2 (x86_64)
 *   - Device Tree Blob / UEFI (ARM64)
 * 
 * The architecture-specific boot code is responsible for populating
 * the boot_info_t structure from the native bootloader format.
 * 
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 8.1, 8.2, 8.3, 8.4**
 */

#ifndef _BOOT_BOOT_INFO_H_
#define _BOOT_BOOT_INFO_H_

#include <types.h>

/* ============================================================================
 * Memory Map Types
 * ========================================================================== */

/**
 * @brief Memory region type enumeration
 * 
 * These types are architecture-independent and map to:
 *   - Multiboot memory types (i686/x86_64)
 *   - DTB memory node types (ARM64)
 */
typedef enum {
    BOOT_MEM_USABLE = 1,            /**< Available for general use */
    BOOT_MEM_RESERVED = 2,          /**< Reserved by firmware/hardware */
    BOOT_MEM_ACPI_RECLAIMABLE = 3,  /**< ACPI tables, reclaimable after parsing */
    BOOT_MEM_ACPI_NVS = 4,          /**< ACPI Non-Volatile Storage */
    BOOT_MEM_BAD = 5,               /**< Bad/defective memory */
    BOOT_MEM_KERNEL = 6,            /**< Kernel code/data */
    BOOT_MEM_BOOTLOADER = 7,        /**< Bootloader reserved */
} boot_mem_type_t;

/**
 * @brief Memory map entry
 * 
 * Describes a contiguous region of physical memory.
 */
typedef struct boot_mmap_entry {
    uint64_t base;              /**< Base physical address */
    uint64_t length;            /**< Length in bytes */
    boot_mem_type_t type;       /**< Memory region type */
    uint32_t reserved;          /**< Reserved for alignment */
} boot_mmap_entry_t;

/* Maximum number of memory map entries */
#define BOOT_MMAP_MAX_ENTRIES   64

/* ============================================================================
 * Framebuffer Information
 * ========================================================================== */

/**
 * @brief Framebuffer type enumeration
 */
typedef enum {
    BOOT_FB_TYPE_INDEXED = 0,   /**< Indexed color (palette-based) */
    BOOT_FB_TYPE_RGB = 1,       /**< Direct RGB color */
    BOOT_FB_TYPE_TEXT = 2,      /**< EGA text mode */
} boot_fb_type_t;

/**
 * @brief Framebuffer information structure
 * 
 * Describes the graphics framebuffer if available.
 */
typedef struct boot_framebuffer {
    uint64_t addr;              /**< Physical address of framebuffer */
    uint32_t width;             /**< Width in pixels */
    uint32_t height;            /**< Height in pixels */
    uint32_t pitch;             /**< Bytes per scanline */
    uint8_t  bpp;               /**< Bits per pixel */
    boot_fb_type_t type;        /**< Framebuffer type */
    
    /* RGB color field positions (for RGB type) */
    uint8_t  red_pos;           /**< Red field bit position */
    uint8_t  red_size;          /**< Red field bit size */
    uint8_t  green_pos;         /**< Green field bit position */
    uint8_t  green_size;        /**< Green field bit size */
    uint8_t  blue_pos;          /**< Blue field bit position */
    uint8_t  blue_size;         /**< Blue field bit size */
    
    bool     valid;             /**< Whether framebuffer info is valid */
} boot_framebuffer_t;

/* ============================================================================
 * Boot Module Information
 * ========================================================================== */

/**
 * @brief Boot module entry
 * 
 * Describes a module loaded by the bootloader (e.g., initrd, user programs).
 */
typedef struct boot_module {
    uint64_t start;             /**< Physical start address */
    uint64_t end;               /**< Physical end address */
    const char *cmdline;        /**< Module command line (may be NULL) */
} boot_module_t;

/* Maximum number of boot modules */
#define BOOT_MODULE_MAX_COUNT   16

/* ============================================================================
 * Main Boot Information Structure
 * ========================================================================== */

/**
 * @brief Unified boot information structure
 * 
 * This structure is populated by architecture-specific boot code and
 * provides a consistent interface for kernel initialization.
 * 
 * Usage:
 *   - i686: Populated from Multiboot info structure
 *   - x86_64: Populated from Multiboot/Multiboot2 info structure
 *   - ARM64: Populated from Device Tree Blob (DTB)
 */
typedef struct boot_info {
    /* ====== Memory Information ====== */
    
    uint64_t mem_lower;             /**< Lower memory size in KB (below 1MB) */
    uint64_t mem_upper;             /**< Upper memory size in KB (above 1MB) */
    uint64_t total_memory;          /**< Total usable memory in bytes */
    
    /* Memory map */
    boot_mmap_entry_t mmap[BOOT_MMAP_MAX_ENTRIES];  /**< Memory map array */
    uint32_t mmap_count;            /**< Number of valid memory map entries */
    
    /* ====== Command Line ====== */
    
    const char *cmdline;            /**< Kernel command line (may be NULL) */
    
    /* ====== Framebuffer ====== */
    
    boot_framebuffer_t framebuffer; /**< Framebuffer information */
    
    /* ====== Boot Modules ====== */
    
    boot_module_t modules[BOOT_MODULE_MAX_COUNT];   /**< Module array */
    uint32_t module_count;          /**< Number of loaded modules */
    
    /* ====== Architecture-Specific Information ====== */
    
    /**
     * @brief Architecture-specific boot data pointer
     * 
     * Points to architecture-specific data:
     *   - i686/x86_64: ACPI RSDP pointer
     *   - ARM64: DTB pointer
     */
    void *arch_info;
    
    /* ====== Boot Source Information ====== */
    
    /**
     * @brief Boot protocol identifier
     */
    enum {
        BOOT_PROTO_UNKNOWN = 0,
        BOOT_PROTO_MULTIBOOT = 1,   /**< Multiboot 1 (i686) */
        BOOT_PROTO_MULTIBOOT2 = 2,  /**< Multiboot 2 (x86_64) */
        BOOT_PROTO_DTB = 3,         /**< Device Tree Blob (ARM64) */
        BOOT_PROTO_UEFI = 4,        /**< UEFI direct boot */
    } boot_protocol;
    
    /* ====== Validity Flag ====== */
    
    bool valid;                     /**< Whether boot info is valid */
    
} boot_info_t;

/* ============================================================================
 * Global Boot Info Access
 * ========================================================================== */

/**
 * @brief Get the global boot information structure
 * 
 * @return Pointer to the boot_info_t structure, or NULL if not initialized
 * 
 * This function returns the boot information populated during early boot.
 * The returned pointer is valid for the lifetime of the kernel.
 */
boot_info_t *boot_info_get(void);

/**
 * @brief Check if boot information is valid
 * 
 * @return true if boot info has been successfully populated
 */
bool boot_info_is_valid(void);

/**
 * @brief Get total usable memory from boot info
 * 
 * @return Total usable memory in bytes, or 0 if not available
 */
uint64_t boot_info_get_total_memory(void);

/**
 * @brief Find a memory region by type
 * 
 * @param type Memory type to search for
 * @param index Index of the region (for multiple regions of same type)
 * @return Pointer to memory map entry, or NULL if not found
 */
const boot_mmap_entry_t *boot_info_find_memory(boot_mem_type_t type, uint32_t index);

/**
 * @brief Print boot information summary (for debugging)
 */
void boot_info_print(void);

/* ============================================================================
 * Architecture-Specific Initialization
 * ========================================================================== */

/**
 * @brief Initialize boot info from Multiboot (i686)
 * 
 * @param mbi Pointer to Multiboot info structure
 * @return Pointer to populated boot_info_t, or NULL on failure
 * 
 * This function is implemented in arch/i686/boot/boot_info.c
 */
boot_info_t *boot_info_init_multiboot(void *mbi);

/**
 * @brief Initialize boot info from Multiboot2 (x86_64)
 * 
 * @param mbi Pointer to Multiboot2 info structure
 * @return Pointer to populated boot_info_t, or NULL on failure
 * 
 * This function is implemented in arch/x86_64/boot/boot_info.c
 */
boot_info_t *boot_info_init_multiboot2(void *mbi);

/**
 * @brief Initialize boot info from Device Tree Blob (ARM64)
 * 
 * @param dtb Pointer to DTB
 * @return Pointer to populated boot_info_t, or NULL on failure
 * 
 * This function is implemented in arch/arm64/boot/boot_info.c
 */
boot_info_t *boot_info_init_dtb(void *dtb);

#endif /* _BOOT_BOOT_INFO_H_ */
