/**
 * @file boot_info.c
 * @brief ARM64 Boot Information Initialization
 * 
 * This file implements the conversion from Device Tree Blob (DTB)
 * information to the standardized boot_info_t structure for ARM64.
 * 
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 8.1**
 */

#include <boot/boot_info.h>
#include "../include/dtb.h"

/* Forward declaration for memset */
extern void *memset(void *s, int c, size_t n);

/* Global boot info structure */
static boot_info_t g_boot_info;

/**
 * @brief Initialize boot info from Device Tree Blob (ARM64)
 * 
 * This function parses the DTB and populates the boot_info_t structure
 * with memory regions and device information.
 * 
 * @param dtb Pointer to DTB (passed by bootloader in x0)
 * @return Pointer to populated boot_info_t, or NULL on failure
 */
boot_info_t *boot_info_init_dtb(void *dtb) {
    /* Clear the structure */
    memset(&g_boot_info, 0, sizeof(g_boot_info));
    
    if (!dtb) {
        return NULL;
    }
    
    g_boot_info.boot_protocol = BOOT_PROTO_DTB;
    
    /* Parse the DTB */
    dtb_info_t *dtb_info = dtb_parse(dtb);
    if (!dtb_info || !dtb_info->valid) {
        return NULL;
    }
    
    /* ====== Memory Information ====== */
    
    g_boot_info.total_memory = dtb_info->total_memory;
    
    /* Convert DTB memory regions to boot_info format */
    uint32_t count = 0;
    for (uint32_t i = 0; i < dtb_info->num_memory_regions && count < BOOT_MMAP_MAX_ENTRIES; i++) {
        const dtb_memory_region_t *region = &dtb_info->memory[i];
        
        g_boot_info.mmap[count].base = region->base;
        g_boot_info.mmap[count].length = region->size;
        g_boot_info.mmap[count].type = BOOT_MEM_USABLE;
        g_boot_info.mmap[count].reserved = 0;
        
        count++;
    }
    g_boot_info.mmap_count = count;
    
    /* Calculate mem_lower and mem_upper (for compatibility) */
    /* ARM64 typically doesn't have the same low/high memory split as x86 */
    g_boot_info.mem_lower = 0;  /* No conventional memory below 1MB on ARM64 */
    g_boot_info.mem_upper = g_boot_info.total_memory / 1024;  /* Convert to KB */
    
    /* ====== Command Line ====== */
    
    /* DTB may contain a /chosen node with bootargs */
    /* For now, leave as NULL - can be extended to parse /chosen */
    g_boot_info.cmdline = NULL;
    
    /* ====== Framebuffer ====== */
    
    /* ARM64 framebuffer info would come from DTB or UEFI GOP */
    /* For now, mark as not available */
    g_boot_info.framebuffer.valid = false;
    
    /* ====== Boot Modules ====== */
    
    /* DTB may contain initrd info in /chosen node */
    /* For now, no modules */
    g_boot_info.module_count = 0;
    
    /* ====== Architecture-Specific Info ====== */
    
    /* Store DTB pointer for later use */
    g_boot_info.arch_info = dtb;
    
    g_boot_info.valid = true;
    
    return &g_boot_info;
}

/**
 * @brief Get the global boot information structure
 */
boot_info_t *boot_info_get(void) {
    if (!g_boot_info.valid) {
        return NULL;
    }
    return &g_boot_info;
}

/**
 * @brief Check if boot information is valid
 */
bool boot_info_is_valid(void) {
    return g_boot_info.valid;
}

/**
 * @brief Get total usable memory from boot info
 */
uint64_t boot_info_get_total_memory(void) {
    return g_boot_info.total_memory;
}

/**
 * @brief Find a memory region by type
 */
const boot_mmap_entry_t *boot_info_find_memory(boot_mem_type_t type, uint32_t index) {
    uint32_t found = 0;
    
    for (uint32_t i = 0; i < g_boot_info.mmap_count; i++) {
        if (g_boot_info.mmap[i].type == type) {
            if (found == index) {
                return &g_boot_info.mmap[i];
            }
            found++;
        }
    }
    
    return NULL;
}

/**
 * @brief Print boot information summary
 */
void boot_info_print(void) {
    /* Implementation can be added when needed */
}

/* Stub implementations for unused init functions on ARM64 */

boot_info_t *boot_info_init_multiboot(void *mbi) {
    (void)mbi;
    return NULL;
}

boot_info_t *boot_info_init_multiboot2(void *mbi) {
    (void)mbi;
    return NULL;
}
