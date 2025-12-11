// ============================================================================
// pbt_generators.h - Domain-Specific PBT Generators
// ============================================================================
//
// Provides domain-specific random value generators for property-based testing
// of CastorOS kernel components. These generators produce valid values for
// memory management types (paddr_t, vaddr_t, pte_t) that respect architecture
// constraints.
//
// **Feature: test-refactor**
// **Validates: Requirements 2.2**
//
// Usage:
//   PBT_PROPERTY(test_vmm_mapping) {
//       paddr_t phys = pbt_gen_paddr(state);
//       vaddr_t virt = pbt_gen_vaddr_user(state);
//       pte_t pte = pbt_gen_pte_with_flags(state, PTE_PRESENT | PTE_WRITE);
//       // ... test property
//   }
// ============================================================================

#ifndef _TESTS_PBT_GENERATORS_H_
#define _TESTS_PBT_GENERATORS_H_

#include <tests/pbt.h>
#include <mm/mm_types.h>
#include <mm/pgtable.h>
#include <hal/pgtable.h>

// ============================================================================
// Physical Address Generators
// ============================================================================

/**
 * @brief Generate a random page-aligned physical address
 * 
 * Generates a valid physical address within the architecture's addressable
 * range. The address is always page-aligned (4KB boundary).
 * 
 * @param state PBT state
 * @return Page-aligned physical address
 * 
 * Architecture constraints:
 *   - i686: 0 to 0xFFFFFFFF (4GB, 32-bit)
 *   - x86_64: 0 to 0xFFFFFFFFFFFFF (52-bit physical)
 *   - ARM64: 0 to 0xFFFFFFFFFFFF (48-bit physical)
 */
static inline paddr_t pbt_gen_paddr(pbt_state_t *state) {
    uint64_t raw = pbt_gen_uint64(state);
    
#if defined(ARCH_X86_64)
    // x86_64: 52-bit physical address space
    paddr_t addr = raw & 0x000FFFFFFFFFFFFFULL;
#elif defined(ARCH_ARM64)
    // ARM64: 48-bit physical address space
    paddr_t addr = raw & 0x0000FFFFFFFFFFFFULL;
#else
    // i686: 32-bit physical address space
    paddr_t addr = (paddr_t)(raw & 0xFFFFFFFFULL);
#endif
    
    // Align to page boundary
    return PADDR_ALIGN_DOWN(addr);
}

/**
 * @brief Generate a random page-aligned physical address within a range
 * 
 * @param state PBT state
 * @param min Minimum address (will be aligned up)
 * @param max Maximum address (will be aligned down)
 * @return Page-aligned physical address in [min, max]
 */
static inline paddr_t pbt_gen_paddr_range(pbt_state_t *state, paddr_t min, paddr_t max) {
    paddr_t aligned_min = PADDR_ALIGN_UP(min);
    paddr_t aligned_max = PADDR_ALIGN_DOWN(max);
    
    if (aligned_min > aligned_max) {
        return aligned_min;
    }
    
    // Calculate number of pages in range
    uint64_t num_pages = (aligned_max - aligned_min) / PAGE_SIZE + 1;
    uint64_t page_idx = pbt_gen_uint64(state) % num_pages;
    
    return aligned_min + (page_idx * PAGE_SIZE);
}

/**
 * @brief Generate a "realistic" physical address for testing
 * 
 * Generates addresses in commonly used physical memory regions,
 * avoiding reserved areas like the first 1MB.
 * 
 * @param state PBT state
 * @return Page-aligned physical address in realistic range
 */
static inline paddr_t pbt_gen_paddr_realistic(pbt_state_t *state) {
    // Skip first 1MB (reserved for BIOS, etc.)
    // Generate in 1MB - 256MB range for realistic testing
    return pbt_gen_paddr_range(state, 0x100000, 0x10000000);
}

// ============================================================================
// Virtual Address Generators
// ============================================================================

/**
 * @brief Generate a random page-aligned user-space virtual address
 * 
 * Generates a valid user-space virtual address (below KERNEL_VIRTUAL_BASE).
 * The address is always page-aligned.
 * 
 * @param state PBT state
 * @return Page-aligned user-space virtual address
 */
static inline vaddr_t pbt_gen_vaddr_user(pbt_state_t *state) {
    uint64_t raw = pbt_gen_uint64(state);
    
#if defined(ARCH_X86_64)
    // x86_64: User space is 0 to 0x00007FFFFFFFFFFF (lower half)
    vaddr_t addr = raw & 0x00007FFFFFFFFFFFULL;
#elif defined(ARCH_ARM64)
    // ARM64: User space is 0 to KERNEL_VIRTUAL_BASE - 1
    vaddr_t addr = raw % KERNEL_VIRTUAL_BASE;
#else
    // i686: User space is 0 to KERNEL_VIRTUAL_BASE - 1 (0x80000000)
    vaddr_t addr = (vaddr_t)(raw % KERNEL_VIRTUAL_BASE);
#endif
    
    // Align to page boundary, ensure non-zero (avoid NULL)
    addr = VADDR_ALIGN_DOWN(addr);
    if (addr == 0) {
        addr = PAGE_SIZE;
    }
    
    return addr;
}

/**
 * @brief Generate a random page-aligned kernel-space virtual address
 * 
 * Generates a valid kernel-space virtual address (at or above KERNEL_VIRTUAL_BASE).
 * The address is always page-aligned.
 * 
 * @param state PBT state
 * @return Page-aligned kernel-space virtual address
 */
static inline vaddr_t pbt_gen_vaddr_kernel(pbt_state_t *state) {
    uint64_t raw = pbt_gen_uint64(state);
    
#if defined(ARCH_X86_64)
    // x86_64: Kernel space starts at 0xFFFF800000000000
    // Generate offset within reasonable kernel range (up to 512GB)
    vaddr_t offset = raw & 0x0000007FFFFFFFFFULL;
    vaddr_t addr = KERNEL_VIRTUAL_BASE + offset;
#elif defined(ARCH_ARM64)
    // ARM64: Kernel space starts at 0xFFFF000000000000
    vaddr_t offset = raw & 0x0000FFFFFFFFFFFFULL;
    vaddr_t addr = KERNEL_VIRTUAL_BASE + offset;
#else
    // i686: Kernel space is 0x80000000 to 0xFFFFFFFF
    vaddr_t offset = (vaddr_t)(raw & 0x7FFFFFFFUL);
    vaddr_t addr = KERNEL_VIRTUAL_BASE + offset;
#endif
    
    return VADDR_ALIGN_DOWN(addr);
}

/**
 * @brief Generate a random page-aligned virtual address (user or kernel)
 * 
 * @param state PBT state
 * @return Page-aligned virtual address
 */
static inline vaddr_t pbt_gen_vaddr(pbt_state_t *state) {
    if (pbt_gen_bool(state)) {
        return pbt_gen_vaddr_user(state);
    } else {
        return pbt_gen_vaddr_kernel(state);
    }
}

/**
 * @brief Generate a random page-aligned virtual address within a range
 * 
 * @param state PBT state
 * @param min Minimum address (will be aligned up)
 * @param max Maximum address (will be aligned down)
 * @return Page-aligned virtual address in [min, max]
 */
static inline vaddr_t pbt_gen_vaddr_range(pbt_state_t *state, vaddr_t min, vaddr_t max) {
    vaddr_t aligned_min = VADDR_ALIGN_UP(min);
    vaddr_t aligned_max = VADDR_ALIGN_DOWN(max);
    
    if (aligned_min > aligned_max) {
        return aligned_min;
    }
    
    // Calculate number of pages in range
    size_t num_pages = (aligned_max - aligned_min) / PAGE_SIZE + 1;
    size_t page_idx = pbt_gen_uint64(state) % num_pages;
    
    return aligned_min + (page_idx * PAGE_SIZE);
}

// ============================================================================
// Page Table Entry Generators
// ============================================================================

/**
 * @brief Generate a random valid page table entry
 * 
 * Generates a PTE with a random page-aligned physical address and
 * random valid flags. The PRESENT flag is always set.
 * 
 * @param state PBT state
 * @return Valid page table entry
 */
static inline pte_t pbt_gen_pte(pbt_state_t *state) {
    paddr_t phys = pbt_gen_paddr(state);
    
    // Generate random flags, always include PRESENT
    uint32_t flags = PTE_FLAG_PRESENT;
    
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_WRITE;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_USER;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_ACCESSED;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_DIRTY;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_GLOBAL;
    
    return MAKE_PTE(phys, flags);
}

/**
 * @brief Generate a page table entry with specific required flags
 * 
 * Generates a PTE with the specified flags always set, plus random
 * additional flags that don't conflict.
 * 
 * @param state PBT state
 * @param required_flags Flags that must be set (PTE_FLAG_* constants)
 * @return Page table entry with required flags set
 */
static inline pte_t pbt_gen_pte_with_flags(pbt_state_t *state, uint32_t required_flags) {
    paddr_t phys = pbt_gen_paddr(state);
    
    // Start with required flags
    uint32_t flags = required_flags;
    
    // Add random optional flags that don't conflict
    if (!(required_flags & PTE_FLAG_WRITE) && pbt_gen_bool(state)) {
        flags |= PTE_FLAG_WRITE;
    }
    if (!(required_flags & PTE_FLAG_USER) && pbt_gen_bool(state)) {
        flags |= PTE_FLAG_USER;
    }
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_ACCESSED;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_DIRTY;
    
    return MAKE_PTE(phys, flags);
}

/**
 * @brief Generate a user-accessible page table entry
 * 
 * Generates a PTE suitable for user-space mappings.
 * 
 * @param state PBT state
 * @return User-accessible page table entry
 */
static inline pte_t pbt_gen_pte_user(pbt_state_t *state) {
    return pbt_gen_pte_with_flags(state, PTE_FLAG_PRESENT | PTE_FLAG_USER);
}

/**
 * @brief Generate a kernel-only page table entry
 * 
 * Generates a PTE suitable for kernel-space mappings (no USER flag).
 * 
 * @param state PBT state
 * @return Kernel-only page table entry
 */
static inline pte_t pbt_gen_pte_kernel(pbt_state_t *state) {
    paddr_t phys = pbt_gen_paddr(state);
    
    // Kernel pages: PRESENT, optionally WRITE, GLOBAL, no USER
    uint32_t flags = PTE_FLAG_PRESENT;
    
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_WRITE;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_GLOBAL;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_ACCESSED;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_DIRTY;
    
    return MAKE_PTE(phys, flags);
}

/**
 * @brief Generate a COW (Copy-on-Write) page table entry
 * 
 * Generates a PTE with COW flag set and WRITE flag cleared.
 * 
 * @param state PBT state
 * @return COW page table entry
 */
static inline pte_t pbt_gen_pte_cow(pbt_state_t *state) {
    paddr_t phys = pbt_gen_paddr(state);
    
    // COW pages: PRESENT, COW, no WRITE
    uint32_t flags = PTE_FLAG_PRESENT | PTE_FLAG_COW;
    
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_USER;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_ACCESSED;
    
    return MAKE_PTE(phys, flags);
}

/**
 * @brief Generate an invalid (not present) page table entry
 * 
 * Generates a PTE without the PRESENT flag set.
 * 
 * @param state PBT state
 * @return Invalid page table entry
 */
static inline pte_t pbt_gen_pte_invalid(pbt_state_t *state) {
    // Just return 0 or random non-present value
    if (pbt_gen_bool(state)) {
        return 0;
    } else {
        // Random value without PRESENT flag
        paddr_t phys = pbt_gen_paddr(state);
        uint32_t flags = pbt_gen_uint32(state) & ~PTE_FLAG_PRESENT;
        return MAKE_PTE(phys, flags);
    }
}

// ============================================================================
// Page Frame Number Generators
// ============================================================================

/**
 * @brief Generate a random valid page frame number
 * 
 * @param state PBT state
 * @return Valid page frame number
 */
static inline pfn_t pbt_gen_pfn(pbt_state_t *state) {
    paddr_t addr = pbt_gen_paddr(state);
    return PADDR_TO_PFN(addr);
}

/**
 * @brief Generate a page frame number within a range
 * 
 * @param state PBT state
 * @param min_pfn Minimum PFN
 * @param max_pfn Maximum PFN
 * @return PFN in [min_pfn, max_pfn]
 */
static inline pfn_t pbt_gen_pfn_range(pbt_state_t *state, pfn_t min_pfn, pfn_t max_pfn) {
    if (min_pfn >= max_pfn) {
        return min_pfn;
    }
    
    uint64_t range = max_pfn - min_pfn + 1;
    return min_pfn + (pbt_gen_uint64(state) % range);
}

// ============================================================================
// Size Generators
// ============================================================================

/**
 * @brief Generate a random page-aligned size
 * 
 * @param state PBT state
 * @param max_pages Maximum number of pages
 * @return Page-aligned size (at least PAGE_SIZE)
 */
static inline size_t pbt_gen_size_pages(pbt_state_t *state, uint32_t max_pages) {
    if (max_pages == 0) max_pages = 1;
    uint32_t num_pages = pbt_gen_uint32_range(state, 1, max_pages);
    return (size_t)num_pages * PAGE_SIZE;
}

/**
 * @brief Generate a random allocation size
 * 
 * Generates sizes commonly used in heap allocations.
 * 
 * @param state PBT state
 * @param max_size Maximum size in bytes
 * @return Allocation size (at least 1)
 */
static inline size_t pbt_gen_alloc_size(pbt_state_t *state, size_t max_size) {
    if (max_size == 0) max_size = 1;
    
    // Bias towards smaller allocations (more common)
    uint32_t choice = pbt_gen_uint32_range(state, 0, 10);
    
    if (choice < 5) {
        // Small allocation: 1-256 bytes
        return pbt_gen_uint32_range(state, 1, 256);
    } else if (choice < 8) {
        // Medium allocation: 256-4096 bytes
        return pbt_gen_uint32_range(state, 256, 4096);
    } else {
        // Large allocation: up to max_size
        size_t size = pbt_gen_uint64(state) % max_size;
        return size > 0 ? size : 1;
    }
}

// ============================================================================
// Flag Combination Generators
// ============================================================================

/**
 * @brief Generate a random combination of PTE flags
 * 
 * @param state PBT state
 * @return Random combination of PTE_FLAG_* constants
 */
static inline uint32_t pbt_gen_pte_flags(pbt_state_t *state) {
    uint32_t flags = 0;
    
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_PRESENT;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_WRITE;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_USER;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_PWT;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_PCD;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_ACCESSED;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_DIRTY;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_GLOBAL;
    if (pbt_gen_bool(state)) flags |= PTE_FLAG_COW;
    
    return flags;
}

/**
 * @brief Generate valid PTE flags (PRESENT always set)
 * 
 * @param state PBT state
 * @return Valid PTE flags with PRESENT set
 */
static inline uint32_t pbt_gen_pte_flags_valid(pbt_state_t *state) {
    return pbt_gen_pte_flags(state) | PTE_FLAG_PRESENT;
}

#endif // _TESTS_PBT_GENERATORS_H_
