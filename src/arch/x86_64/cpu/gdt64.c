/**
 * @file gdt64.c
 * @brief Global Descriptor Table Implementation (x86_64)
 * 
 * This file implements the 64-bit GDT and TSS for x86_64 architecture.
 * 
 * In long mode (64-bit), segmentation is largely disabled:
 *   - Base address is always treated as 0 for code/data segments
 *   - Limit is ignored
 *   - Only CS.L (Long mode), CS.D, and DPL are meaningful for code segments
 *   - Data segments only use DPL
 * 
 * However, GDT is still required for:
 *   - Defining privilege levels (Ring 0 vs Ring 3)
 *   - TSS for stack switching during privilege transitions
 *   - SYSCALL/SYSRET configuration
 * 
 * Requirements: 3.3 - Configure 64-bit GDT with appropriate code and data segments
 */

#include "gdt64.h"
#include <types.h>
#include <lib/string.h>
#include <lib/klog.h>

/* ============================================================================
 * GDT Table Layout
 * ============================================================================
 * Index 0: Null descriptor (required)
 * Index 1: Kernel code segment (64-bit, Ring 0)
 * Index 2: Kernel data segment (Ring 0)
 * Index 3: User code segment (64-bit, Ring 3)
 * Index 4: User data segment (Ring 3)
 * Index 5-6: TSS descriptor (16 bytes, spans two entries)
 * 
 * Total: 7 entries worth of space (56 bytes for entries + 16 for TSS desc)
 */

/* GDT entries: 5 normal entries + 2 entries for TSS (16 bytes) */
static gdt64_entry_t gdt64_entries[7] __attribute__((aligned(16)));

/* GDT pointer for LGDT instruction */
static gdt64_ptr_t gdt64_pointer;

/* Task State Segment */
static tss64_entry_t tss64 __attribute__((aligned(16)));

/* ============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * @brief Set a standard GDT entry (8 bytes)
 * @param index Entry index in GDT
 * @param base Base address (ignored in long mode for code/data)
 * @param limit Segment limit (ignored in long mode for code/data)
 * @param access Access byte
 * @param flags Flags (upper nibble)
 */
static void gdt64_set_entry(uint8_t index, uint32_t base, uint32_t limit,
                            uint8_t access, uint8_t flags) {
    gdt64_entries[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt64_entries[index].base_low = (uint16_t)(base & 0xFFFF);
    gdt64_entries[index].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt64_entries[index].access = access;
    gdt64_entries[index].flags_limit_high = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt64_entries[index].base_high = (uint8_t)((base >> 24) & 0xFF);
}

/**
 * @brief Set the TSS descriptor (16 bytes, spans two GDT entries)
 * @param index Starting index in GDT (will use index and index+1)
 * @param base 64-bit base address of TSS
 * @param limit TSS limit (size - 1)
 */
static void gdt64_set_tss_descriptor(uint8_t index, uint64_t base, uint32_t limit) {
    /* TSS descriptor is 16 bytes in 64-bit mode */
    tss64_descriptor_t *tss_desc = (tss64_descriptor_t *)&gdt64_entries[index];
    
    tss_desc->limit_low = (uint16_t)(limit & 0xFFFF);
    tss_desc->base_low = (uint16_t)(base & 0xFFFF);
    tss_desc->base_middle_low = (uint8_t)((base >> 16) & 0xFF);
    tss_desc->access = GDT64_ACCESS_TSS;  /* Present + 64-bit TSS Available */
    tss_desc->flags_limit_high = (uint8_t)((limit >> 16) & 0x0F);  /* No flags for TSS */
    tss_desc->base_middle_high = (uint8_t)((base >> 24) & 0xFF);
    tss_desc->base_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    tss_desc->reserved = 0;
}

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Initialize GDT with TSS for x86_64
 * @param kernel_stack Kernel stack pointer (RSP0 in TSS)
 */
void gdt64_init_with_tss(uint64_t kernel_stack) {
    LOG_INFO_MSG("Initializing x86_64 GDT with TSS...\n");
    
    /* Clear GDT entries */
    memset(gdt64_entries, 0, sizeof(gdt64_entries));
    
    /* Entry 0: Null descriptor (required by CPU) */
    gdt64_set_entry(0, 0, 0, 0, 0);
    
    /* Entry 1: Kernel Code Segment (64-bit, Ring 0)
     * Access: Present + Ring 0 + Code/Data + Executable + Readable
     * Flags: Long mode (L=1, D=0)
     */
    gdt64_set_entry(1, 0, 0xFFFFF,
                    GDT64_ACCESS_PRESENT | GDT64_ACCESS_PRIV_RING0 |
                    GDT64_ACCESS_CODE_DATA | GDT64_ACCESS_EXECUTABLE |
                    GDT64_ACCESS_READABLE,
                    GDT64_FLAG_GRANULARITY | GDT64_FLAG_LONG_MODE);
    
    /* Entry 2: Kernel Data Segment (Ring 0)
     * Access: Present + Ring 0 + Code/Data + Writable
     * Flags: None (data segments don't use L bit)
     */
    gdt64_set_entry(2, 0, 0xFFFFF,
                    GDT64_ACCESS_PRESENT | GDT64_ACCESS_PRIV_RING0 |
                    GDT64_ACCESS_CODE_DATA | GDT64_ACCESS_READABLE,
                    GDT64_FLAG_GRANULARITY);
    
    /* Entry 3: User Data Segment (Ring 3)
     * Access: Present + Ring 3 + Code/Data + Writable
     * Flags: None
     * 
     * NOTE: User Data MUST be at index 3 (0x18) for SYSRET compatibility.
     * SYSRET SS = STAR[63:48] + 8 | 3 = 0x10 + 8 | 3 = 0x1B
     */
    gdt64_set_entry(3, 0, 0xFFFFF,
                    GDT64_ACCESS_PRESENT | GDT64_ACCESS_PRIV_RING3 |
                    GDT64_ACCESS_CODE_DATA | GDT64_ACCESS_READABLE,
                    GDT64_FLAG_GRANULARITY);
    
    /* Entry 4: User Code Segment (64-bit, Ring 3)
     * Access: Present + Ring 3 + Code/Data + Executable + Readable
     * Flags: Long mode (L=1, D=0)
     * 
     * NOTE: User Code MUST be at index 4 (0x20) for SYSRET compatibility.
     * SYSRET CS = STAR[63:48] + 16 | 3 = 0x10 + 16 | 3 = 0x23
     */
    gdt64_set_entry(4, 0, 0xFFFFF,
                    GDT64_ACCESS_PRESENT | GDT64_ACCESS_PRIV_RING3 |
                    GDT64_ACCESS_CODE_DATA | GDT64_ACCESS_EXECUTABLE |
                    GDT64_ACCESS_READABLE,
                    GDT64_FLAG_GRANULARITY | GDT64_FLAG_LONG_MODE);
    
    /* Initialize TSS */
    memset(&tss64, 0, sizeof(tss64));
    tss64.rsp0 = kernel_stack;
    tss64.iomap_base = sizeof(tss64);  /* No I/O bitmap */
    
    LOG_DEBUG_MSG("  TSS addr=0x%llx size=%u\n", (unsigned long long)&tss64, (uint32_t)sizeof(tss64));
    
    /* Entry 5-6: TSS Descriptor (16 bytes) */
    gdt64_set_tss_descriptor(5, (uint64_t)&tss64, sizeof(tss64) - 1);
    
    /* Set up GDT pointer */
    gdt64_pointer.limit = sizeof(gdt64_entries) - 1;
    gdt64_pointer.base = (uint64_t)&gdt64_entries;
    
    LOG_DEBUG_MSG("  GDT base=0x%llx limit=%u\n", (unsigned long long)gdt64_pointer.base, gdt64_pointer.limit);
    
    /* Load GDT and reload segment registers */
    gdt64_flush((uint64_t)&gdt64_pointer);
    
    /* Load TSS (selector = index 5 << 3 = 0x28) */
    tss64_load(GDT64_TSS_SEGMENT);
    
    LOG_INFO_MSG("x86_64 GDT+TSS installed and loaded\n");
}

/**
 * @brief Update TSS kernel stack pointer (RSP0)
 * @param kernel_stack New kernel stack pointer
 */
void tss64_set_kernel_stack(uint64_t kernel_stack) {
    tss64.rsp0 = kernel_stack;
}

/**
 * @brief Set an IST (Interrupt Stack Table) entry
 * @param ist_index IST index (1-7)
 * @param stack_top Stack top address for this IST entry
 */
void tss64_set_ist(uint8_t ist_index, uint64_t stack_top) {
    if (ist_index < 1 || ist_index > 7) {
        return;  /* Invalid IST index */
    }
    
    /* IST entries are 1-indexed in the TSS structure */
    switch (ist_index) {
        case 1: tss64.ist1 = stack_top; break;
        case 2: tss64.ist2 = stack_top; break;
        case 3: tss64.ist3 = stack_top; break;
        case 4: tss64.ist4 = stack_top; break;
        case 5: tss64.ist5 = stack_top; break;
        case 6: tss64.ist6 = stack_top; break;
        case 7: tss64.ist7 = stack_top; break;
    }
}

/**
 * @brief Get TSS address
 * @return Address of the TSS structure
 */
uint64_t tss64_get_address(void) {
    return (uint64_t)&tss64;
}

/**
 * @brief Get TSS size
 * @return Size of the TSS structure in bytes
 */
uint32_t tss64_get_size(void) {
    return (uint32_t)sizeof(tss64);
}
