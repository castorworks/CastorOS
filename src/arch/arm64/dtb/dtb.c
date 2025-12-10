/**
 * @file dtb.c
 * @brief ARM64 Device Tree Blob (DTB) Parser Implementation
 * 
 * This file implements a parser for Device Tree Blob (DTB) data structures
 * used by ARM64 systems. The DTB is passed by the bootloader and contains
 * hardware configuration information.
 * 
 * DTB Structure:
 *   - Header: Contains offsets and sizes
 *   - Memory Reservation Block: Reserved memory regions
 *   - Structure Block: Tree of nodes and properties
 *   - Strings Block: Property name strings
 * 
 * Requirements: 4.3 - Parse device information from Device Tree Blob (DTB)
 */

#include <types.h>
#include "../include/dtb.h"

/* Forward declarations for serial output */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);
extern void serial_putchar(char c);

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

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
 * @brief Convert 64-bit big-endian to host byte order
 */
static inline uint64_t be64_to_cpu(uint64_t be_val) {
    return ((be_val & 0xFF00000000000000ULL) >> 56) |
           ((be_val & 0x00FF000000000000ULL) >> 40) |
           ((be_val & 0x0000FF0000000000ULL) >> 24) |
           ((be_val & 0x000000FF00000000ULL) >> 8)  |
           ((be_val & 0x00000000FF000000ULL) << 8)  |
           ((be_val & 0x0000000000FF0000ULL) << 24) |
           ((be_val & 0x000000000000FF00ULL) << 40) |
           ((be_val & 0x00000000000000FFULL) << 56);
}

/**
 * @brief Simple string comparison
 */
static int dtb_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/**
 * @brief Check if string starts with prefix
 */
static bool dtb_strstart(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Copy string with length limit
 */
static void dtb_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/**
 * @brief Get string length
 */
static size_t dtb_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

/**
 * @brief Align value up to 4-byte boundary
 */
static inline uint32_t align4(uint32_t val) {
    return (val + 3) & ~3;
}

/* ============================================================================
 * Global State
 * ========================================================================== */

/** Global DTB information structure */
static dtb_info_t g_dtb_info;

/** Pointer to DTB base address */
static uint8_t *g_dtb_base = NULL;

/** Pointer to strings block */
static const char *g_strings_block = NULL;

/* ============================================================================
 * Property Parsing Helpers
 * ========================================================================== */

/**
 * @brief Read a reg property (address + size pairs)
 * 
 * @param data Property data
 * @param len Property length
 * @param addr_cells Number of cells for address
 * @param size_cells Number of cells for size
 * @param base Output: base address
 * @param size Output: size
 * @return true if successful
 */
static bool parse_reg_property(const uint32_t *data, uint32_t len,
                               uint32_t addr_cells, uint32_t size_cells,
                               uint64_t *base, uint64_t *size) {
    uint32_t expected_len = (addr_cells + size_cells) * sizeof(uint32_t);
    if (len < expected_len) {
        return false;
    }
    
    /* Parse base address */
    *base = 0;
    for (uint32_t i = 0; i < addr_cells; i++) {
        *base = (*base << 32) | be32_to_cpu(data[i]);
    }
    
    /* Parse size */
    *size = 0;
    for (uint32_t i = 0; i < size_cells; i++) {
        *size = (*size << 32) | be32_to_cpu(data[addr_cells + i]);
    }
    
    return true;
}

/* ============================================================================
 * Node Parsing State
 * ========================================================================== */

/** Current parsing context */
typedef struct {
    uint32_t addr_cells;    /**< #address-cells for current node */
    uint32_t size_cells;    /**< #size-cells for current node */
    int      depth;         /**< Current node depth */
    char     path[256];     /**< Current node path */
} parse_context_t;

/* ============================================================================
 * Structure Block Parsing
 * ========================================================================== */

/**
 * @brief Parse a single property
 * 
 * @param ctx Parse context
 * @param node_name Current node name
 * @param prop_name Property name
 * @param data Property data
 * @param len Property data length
 */
static void parse_property(parse_context_t *ctx, const char *node_name,
                          const char *prop_name, const uint32_t *data, 
                          uint32_t len) {
    /* Handle #address-cells and #size-cells */
    if (dtb_strcmp(prop_name, "#address-cells") == 0 && len >= 4) {
        ctx->addr_cells = be32_to_cpu(data[0]);
        return;
    }
    if (dtb_strcmp(prop_name, "#size-cells") == 0 && len >= 4) {
        ctx->size_cells = be32_to_cpu(data[0]);
        return;
    }
    
    /* Parse memory node */
    if (dtb_strstart(node_name, "memory") && 
        dtb_strcmp(prop_name, "reg") == 0) {
        uint64_t base, size;
        uint32_t offset = 0;
        uint32_t entry_size = (ctx->addr_cells + ctx->size_cells) * 4;
        
        while (offset + entry_size <= len && 
               g_dtb_info.num_memory_regions < DTB_MAX_MEMORY_REGIONS) {
            if (parse_reg_property(&data[offset / 4], len - offset,
                                   ctx->addr_cells, ctx->size_cells,
                                   &base, &size)) {
                uint32_t idx = g_dtb_info.num_memory_regions++;
                g_dtb_info.memory[idx].base = base;
                g_dtb_info.memory[idx].size = size;
                g_dtb_info.total_memory += size;
            }
            offset += entry_size;
        }
        return;
    }
    
    /* Parse GIC (interrupt controller) */
    if ((dtb_strstart(node_name, "intc") || 
         dtb_strstart(node_name, "gic") ||
         dtb_strstart(node_name, "interrupt-controller"))) {
        
        if (dtb_strcmp(prop_name, "compatible") == 0) {
            const char *compat = (const char *)data;
            if (dtb_strstart(compat, "arm,gic-v3") ||
                dtb_strstart(compat, "arm,cortex-a15-gic") ||
                dtb_strstart(compat, "arm,gic-400")) {
                g_dtb_info.gic.found = true;
                if (dtb_strstart(compat, "arm,gic-v3")) {
                    g_dtb_info.gic.version = 3;
                } else {
                    g_dtb_info.gic.version = 2;
                }
            }
        }
        
        if (dtb_strcmp(prop_name, "reg") == 0 && g_dtb_info.gic.found) {
            uint64_t base, size;
            /* First reg entry is distributor */
            if (parse_reg_property(data, len, ctx->addr_cells, ctx->size_cells,
                                   &base, &size)) {
                g_dtb_info.gic.distributor_base = base;
            }
            /* Second reg entry is CPU interface (GICv2) or redistributor (GICv3) */
            uint32_t entry_size = (ctx->addr_cells + ctx->size_cells) * 4;
            if (len >= entry_size * 2) {
                if (parse_reg_property(&data[entry_size / 4], len - entry_size,
                                       ctx->addr_cells, ctx->size_cells,
                                       &base, &size)) {
                    if (g_dtb_info.gic.version == 3) {
                        g_dtb_info.gic.redistributor_base = base;
                    } else {
                        g_dtb_info.gic.cpu_interface_base = base;
                    }
                }
            }
        }
        return;
    }
    
    /* Parse timer */
    if (dtb_strstart(node_name, "timer")) {
        if (dtb_strcmp(prop_name, "compatible") == 0) {
            const char *compat = (const char *)data;
            if (dtb_strstart(compat, "arm,armv8-timer") ||
                dtb_strstart(compat, "arm,armv7-timer")) {
                g_dtb_info.timer_found = true;
            }
        }
        if (dtb_strcmp(prop_name, "interrupts") == 0 && len >= 12) {
            /* ARM timer has 4 interrupts, we want the physical timer (index 1) */
            /* Format: <type irq flags> for each interrupt */
            /* Skip first interrupt (secure physical), get second (non-secure physical) */
            g_dtb_info.timer_irq = be32_to_cpu(data[4]) + 16; /* SPI offset */
        }
        return;
    }
    
    /* Parse UART/serial */
    if (dtb_strstart(node_name, "pl011") || 
        dtb_strstart(node_name, "uart") ||
        dtb_strstart(node_name, "serial")) {
        
        if (dtb_strcmp(prop_name, "compatible") == 0) {
            const char *compat = (const char *)data;
            if (dtb_strstart(compat, "arm,pl011") ||
                dtb_strstart(compat, "arm,primecell")) {
                g_dtb_info.uart_found = true;
            }
        }
        
        if (dtb_strcmp(prop_name, "reg") == 0 && !g_dtb_info.uart_base) {
            uint64_t base, size;
            if (parse_reg_property(data, len, ctx->addr_cells, ctx->size_cells,
                                   &base, &size)) {
                g_dtb_info.uart_base = base;
            }
        }
        
        if (dtb_strcmp(prop_name, "interrupts") == 0 && len >= 4) {
            g_dtb_info.uart_irq = be32_to_cpu(data[0]) + 32; /* SPI offset */
        }
        return;
    }
    
    /* Track other devices */
    if (dtb_strcmp(prop_name, "compatible") == 0 && 
        g_dtb_info.num_devices < DTB_MAX_DEVICES) {
        /* Check if we already have this device */
        for (uint32_t i = 0; i < g_dtb_info.num_devices; i++) {
            if (dtb_strcmp(g_dtb_info.devices[i].name, node_name) == 0) {
                return; /* Already tracked */
            }
        }
        
        uint32_t idx = g_dtb_info.num_devices++;
        dtb_strncpy(g_dtb_info.devices[idx].name, node_name, DTB_MAX_NAME_LEN);
        g_dtb_info.devices[idx].valid = true;
    }
    
    if (dtb_strcmp(prop_name, "reg") == 0) {
        /* Find the device entry and update its address */
        for (uint32_t i = 0; i < g_dtb_info.num_devices; i++) {
            if (dtb_strcmp(g_dtb_info.devices[i].name, node_name) == 0 &&
                g_dtb_info.devices[i].base_addr == 0) {
                uint64_t base, size;
                if (parse_reg_property(data, len, ctx->addr_cells, ctx->size_cells,
                                       &base, &size)) {
                    g_dtb_info.devices[i].base_addr = base;
                    g_dtb_info.devices[i].size = size;
                }
                break;
            }
        }
    }
}

/**
 * @brief Parse the structure block
 * 
 * @param struct_block Pointer to structure block
 * @param struct_size Size of structure block
 * @return true if parsing succeeded
 */
static bool parse_structure_block(const uint8_t *struct_block, 
                                  uint32_t struct_size) {
    parse_context_t ctx = {
        .addr_cells = 2,  /* Default for ARM64 */
        .size_cells = 1,
        .depth = 0
    };
    ctx.path[0] = '\0';
    
    const uint32_t *p = (const uint32_t *)struct_block;
    const uint32_t *end = (const uint32_t *)(struct_block + struct_size);
    char current_node[64] = "";
    
    while (p < end) {
        uint32_t token = be32_to_cpu(*p++);
        
        switch (token) {
            case FDT_BEGIN_NODE: {
                /* Node name follows the token */
                const char *name = (const char *)p;
                size_t name_len = dtb_strlen(name);
                p = (const uint32_t *)((uintptr_t)p + align4(name_len + 1));
                
                /* Extract node name without unit address */
                dtb_strncpy(current_node, name, sizeof(current_node));
                char *at = current_node;
                while (*at && *at != '@') at++;
                *at = '\0';
                
                ctx.depth++;
                break;
            }
            
            case FDT_END_NODE:
                ctx.depth--;
                current_node[0] = '\0';
                break;
            
            case FDT_PROP: {
                if (p + 2 > end) return false;
                
                uint32_t len = be32_to_cpu(p[0]);
                uint32_t nameoff = be32_to_cpu(p[1]);
                p += 2;
                
                const char *prop_name = g_strings_block + nameoff;
                const uint32_t *prop_data = p;
                
                parse_property(&ctx, current_node, prop_name, prop_data, len);
                
                p = (const uint32_t *)((uintptr_t)p + align4(len));
                break;
            }
            
            case FDT_NOP:
                /* Skip */
                break;
            
            case FDT_END:
                return true;
            
            default:
                /* Unknown token */
                serial_puts("DTB: Unknown token: ");
                serial_put_hex64(token);
                serial_puts("\n");
                return false;
        }
    }
    
    return true;
}

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

dtb_info_t *dtb_parse(void *dtb_addr) {
    serial_puts("DTB: Parsing Device Tree at ");
    serial_put_hex64((uint64_t)dtb_addr);
    serial_puts("\n");
    
    /* Initialize global state */
    g_dtb_base = (uint8_t *)dtb_addr;
    
    /* Clear info structure */
    for (size_t i = 0; i < sizeof(g_dtb_info); i++) {
        ((uint8_t *)&g_dtb_info)[i] = 0;
    }
    
    /* Validate DTB header */
    if (!dtb_addr) {
        serial_puts("DTB: NULL address\n");
        return NULL;
    }
    
    const dtb_header_t *header = (const dtb_header_t *)dtb_addr;
    uint32_t magic = be32_to_cpu(header->magic);
    
    if (magic != DTB_MAGIC) {
        serial_puts("DTB: Invalid magic: ");
        serial_put_hex64(magic);
        serial_puts(" (expected 0xD00DFEED)\n");
        return NULL;
    }
    
    uint32_t version = be32_to_cpu(header->version);
    if (version < DTB_VERSION_MIN || version > DTB_VERSION_MAX) {
        serial_puts("DTB: Unsupported version: ");
        serial_put_hex64(version);
        serial_puts("\n");
        return NULL;
    }
    
    serial_puts("DTB: Valid header, version ");
    serial_put_hex64(version);
    serial_puts("\n");
    
    /* Get block offsets */
    uint32_t struct_offset = be32_to_cpu(header->off_dt_struct);
    uint32_t strings_offset = be32_to_cpu(header->off_dt_strings);
    uint32_t struct_size = be32_to_cpu(header->size_dt_struct);
    
    /* Set up strings block pointer */
    g_strings_block = (const char *)(g_dtb_base + strings_offset);
    
    /* Parse structure block */
    const uint8_t *struct_block = g_dtb_base + struct_offset;
    if (!parse_structure_block(struct_block, struct_size)) {
        serial_puts("DTB: Failed to parse structure block\n");
        return NULL;
    }
    
    g_dtb_info.valid = true;
    serial_puts("DTB: Parsing complete\n");
    
    return &g_dtb_info;
}

dtb_info_t *dtb_get_info(void) {
    if (!g_dtb_info.valid) {
        return NULL;
    }
    return &g_dtb_info;
}

bool dtb_is_valid(void) {
    return g_dtb_info.valid;
}

const dtb_device_t *dtb_find_device(const char *compatible) {
    for (uint32_t i = 0; i < g_dtb_info.num_devices; i++) {
        if (g_dtb_info.devices[i].valid &&
            dtb_strcmp(g_dtb_info.devices[i].name, compatible) == 0) {
            return &g_dtb_info.devices[i];
        }
    }
    return NULL;
}

uint64_t dtb_get_total_memory(void) {
    return g_dtb_info.total_memory;
}

const dtb_memory_region_t *dtb_get_memory_region(uint32_t index) {
    if (index >= g_dtb_info.num_memory_regions) {
        return NULL;
    }
    return &g_dtb_info.memory[index];
}

const dtb_gic_info_t *dtb_get_gic_info(void) {
    if (!g_dtb_info.gic.found) {
        return NULL;
    }
    return &g_dtb_info.gic;
}


void dtb_print_info(void) {
    if (!g_dtb_info.valid) {
        serial_puts("DTB: Not parsed or invalid\n");
        return;
    }
    
    serial_puts("\n=== Device Tree Information ===\n\n");
    
    /* Memory regions */
    serial_puts("Memory Regions: ");
    serial_put_hex64(g_dtb_info.num_memory_regions);
    serial_puts("\n");
    
    for (uint32_t i = 0; i < g_dtb_info.num_memory_regions; i++) {
        serial_puts("  [");
        serial_putchar('0' + i);
        serial_puts("] Base: ");
        serial_put_hex64(g_dtb_info.memory[i].base);
        serial_puts(", Size: ");
        serial_put_hex64(g_dtb_info.memory[i].size);
        serial_puts(" (");
        serial_put_hex64(g_dtb_info.memory[i].size / (1024 * 1024));
        serial_puts(" MB)\n");
    }
    
    serial_puts("Total Memory: ");
    serial_put_hex64(g_dtb_info.total_memory);
    serial_puts(" bytes (");
    serial_put_hex64(g_dtb_info.total_memory / (1024 * 1024));
    serial_puts(" MB)\n\n");
    
    /* GIC information */
    if (g_dtb_info.gic.found) {
        serial_puts("GIC (Generic Interrupt Controller):\n");
        serial_puts("  Version: ");
        serial_putchar('0' + g_dtb_info.gic.version);
        serial_puts("\n");
        serial_puts("  Distributor: ");
        serial_put_hex64(g_dtb_info.gic.distributor_base);
        serial_puts("\n");
        if (g_dtb_info.gic.version == 2) {
            serial_puts("  CPU Interface: ");
            serial_put_hex64(g_dtb_info.gic.cpu_interface_base);
        } else {
            serial_puts("  Redistributor: ");
            serial_put_hex64(g_dtb_info.gic.redistributor_base);
        }
        serial_puts("\n\n");
    } else {
        serial_puts("GIC: Not found\n\n");
    }
    
    /* Timer information */
    if (g_dtb_info.timer_found) {
        serial_puts("ARM Generic Timer:\n");
        serial_puts("  IRQ: ");
        serial_put_hex64(g_dtb_info.timer_irq);
        serial_puts("\n\n");
    }
    
    /* UART information */
    if (g_dtb_info.uart_found) {
        serial_puts("UART (PL011):\n");
        serial_puts("  Base: ");
        serial_put_hex64(g_dtb_info.uart_base);
        serial_puts("\n");
        serial_puts("  IRQ: ");
        serial_put_hex64(g_dtb_info.uart_irq);
        serial_puts("\n\n");
    }
    
    /* Other devices */
    if (g_dtb_info.num_devices > 0) {
        serial_puts("Other Devices: ");
        serial_put_hex64(g_dtb_info.num_devices);
        serial_puts("\n");
        
        for (uint32_t i = 0; i < g_dtb_info.num_devices; i++) {
            if (g_dtb_info.devices[i].valid) {
                serial_puts("  - ");
                serial_puts(g_dtb_info.devices[i].name);
                if (g_dtb_info.devices[i].base_addr) {
                    serial_puts(" @ ");
                    serial_put_hex64(g_dtb_info.devices[i].base_addr);
                }
                serial_puts("\n");
            }
        }
    }
    
    serial_puts("\n===============================\n\n");
}
