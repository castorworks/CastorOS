/**
 * @file boot_info.c
 * @brief x86_64 Boot Information Initialization
 * 
 * This file implements the conversion from Multiboot/Multiboot2 information
 * to the standardized boot_info_t structure for x86_64 architecture.
 * 
 * Note: x86_64 CastorOS currently uses Multiboot1 (same as i686) for
 * compatibility. Multiboot2 support can be added later.
 * 
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 8.1**
 */

#include <boot/boot_info.h>
#include <kernel/multiboot.h>
#include <lib/string.h>

/* PHYS_TO_VIRT is defined in types.h */

/* Global boot info structure */
static boot_info_t g_boot_info;

/**
 * @brief Convert Multiboot memory type to boot_mem_type_t
 */
static boot_mem_type_t convert_mmap_type(uint32_t mb_type) {
    switch (mb_type) {
        case MULTIBOOT_MEMORY_AVAILABLE:
            return BOOT_MEM_USABLE;
        case MULTIBOOT_MEMORY_RESERVED:
            return BOOT_MEM_RESERVED;
        case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
            return BOOT_MEM_ACPI_RECLAIMABLE;
        case MULTIBOOT_MEMORY_NVS:
            return BOOT_MEM_ACPI_NVS;
        case MULTIBOOT_MEMORY_BADRAM:
            return BOOT_MEM_BAD;
        default:
            return BOOT_MEM_RESERVED;
    }
}

/**
 * @brief Initialize boot info from Multiboot (x86_64)
 * 
 * This function handles Multiboot1 format. The x86_64 boot code
 * passes the Multiboot info pointer after converting to virtual address.
 */
boot_info_t *boot_info_init_multiboot(void *mbi_ptr) {
    multiboot_info_t *mbi = (multiboot_info_t *)mbi_ptr;
    
    /* Clear the structure */
    memset(&g_boot_info, 0, sizeof(g_boot_info));
    
    if (!mbi) {
        return NULL;
    }
    
    g_boot_info.boot_protocol = BOOT_PROTO_MULTIBOOT;
    
    /* ====== Memory Information ====== */
    
    if (mbi->flags & MULTIBOOT_INFO_MEM) {
        g_boot_info.mem_lower = mbi->mem_lower;
        g_boot_info.mem_upper = mbi->mem_upper;
        /* Convert KB to bytes for total */
        g_boot_info.total_memory = ((uint64_t)mbi->mem_lower + mbi->mem_upper) * 1024;
    }
    
    /* ====== Memory Map ====== */
    
    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        /* Parse memory map entries */
        /* Note: mbi->mmap_addr is physical, need to convert */
        uintptr_t mmap_addr = PHYS_TO_VIRT(mbi->mmap_addr);
        uintptr_t mmap_end = mmap_addr + mbi->mmap_length;
        uint32_t count = 0;
        
        while (mmap_addr < mmap_end && count < BOOT_MMAP_MAX_ENTRIES) {
            multiboot_memory_map_t *entry = (multiboot_memory_map_t *)mmap_addr;
            
            g_boot_info.mmap[count].base = entry->addr;
            g_boot_info.mmap[count].length = entry->len;
            g_boot_info.mmap[count].type = convert_mmap_type(entry->type);
            g_boot_info.mmap[count].reserved = 0;
            
            /* Track highest usable memory address */
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t region_end = entry->addr + entry->len;
                if (region_end > g_boot_info.total_memory) {
                    g_boot_info.total_memory = region_end;
                }
            }
            
            count++;
            mmap_addr += entry->size + sizeof(entry->size);
        }
        
        g_boot_info.mmap_count = count;
    }
    
    /* ====== Command Line ====== */
    
    if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
        g_boot_info.cmdline = (const char *)PHYS_TO_VIRT(mbi->cmdline);
    }
    
    /* ====== Framebuffer ====== */
    
    if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        g_boot_info.framebuffer.addr = mbi->framebuffer_addr;
        g_boot_info.framebuffer.width = mbi->framebuffer_width;
        g_boot_info.framebuffer.height = mbi->framebuffer_height;
        g_boot_info.framebuffer.pitch = mbi->framebuffer_pitch;
        g_boot_info.framebuffer.bpp = mbi->framebuffer_bpp;
        
        switch (mbi->framebuffer_type) {
            case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED:
                g_boot_info.framebuffer.type = BOOT_FB_TYPE_INDEXED;
                break;
            case MULTIBOOT_FRAMEBUFFER_TYPE_RGB:
                g_boot_info.framebuffer.type = BOOT_FB_TYPE_RGB;
                g_boot_info.framebuffer.red_pos = mbi->framebuffer_red_field_position;
                g_boot_info.framebuffer.red_size = mbi->framebuffer_red_mask_size;
                g_boot_info.framebuffer.green_pos = mbi->framebuffer_green_field_position;
                g_boot_info.framebuffer.green_size = mbi->framebuffer_green_mask_size;
                g_boot_info.framebuffer.blue_pos = mbi->framebuffer_blue_field_position;
                g_boot_info.framebuffer.blue_size = mbi->framebuffer_blue_mask_size;
                break;
            case MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT:
                g_boot_info.framebuffer.type = BOOT_FB_TYPE_TEXT;
                break;
            default:
                g_boot_info.framebuffer.type = BOOT_FB_TYPE_RGB;
                break;
        }
        
        g_boot_info.framebuffer.valid = true;
    }
    
    /* ====== Boot Modules ====== */
    
    if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count > 0) {
        multiboot_module_t *modules = (multiboot_module_t *)PHYS_TO_VIRT(mbi->mods_addr);
        uint32_t count = (mbi->mods_count < BOOT_MODULE_MAX_COUNT) ? 
                         mbi->mods_count : BOOT_MODULE_MAX_COUNT;
        
        for (uint32_t i = 0; i < count; i++) {
            g_boot_info.modules[i].start = modules[i].mod_start;
            g_boot_info.modules[i].end = modules[i].mod_end;
            if (modules[i].cmdline) {
                g_boot_info.modules[i].cmdline = (const char *)PHYS_TO_VIRT(modules[i].cmdline);
            } else {
                g_boot_info.modules[i].cmdline = NULL;
            }
        }
        
        g_boot_info.module_count = count;
    }
    
    /* ====== Architecture-Specific Info ====== */
    
    /* Store original multiboot info pointer for ACPI discovery */
    g_boot_info.arch_info = mbi_ptr;
    
    g_boot_info.valid = true;
    
    return &g_boot_info;
}

/**
 * @brief Initialize boot info from Multiboot2 (x86_64)
 * 
 * Placeholder for Multiboot2 support. Currently not implemented
 * as CastorOS uses Multiboot1 for x86_64.
 */
boot_info_t *boot_info_init_multiboot2(void *mbi) {
    /* TODO: Implement Multiboot2 parsing when needed */
    (void)mbi;
    return NULL;
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
