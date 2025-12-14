/**
 * @file boot_info.c
 * @brief ARM64 Boot Information Initialization
 * 
 * This file implements the conversion from Device Tree Blob (DTB)
 * information to the standardized boot_info_t structure for ARM64.
 * 
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 1.1**
 */

#include <boot/boot_info.h>
#include "../include/dtb.h"

/* Forward declaration for memset */
extern void *memset(void *s, int c, size_t n);

/* Forward declarations for serial output (debugging) */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);

/* ============================================================================
 * Linker Symbols for Kernel Physical Address Range
 * ============================================================================ */

/**
 * Linker symbols defined in linker_arm64.ld
 * These mark the physical address boundaries of the kernel image.
 */
extern char _kernel_start[];    /* Kernel start address (physical) */
extern char _kernel_end[];      /* Kernel end address (physical) */

/* QEMU virt machine physical memory base */
#define ARM64_PHYS_MEM_BASE     0x40000000ULL

/* Kernel virtual base (TTBR1 region) */
#define ARM64_KERNEL_VIRT_BASE  0xFFFF000000000000ULL

/* Global boot info structure */
static boot_info_t g_boot_info;

/**
 * @brief Convert kernel physical address to virtual address
 */
static inline uint64_t phys_to_virt(uint64_t phys) {
    return phys - ARM64_PHYS_MEM_BASE + ARM64_KERNEL_VIRT_BASE;
}

/**
 * @brief Convert kernel virtual address to physical address
 */
static inline uint64_t virt_to_phys(uint64_t virt) {
    return virt - ARM64_KERNEL_VIRT_BASE + ARM64_PHYS_MEM_BASE;
}

/**
 * @brief DTB magic number (big-endian)
 */
#define DTB_MAGIC 0xD00DFEED

/**
 * @brief Convert 32-bit big-endian to host byte order
 */
static inline uint32_t be32_to_cpu(uint32_t be_val) {
    return ((be_val & 0xFF000000) >> 24) |
           ((be_val & 0x00FF0000) >> 8)  |
           ((be_val & 0x0000FF00) << 8)  |
           ((be_val & 0x000000FF) << 24);
}

/**
 * @brief Check if address contains valid DTB magic
 */
static bool is_valid_dtb(void *addr) {
    if (!addr) return false;
    uint32_t magic = be32_to_cpu(*(uint32_t *)addr);
    return magic == DTB_MAGIC;
}

/**
 * @brief Search for DTB at known QEMU virt machine locations
 * 
 * QEMU virt machine places DTB at various locations depending on
 * configuration. Common locations include:
 * - Passed in x0 by bootloader
 * - At top of RAM (RAM_BASE + RAM_SIZE - DTB_SIZE)
 * - At fixed offset from RAM base
 * 
 * @param hint DTB address hint (from x0)
 * @return Valid DTB address or NULL
 */
static void *find_dtb(void *hint) {
    /* First, try the hint (passed in x0) */
    if (is_valid_dtb(hint)) {
        serial_puts("boot_info: Found DTB at hint address ");
        serial_put_hex64((uint64_t)hint);
        serial_puts("\n");
        return hint;
    }
    
    /*
     * QEMU virt machine with -kernel option places DTB at specific locations.
     * For 128MB RAM (default), DTB is typically at:
     * - 0x40000000 + 128MB - 2MB = 0x47E00000 (physical)
     * - Or at 0x48000000 - 0x200000 for larger RAM
     * 
     * Common QEMU virt DTB locations to search:
     */
    static const uint64_t dtb_search_addrs[] = {
        0x40000000,         /* RAM base - sometimes DTB is here */
        0x44000000,         /* 64MB offset */
        0x47E00000,         /* 128MB - 2MB */
        0x48000000,         /* 128MB */
        0x4FE00000,         /* 256MB - 2MB */
        0x50000000,         /* 256MB */
        0x80000000,         /* 1GB */
        0x0,                /* End marker */
    };
    
    serial_puts("boot_info: Searching for DTB at known locations...\n");
    
    for (int i = 0; dtb_search_addrs[i] != 0; i++) {
        void *addr = (void *)dtb_search_addrs[i];
        if (is_valid_dtb(addr)) {
            serial_puts("boot_info: Found DTB at ");
            serial_put_hex64((uint64_t)addr);
            serial_puts("\n");
            return addr;
        }
    }
    
    serial_puts("boot_info: DTB not found at any known location\n");
    return NULL;
}

/**
 * @brief Initialize boot info from Device Tree Blob (ARM64)
 * 
 * This function parses the DTB and populates the boot_info_t structure
 * with memory regions and device information.
 * 
 * Key responsibilities:
 * 1. Parse DTB /memory node to extract memory regions
 * 2. Calculate kernel physical address range using linker symbols
 * 3. Mark kernel-occupied memory as reserved
 * 
 * @param dtb Pointer to DTB (passed by bootloader in x0)
 * @return Pointer to populated boot_info_t, or NULL on failure
 * 
 * **Feature: arm64-kernel-integration**
 * **Validates: Requirements 1.1**
 */
boot_info_t *boot_info_init_dtb(void *dtb) {
    /* Clear the structure */
    memset(&g_boot_info, 0, sizeof(g_boot_info));
    
    /* Try to find DTB - either from hint or by searching known locations */
    dtb = find_dtb(dtb);
    
    if (!dtb) {
        serial_puts("boot_info: DTB not found, cannot initialize\n");
        return NULL;
    }
    
    g_boot_info.boot_protocol = BOOT_PROTO_DTB;
    
    /* Parse the DTB */
    dtb_info_t *dtb_info = dtb_parse(dtb);
    if (!dtb_info || !dtb_info->valid) {
        serial_puts("boot_info: Failed to parse DTB\n");
        return NULL;
    }
    
    /* ====== Memory Information from DTB ====== */
    
    serial_puts("boot_info: Extracting memory information from DTB\n");
    
    g_boot_info.total_memory = dtb_info->total_memory;
    
    /* Convert DTB memory regions to boot_info format */
    uint32_t usable_count = 0;
    for (uint32_t i = 0; i < dtb_info->num_memory_regions && usable_count < BOOT_MMAP_MAX_ENTRIES; i++) {
        const dtb_memory_region_t *region = &dtb_info->memory[i];
        
        /* Add usable memory region */
        g_boot_info.mmap[usable_count].base = region->base;
        g_boot_info.mmap[usable_count].length = region->size;
        g_boot_info.mmap[usable_count].type = BOOT_MEM_USABLE;
        g_boot_info.mmap[usable_count].reserved = 0;
        
        serial_puts("  Memory region ");
        serial_put_hex64(usable_count);
        serial_puts(": base=");
        serial_put_hex64(region->base);
        serial_puts(", size=");
        serial_put_hex64(region->size);
        serial_puts(" (");
        serial_put_hex64(region->size / (1024 * 1024));
        serial_puts(" MB)\n");
        
        usable_count++;
    }
    g_boot_info.mmap_count = usable_count;
    
    /* ====== Kernel Physical Address Range ====== */
    
    /*
     * Calculate kernel physical address range using linker symbols.
     * On ARM64, _kernel_start and _kernel_end are physical addresses
     * as defined in linker_arm64.ld (starting at KERNEL_PHYS_BASE = 0x40100000).
     * 
     * **Validates: Requirements 1.1 - Kernel physical address range detection**
     */
    uint64_t kernel_phys_start = (uint64_t)(uintptr_t)_kernel_start;
    uint64_t kernel_phys_end = (uint64_t)(uintptr_t)_kernel_end;
    
    /* Align kernel end to page boundary (4KB) */
    kernel_phys_end = (kernel_phys_end + 0xFFF) & ~0xFFFULL;
    
    serial_puts("boot_info: Kernel physical range:\n");
    serial_puts("  _kernel_start = ");
    serial_put_hex64(kernel_phys_start);
    serial_puts("\n");
    serial_puts("  _kernel_end   = ");
    serial_put_hex64(kernel_phys_end);
    serial_puts("\n");
    serial_puts("  Kernel size   = ");
    serial_put_hex64(kernel_phys_end - kernel_phys_start);
    serial_puts(" bytes (");
    serial_put_hex64((kernel_phys_end - kernel_phys_start) / 1024);
    serial_puts(" KB)\n");
    
    /* Add kernel region as reserved in memory map */
    if (g_boot_info.mmap_count < BOOT_MMAP_MAX_ENTRIES) {
        uint32_t kernel_idx = g_boot_info.mmap_count;
        g_boot_info.mmap[kernel_idx].base = kernel_phys_start;
        g_boot_info.mmap[kernel_idx].length = kernel_phys_end - kernel_phys_start;
        g_boot_info.mmap[kernel_idx].type = BOOT_MEM_KERNEL;
        g_boot_info.mmap[kernel_idx].reserved = 0;
        g_boot_info.mmap_count++;
        
        serial_puts("  Added kernel region to memory map as BOOT_MEM_KERNEL\n");
    }
    
    /* Calculate mem_lower and mem_upper (for compatibility with x86 code) */
    /* ARM64 typically doesn't have the same low/high memory split as x86 */
    g_boot_info.mem_lower = 0;  /* No conventional memory below 1MB on ARM64 */
    g_boot_info.mem_upper = g_boot_info.total_memory / 1024;  /* Convert to KB */
    
    serial_puts("boot_info: Total memory = ");
    serial_put_hex64(g_boot_info.total_memory);
    serial_puts(" bytes (");
    serial_put_hex64(g_boot_info.total_memory / (1024 * 1024));
    serial_puts(" MB)\n");
    
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
    
    serial_puts("boot_info: Initialization complete\n\n");
    
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
 * @brief Get kernel physical start address
 * 
 * Returns the physical address where the kernel image starts.
 * This is determined from the linker symbol _kernel_start.
 * 
 * @return Kernel physical start address
 */
uint64_t boot_info_get_kernel_phys_start(void) {
    return (uint64_t)(uintptr_t)_kernel_start;
}

/**
 * @brief Get kernel physical end address
 * 
 * Returns the physical address where the kernel image ends (page-aligned).
 * This is determined from the linker symbol _kernel_end.
 * 
 * @return Kernel physical end address (page-aligned)
 */
uint64_t boot_info_get_kernel_phys_end(void) {
    uint64_t end = (uint64_t)(uintptr_t)_kernel_end;
    /* Align to 4KB page boundary */
    return (end + 0xFFF) & ~0xFFFULL;
}

/**
 * @brief Print boot information summary
 */
void boot_info_print(void) {
    if (!g_boot_info.valid) {
        serial_puts("boot_info: Not initialized\n");
        return;
    }
    
    serial_puts("\n=== Boot Information Summary ===\n");
    serial_puts("Boot protocol: DTB\n");
    serial_puts("Total memory: ");
    serial_put_hex64(g_boot_info.total_memory);
    serial_puts(" bytes (");
    serial_put_hex64(g_boot_info.total_memory / (1024 * 1024));
    serial_puts(" MB)\n");
    
    serial_puts("Memory map entries: ");
    serial_put_hex64(g_boot_info.mmap_count);
    serial_puts("\n");
    
    for (uint32_t i = 0; i < g_boot_info.mmap_count; i++) {
        serial_puts("  [");
        serial_put_hex64(i);
        serial_puts("] ");
        serial_put_hex64(g_boot_info.mmap[i].base);
        serial_puts(" - ");
        serial_put_hex64(g_boot_info.mmap[i].base + g_boot_info.mmap[i].length);
        serial_puts(" type=");
        serial_put_hex64(g_boot_info.mmap[i].type);
        serial_puts("\n");
    }
    
    serial_puts("Kernel physical range: ");
    serial_put_hex64(boot_info_get_kernel_phys_start());
    serial_puts(" - ");
    serial_put_hex64(boot_info_get_kernel_phys_end());
    serial_puts("\n");
    
    serial_puts("================================\n\n");
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
