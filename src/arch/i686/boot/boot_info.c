/**
 * @file boot_info.c
 * @brief i686 Boot Information Initialization
 * 
 * This file implements the conversion from Multiboot information
 * to the standardized boot_info_t structure for i686 architecture.
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
 * @brief Initialize boot info from Multiboot (i686)
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
        uintptr_t mmap_addr = PHYS_TO_VIRT(mbi->mmap_addr);
        uintptr_t mmap_end = mmap_addr + mbi->mmap_length;
        uint32_t count = 0;
        
        while (mmap_addr < mmap_end && count < BOOT_MMAP_MAX_ENTRIES) {
            multiboot_memory_map_t *entry = (multiboot_memory_map_t *)mmap_addr;
            
            g_boot_info.mmap[count].base = entry->addr;
            g_boot_info.mmap[count].length = entry->len;
            g_boot_info.mmap[count].type = convert_mmap_type(entry->type);
            g_boot_info.mmap[count].reserved = 0;
            
            /* Calculate total usable memory */
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                g_boot_info.total_memory = 
                    (g_boot_info.total_memory > entry->addr + entry->len) ?
                    g_boot_info.total_memory : entry->addr + entry->len;
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
    /* This would use kprintf, but we keep it simple for now */
    /* Implementation can be added when needed */
}
