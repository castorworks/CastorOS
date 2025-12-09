/**
 * @file gdt64.h
 * @brief Global Descriptor Table & Task State Segment (x86_64)
 * 
 * This header defines the 64-bit GDT and TSS structures for x86_64 architecture.
 * In long mode, segmentation is mostly disabled, but GDT is still required for:
 *   - Code/data segment descriptors (with limited functionality)
 *   - TSS descriptor for privilege level transitions
 *   - System call entry (SYSCALL/SYSRET MSR configuration)
 * 
 * Requirements: 3.3 - x86_64 GDT with appropriate code and data segments
 */

#ifndef _ARCH_X86_64_GDT64_H_
#define _ARCH_X86_64_GDT64_H_

#include <types.h>

/* ============================================================================
 * Segment Selectors
 * ========================================================================== */

#define GDT64_NULL_SEGMENT          0x00
#define GDT64_KERNEL_CODE_SEGMENT   0x08    /* Index 1 */
#define GDT64_KERNEL_DATA_SEGMENT   0x10    /* Index 2 */
#define GDT64_USER_CODE_SEGMENT     0x18    /* Index 3, RPL=3 -> 0x1B */
#define GDT64_USER_DATA_SEGMENT     0x20    /* Index 4, RPL=3 -> 0x23 */
#define GDT64_TSS_SEGMENT           0x28    /* Index 5 (16 bytes, spans 5-6) */

/* User mode selectors with RPL=3 */
#define GDT64_USER_CODE_SELECTOR    (GDT64_USER_CODE_SEGMENT | 3)
#define GDT64_USER_DATA_SELECTOR    (GDT64_USER_DATA_SEGMENT | 3)

/* Compatibility macros for generic kernel code */
#define GDT_KERNEL_CODE_SEGMENT     GDT64_KERNEL_CODE_SEGMENT
#define GDT_KERNEL_DATA_SEGMENT     GDT64_KERNEL_DATA_SEGMENT
#define GDT_USER_CODE_SEGMENT       GDT64_USER_CODE_SEGMENT
#define GDT_USER_DATA_SEGMENT       GDT64_USER_DATA_SEGMENT

/* Compatibility wrapper for TSS kernel stack */
#define tss_set_kernel_stack(stack) tss64_set_kernel_stack((uint64_t)(stack))

/* ============================================================================
 * GDT Entry Structure (8 bytes for normal descriptors)
 * ========================================================================== */

/**
 * @brief Standard GDT entry (8 bytes)
 * 
 * In 64-bit mode, most fields are ignored for code/data segments.
 * Only the following are used:
 *   - L (Long mode) bit in flags
 *   - D (Default operand size) bit - must be 0 for 64-bit code
 *   - DPL (Descriptor Privilege Level) in access
 *   - P (Present) bit in access
 */
typedef struct gdt64_entry {
    uint16_t limit_low;         /* Segment limit 0:15 (ignored in long mode) */
    uint16_t base_low;          /* Base address 0:15 (ignored in long mode) */
    uint8_t  base_middle;       /* Base address 16:23 (ignored in long mode) */
    uint8_t  access;            /* Access byte */
    uint8_t  flags_limit_high;  /* Flags (4 bits) + Limit 16:19 (4 bits) */
    uint8_t  base_high;         /* Base address 24:31 (ignored in long mode) */
} __attribute__((packed)) gdt64_entry_t;

/* ============================================================================
 * TSS64 Structure (104 bytes minimum)
 * ========================================================================== */

/**
 * @brief 64-bit Task State Segment
 * 
 * In 64-bit mode, TSS is used for:
 *   - RSP0-RSP2: Stack pointers for privilege level transitions
 *   - IST1-IST7: Interrupt Stack Table entries for dedicated interrupt stacks
 *   - I/O permission bitmap base
 * 
 * Hardware task switching is not supported in long mode.
 */
typedef struct tss64_entry {
    uint32_t reserved0;         /* Reserved, must be 0 */
    uint64_t rsp0;              /* Stack pointer for Ring 0 (kernel) */
    uint64_t rsp1;              /* Stack pointer for Ring 1 (unused) */
    uint64_t rsp2;              /* Stack pointer for Ring 2 (unused) */
    uint64_t reserved1;         /* Reserved, must be 0 */
    uint64_t ist1;              /* Interrupt Stack Table entry 1 */
    uint64_t ist2;              /* Interrupt Stack Table entry 2 */
    uint64_t ist3;              /* Interrupt Stack Table entry 3 */
    uint64_t ist4;              /* Interrupt Stack Table entry 4 */
    uint64_t ist5;              /* Interrupt Stack Table entry 5 */
    uint64_t ist6;              /* Interrupt Stack Table entry 6 */
    uint64_t ist7;              /* Interrupt Stack Table entry 7 */
    uint64_t reserved2;         /* Reserved, must be 0 */
    uint16_t reserved3;         /* Reserved, must be 0 */
    uint16_t iomap_base;        /* I/O Map Base Address */
} __attribute__((packed)) tss64_entry_t;

/* ============================================================================
 * TSS Descriptor (16 bytes in 64-bit mode)
 * ========================================================================== */

/**
 * @brief 64-bit TSS Descriptor
 * 
 * In 64-bit mode, system descriptors (TSS, LDT) are 16 bytes instead of 8.
 * This allows for a full 64-bit base address.
 */
typedef struct tss64_descriptor {
    uint16_t limit_low;         /* Segment limit 0:15 */
    uint16_t base_low;          /* Base address 0:15 */
    uint8_t  base_middle_low;   /* Base address 16:23 */
    uint8_t  access;            /* Access byte (type + DPL + P) */
    uint8_t  flags_limit_high;  /* Flags + Limit 16:19 */
    uint8_t  base_middle_high;  /* Base address 24:31 */
    uint32_t base_high;         /* Base address 32:63 */
    uint32_t reserved;          /* Reserved, must be 0 */
} __attribute__((packed)) tss64_descriptor_t;

/* ============================================================================
 * GDTR Structure (10 bytes in 64-bit mode)
 * ========================================================================== */

/**
 * @brief GDT Register structure for LGDT instruction
 */
typedef struct gdt64_ptr {
    uint16_t limit;             /* GDT size - 1 */
    uint64_t base;              /* GDT base address (64-bit) */
} __attribute__((packed)) gdt64_ptr_t;

/* ============================================================================
 * Access Byte Flags
 * ========================================================================== */

/* Common flags */
#define GDT64_ACCESS_PRESENT        0x80    /* Segment present */
#define GDT64_ACCESS_PRIV_RING0     0x00    /* Ring 0 (kernel) */
#define GDT64_ACCESS_PRIV_RING3     0x60    /* Ring 3 (user) */
#define GDT64_ACCESS_CODE_DATA      0x10    /* Code/Data segment (not system) */
#define GDT64_ACCESS_EXECUTABLE     0x08    /* Executable (code segment) */
#define GDT64_ACCESS_DIRECTION      0x04    /* Direction/Conforming */
#define GDT64_ACCESS_READABLE       0x02    /* Readable (code) / Writable (data) */
#define GDT64_ACCESS_ACCESSED       0x01    /* Accessed */

/* TSS Access byte: Present + 64-bit TSS Available (type = 0x9) */
#define GDT64_ACCESS_TSS            0x89

/* TSS Access byte when busy: Present + 64-bit TSS Busy (type = 0xB) */
#define GDT64_ACCESS_TSS_BUSY       0x8B

/* ============================================================================
 * Flags (upper 4 bits of flags_limit_high)
 * ========================================================================== */

#define GDT64_FLAG_GRANULARITY      0x80    /* 4KB granularity (G bit) */
#define GDT64_FLAG_SIZE_32          0x40    /* 32-bit segment (D bit) - must be 0 for 64-bit code */
#define GDT64_FLAG_LONG_MODE        0x20    /* 64-bit code segment (L bit) */
#define GDT64_FLAG_AVAILABLE        0x10    /* Available for system use (AVL bit) */

/* ============================================================================
 * API Functions
 * ========================================================================== */

/**
 * @brief Initialize GDT with TSS for x86_64
 * @param kernel_stack Kernel stack pointer (RSP0 in TSS)
 * 
 * Sets up:
 *   - Null descriptor
 *   - Kernel code segment (64-bit)
 *   - Kernel data segment
 *   - User code segment (64-bit)
 *   - User data segment
 *   - TSS descriptor (16 bytes)
 */
void gdt64_init_with_tss(uint64_t kernel_stack);

/**
 * @brief Update TSS kernel stack pointer (RSP0)
 * @param kernel_stack New kernel stack pointer
 * 
 * Called during context switch to update the stack used when
 * transitioning from user mode to kernel mode.
 */
void tss64_set_kernel_stack(uint64_t kernel_stack);

/**
 * @brief Set an IST (Interrupt Stack Table) entry
 * @param ist_index IST index (1-7)
 * @param stack_top Stack top address for this IST entry
 */
void tss64_set_ist(uint8_t ist_index, uint64_t stack_top);

/**
 * @brief Get TSS address
 * @return Address of the TSS structure
 */
uint64_t tss64_get_address(void);

/**
 * @brief Get TSS size
 * @return Size of the TSS structure in bytes
 */
uint32_t tss64_get_size(void);

/* ============================================================================
 * Assembly Functions (defined in gdt64_asm.asm)
 * ========================================================================== */

/**
 * @brief Load GDT and reload segment registers
 * @param gdt_ptr Pointer to GDT pointer structure
 */
extern void gdt64_flush(uint64_t gdt_ptr);

/**
 * @brief Load TSS selector into TR register
 * @param selector TSS segment selector
 */
extern void tss64_load(uint16_t selector);

#endif /* _ARCH_X86_64_GDT64_H_ */
