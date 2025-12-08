/**
 * @file arch_types.h
 * @brief i686 (x86 32-bit) Architecture-Specific Type Definitions
 * 
 * This header defines architecture-specific types and constants for the
 * i686 (x86 32-bit) architecture.
 * 
 * Requirements: 10.3
 */

#ifndef _ARCH_I686_ARCH_TYPES_H_
#define _ARCH_I686_ARCH_TYPES_H_

/* ============================================================================
 * Architecture Identification
 * ========================================================================== */

#define ARCH_NAME           "i686"
#define ARCH_BITS           32
#define ARCH_IS_64BIT       0

/* ============================================================================
 * Pointer and Size Types
 * ========================================================================== */

/** Unsigned pointer-sized integer */
typedef unsigned int        uintptr_t;

/** Signed pointer-sized integer */
typedef int                 intptr_t;

/** Size type (unsigned) */
typedef unsigned int        arch_size_t;

/** Signed size type */
typedef int                 arch_ssize_t;

/* ============================================================================
 * Memory Layout Constants
 * ========================================================================== */

/** Kernel virtual base address (higher-half kernel) */
#define KERNEL_VIRTUAL_BASE     0x80000000UL

/** User space ends at kernel base */
#define USER_SPACE_END          KERNEL_VIRTUAL_BASE

/** User space starts at 0 (after NULL page) */
#define USER_SPACE_START        0x00001000UL

/* ============================================================================
 * Page Table Constants
 * ========================================================================== */

/** Standard page size (4KB) */
#define PAGE_SIZE               4096

/** Page size shift (log2 of PAGE_SIZE) */
#define PAGE_SHIFT              12

/** Page alignment mask */
#define PAGE_MASK               0xFFFFF000UL

/** Number of page table levels (2 for i686) */
#define PAGE_TABLE_LEVELS       2

/** Entries per page table (1024 for 32-bit) */
#define PAGE_TABLE_ENTRIES      1024

/** Size of a page table entry (4 bytes for 32-bit) */
#define PAGE_TABLE_ENTRY_SIZE   4

/* ============================================================================
 * Address Space Limits
 * ========================================================================== */

/** Maximum physical address (4GB for 32-bit without PAE) */
#define PHYS_ADDR_MAX           0xFFFFFFFFUL

/** Maximum virtual address */
#define VIRT_ADDR_MAX           0xFFFFFFFFUL

/* ============================================================================
 * Register Sizes
 * ========================================================================== */

/** General purpose register size in bytes */
#define GPR_SIZE                4

/** Number of general purpose registers */
#define GPR_COUNT               8

/* ============================================================================
 * Stack Alignment
 * ========================================================================== */

/** Required stack alignment (16 bytes for SSE compatibility) */
#define STACK_ALIGNMENT         16

/* ============================================================================
 * Context Structure
 * ========================================================================== */

/**
 * @brief i686 CPU context structure
 * 
 * This structure holds all registers needed to save/restore task state.
 * Layout matches the order pushed by PUSHA instruction plus additional
 * registers.
 */
struct hal_context {
    /* Segment registers */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    
    /* General purpose registers (PUSHA order) */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;     /* ESP from PUSHA (not used) */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    
    /* Interrupt frame */
    uint32_t int_no;        /* Interrupt number */
    uint32_t err_code;      /* Error code (or 0) */
    
    /* Pushed by CPU on interrupt */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    
    /* Pushed by CPU on privilege change */
    uint32_t user_esp;
    uint32_t user_ss;
};

#endif /* _ARCH_I686_ARCH_TYPES_H_ */
