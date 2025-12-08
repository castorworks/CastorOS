/**
 * @file arch_types.h
 * @brief ARM64 (AArch64) Architecture-Specific Type Definitions
 * 
 * This header defines architecture-specific types and constants for the
 * ARM64 (AArch64) architecture.
 * 
 * Requirements: 10.3
 */

#ifndef _ARCH_ARM64_ARCH_TYPES_H_
#define _ARCH_ARM64_ARCH_TYPES_H_

/* ============================================================================
 * Architecture Identification
 * ========================================================================== */

#define ARCH_NAME           "arm64"
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

/** Kernel virtual base address (higher-half kernel, TTBR1 region) */
#define KERNEL_VIRTUAL_BASE     0xFFFF000000000000ULL

/** User space ends at TTBR0 limit */
#define USER_SPACE_END          0x0000FFFFFFFFFFFFULL

/** User space starts at 0 (after NULL page) */
#define USER_SPACE_START        0x0000000000001000ULL

/* ============================================================================
 * Page Table Constants
 * ========================================================================== */

/** Standard page size (4KB granule) */
#define PAGE_SIZE               4096

/** Page size shift (log2 of PAGE_SIZE) */
#define PAGE_SHIFT              12

/** Page alignment mask */
#define PAGE_MASK               0xFFFFFFFFFFFFF000ULL

/** Number of page table levels (4 for 4KB granule with 48-bit VA) */
#define PAGE_TABLE_LEVELS       4

/** Entries per page table (512 for 4KB granule) */
#define PAGE_TABLE_ENTRIES      512

/** Size of a page table entry (8 bytes) */
#define PAGE_TABLE_ENTRY_SIZE   8

/** Large page size (2MB, Level 2 block) */
#define LARGE_PAGE_SIZE         (2 * 1024 * 1024)

/** Huge page size (1GB, Level 1 block) */
#define HUGE_PAGE_SIZE          (1024 * 1024 * 1024)

/* ============================================================================
 * Address Space Limits
 * ========================================================================== */

/** Maximum physical address (48-bit physical addressing) */
#define PHYS_ADDR_MAX           0x0000FFFFFFFFFFFFULL

/** Maximum virtual address (TTBR1 region) */
#define VIRT_ADDR_MAX           0xFFFFFFFFFFFFFFFFULL

/* ============================================================================
 * Register Sizes
 * ========================================================================== */

/** General purpose register size in bytes */
#define GPR_SIZE                8

/** Number of general purpose registers (X0-X30) */
#define GPR_COUNT               31

/* ============================================================================
 * Stack Alignment
 * ========================================================================== */

/** Required stack alignment (16 bytes for ABI compliance) */
#define STACK_ALIGNMENT         16

/* ============================================================================
 * ARM64 Specific Constants
 * ========================================================================== */

/** Exception Levels */
#define EL0                     0   /* User mode */
#define EL1                     1   /* Kernel mode */
#define EL2                     2   /* Hypervisor */
#define EL3                     3   /* Secure Monitor */

/** PSTATE bits */
#define PSTATE_N                (1ULL << 31)    /* Negative flag */
#define PSTATE_Z                (1ULL << 30)    /* Zero flag */
#define PSTATE_C                (1ULL << 29)    /* Carry flag */
#define PSTATE_V                (1ULL << 28)    /* Overflow flag */
#define PSTATE_D                (1ULL << 9)     /* Debug mask */
#define PSTATE_A                (1ULL << 8)     /* SError mask */
#define PSTATE_I                (1ULL << 7)     /* IRQ mask */
#define PSTATE_F                (1ULL << 6)     /* FIQ mask */
#define PSTATE_EL_MASK          (3ULL << 2)     /* Exception Level */
#define PSTATE_SP               (1ULL << 0)     /* Stack pointer select */

/* ============================================================================
 * Context Structure
 * ========================================================================== */

/**
 * @brief ARM64 CPU context structure
 * 
 * This structure holds all registers needed to save/restore task state.
 * Includes general purpose registers X0-X30, SP, PC (ELR_EL1), and
 * processor state (SPSR_EL1).
 */
struct hal_context {
    /* General purpose registers X0-X30 */
    uint64_t x[31];
    
    /* Stack pointer */
    uint64_t sp;
    
    /* Program counter (saved in ELR_EL1 on exception) */
    uint64_t pc;
    
    /* Processor state (saved in SPSR_EL1 on exception) */
    uint64_t pstate;
    
    /* User page table base (TTBR0_EL1) */
    uint64_t ttbr0;
    
    /* Exception Syndrome Register (for fault handling) */
    uint64_t esr;
    
    /* Fault Address Register (for fault handling) */
    uint64_t far;
};

#endif /* _ARCH_ARM64_ARCH_TYPES_H_ */
