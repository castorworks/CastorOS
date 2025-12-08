/**
 * @file arch_types.h
 * @brief x86_64 (AMD64/Intel 64-bit) Architecture-Specific Type Definitions
 * 
 * This header defines architecture-specific types and constants for the
 * x86_64 (AMD64/Intel 64-bit) architecture.
 * 
 * Requirements: 10.3
 */

#ifndef _ARCH_X86_64_ARCH_TYPES_H_
#define _ARCH_X86_64_ARCH_TYPES_H_

/* ============================================================================
 * Architecture Identification
 * ========================================================================== */

#define ARCH_NAME           "x86_64"
#define ARCH_BITS           64
#define ARCH_IS_64BIT       1

/* ============================================================================
 * Pointer and Size Types
 * ========================================================================== */

/** Unsigned pointer-sized integer */
typedef unsigned long long  uintptr_t;

/** Signed pointer-sized integer */
typedef long long           intptr_t;

/** Size type (unsigned) */
typedef unsigned long long  arch_size_t;

/** Signed size type */
typedef long long           arch_ssize_t;

/* ============================================================================
 * Memory Layout Constants
 * ========================================================================== */

/** Kernel virtual base address (higher-half kernel, canonical high address) */
#define KERNEL_VIRTUAL_BASE     0xFFFF800000000000ULL

/** User space ends at canonical hole */
#define USER_SPACE_END          0x00007FFFFFFFFFFFULL

/** User space starts at 0 (after NULL page) */
#define USER_SPACE_START        0x0000000000001000ULL

/* ============================================================================
 * Page Table Constants
 * ========================================================================== */

/** Standard page size (4KB) */
#define PAGE_SIZE               4096

/** Page size shift (log2 of PAGE_SIZE) */
#define PAGE_SHIFT              12

/** Page alignment mask */
#define PAGE_MASK               0xFFFFFFFFFFFFF000ULL

/** Number of page table levels (4 for x86_64) */
#define PAGE_TABLE_LEVELS       4

/** Entries per page table (512 for 64-bit) */
#define PAGE_TABLE_ENTRIES      512

/** Size of a page table entry (8 bytes for 64-bit) */
#define PAGE_TABLE_ENTRY_SIZE   8

/** Large page size (2MB) */
#define LARGE_PAGE_SIZE         (2 * 1024 * 1024)

/** Huge page size (1GB) */
#define HUGE_PAGE_SIZE          (1024 * 1024 * 1024)

/* ============================================================================
 * Address Space Limits
 * ========================================================================== */

/** Maximum physical address (48-bit physical addressing) */
#define PHYS_ADDR_MAX           0x0000FFFFFFFFFFFFULL

/** Maximum canonical virtual address (high) */
#define VIRT_ADDR_MAX_HIGH      0xFFFFFFFFFFFFFFFFULL

/** Maximum canonical virtual address (low) */
#define VIRT_ADDR_MAX_LOW       0x00007FFFFFFFFFFFULL

/* ============================================================================
 * Register Sizes
 * ========================================================================== */

/** General purpose register size in bytes */
#define GPR_SIZE                8

/** Number of general purpose registers */
#define GPR_COUNT               16

/* ============================================================================
 * Stack Alignment
 * ========================================================================== */

/** Required stack alignment (16 bytes for ABI compliance) */
#define STACK_ALIGNMENT         16

/* ============================================================================
 * x86_64 Specific Constants
 * ========================================================================== */

/** No-Execute bit position in page table entry */
#define PTE_NX_BIT              63

/* ============================================================================
 * Context Structure
 * ========================================================================== */

/**
 * @brief x86_64 CPU context structure
 * 
 * This structure holds all registers needed to save/restore task state.
 * Includes all 64-bit general purpose registers (RAX-R15).
 */
struct hal_context {
    /* General purpose registers */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    
    /* Interrupt information */
    uint64_t int_no;        /* Interrupt number */
    uint64_t err_code;      /* Error code (or 0) */
    
    /* Pushed by CPU on interrupt */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

#endif /* _ARCH_X86_64_ARCH_TYPES_H_ */
