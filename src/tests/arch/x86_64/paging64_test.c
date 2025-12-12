/**
 * @file paging64_test.c
 * @brief x86_64 分页属性测试
 * 
 * 实现 x86_64 架构的分页相关属性测试
 * 
 * **Feature: multi-arch-support**
 * **Property 4: VMM Kernel Mapping Range Correctness (x86_64)**
 * **Property 5: VMM Page Fault Interpretation (x86_64)**
 * **Validates: Requirements 5.3, 5.4**
 */

#include <tests/ktest.h>
#include <tests/arch/x86_64/paging64_test.h>
#include <types.h>
#include <lib/kprintf.h>

#ifdef ARCH_X86_64

#include "paging64.h"

/* x86_64 specific constants */
#define KERNEL_VIRTUAL_BASE_X64     0xFFFF800000000000ULL
#define USER_SPACE_END_X64          0x00007FFFFFFFFFFFULL
#define PHYS_ADDR_MAX_X64           0x0000FFFFFFFFFFFFULL

/* ============================================================================
 * Property 4: VMM Kernel Mapping Range Correctness (x86_64)
 * 
 * *For any* kernel virtual address, the address SHALL fall within 
 * the architecture-appropriate higher-half range 
 * (≥0xFFFF800000000000 for x86_64).
 * 
 * **Validates: Requirements 5.3**
 * ============================================================================ */

/**
 * @brief Test that KERNEL_VIRTUAL_BASE is correct for x86_64
 */
TEST_CASE(test_pbt_x86_64_kernel_base_address) {
    /* Property: KERNEL_VIRTUAL_BASE must be 0xFFFF800000000000 for x86_64 */
    uint64_t expected_base = KERNEL_VIRTUAL_BASE_X64;
    uint64_t actual_base = x86_64_get_kernel_virtual_base();
    
    ASSERT_TRUE(actual_base == expected_base);
}

/**
 * @brief Test canonical address validation for kernel addresses
 * 
 * *For any* kernel address, it must be a valid canonical address
 * (bits 63:48 must all be 1 for high-half addresses)
 */
TEST_CASE(test_pbt_x86_64_kernel_canonical_addresses) {
    #define PBT_ITERATIONS 20
    
    /* Test various kernel addresses */
    uint64_t test_addrs[] = {
        KERNEL_VIRTUAL_BASE_X64,                    /* Base address */
        KERNEL_VIRTUAL_BASE_X64 + 0x1000,           /* Base + 4KB */
        KERNEL_VIRTUAL_BASE_X64 + 0x100000,         /* Base + 1MB */
        KERNEL_VIRTUAL_BASE_X64 + 0x10000000,       /* Base + 256MB */
        KERNEL_VIRTUAL_BASE_X64 + 0x100000000ULL,   /* Base + 4GB */
        0xFFFF800000000000ULL,                      /* Exact base */
        0xFFFF800000001000ULL,                      /* Base + 4KB */
        0xFFFF8000FFFFFFFFULL,                      /* Base + 4GB - 1 */
        0xFFFFFFFF80000000ULL,                      /* High kernel address */
        0xFFFFFFFFFFFFFFFFULL,                      /* Maximum address */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: All kernel addresses must be canonical */
        bool is_canonical = x86_64_is_canonical_address(addr);
        ASSERT_TRUE(is_canonical);
        
        /* Property: All kernel addresses must be >= KERNEL_VIRTUAL_BASE */
        bool is_kernel = x86_64_is_kernel_address(addr);
        ASSERT_TRUE(is_kernel);
        
        /* Property: Kernel addresses must NOT be user addresses */
        bool is_user = x86_64_is_user_address(addr);
        ASSERT_FALSE(is_user);
    }
}

/**
 * @brief Test canonical address validation for user addresses
 * 
 * *For any* user address, it must be a valid canonical address
 * (bits 63:48 must all be 0 for low-half addresses)
 */
TEST_CASE(test_pbt_x86_64_user_canonical_addresses) {
    /* Test various user addresses */
    uint64_t test_addrs[] = {
        0x0000000000001000ULL,                      /* First valid user page */
        0x0000000000010000ULL,                      /* 64KB */
        0x0000000000100000ULL,                      /* 1MB */
        0x0000000001000000ULL,                      /* 16MB */
        0x0000000010000000ULL,                      /* 256MB */
        0x0000000100000000ULL,                      /* 4GB */
        0x0000001000000000ULL,                      /* 64GB */
        0x00007FFFFFFFF000ULL,                      /* Near end of user space */
        USER_SPACE_END_X64,                         /* End of user space */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: All user addresses must be canonical */
        bool is_canonical = x86_64_is_canonical_address(addr);
        ASSERT_TRUE(is_canonical);
        
        /* Property: User addresses must be user addresses */
        bool is_user = x86_64_is_user_address(addr);
        ASSERT_TRUE(is_user);
        
        /* Property: User addresses must NOT be kernel addresses */
        bool is_kernel = x86_64_is_kernel_address(addr);
        ASSERT_FALSE(is_kernel);
    }
}

/**
 * @brief Test non-canonical address detection
 * 
 * *For any* address in the canonical hole (0x0000800000000000 - 0xFFFF7FFFFFFFFFFF),
 * it must be detected as non-canonical
 */
TEST_CASE(test_pbt_x86_64_noncanonical_addresses) {
    /* Test addresses in the canonical hole */
    uint64_t test_addrs[] = {
        0x0000800000000000ULL,                      /* Start of hole */
        0x0000FFFFFFFFFFFFULL,                      /* Middle of hole */
        0x0001000000000000ULL,                      /* In hole */
        0x7FFFFFFFFFFFFFFFULL,                      /* In hole */
        0x8000000000000000ULL,                      /* In hole */
        0xFFFF7FFFFFFFFFFFULL,                      /* End of hole */
    };
    
    for (uint32_t i = 0; i < sizeof(test_addrs)/sizeof(test_addrs[0]); i++) {
        uint64_t addr = test_addrs[i];
        
        /* Property: Addresses in canonical hole must be non-canonical */
        bool is_canonical = x86_64_is_canonical_address(addr);
        ASSERT_FALSE(is_canonical);
    }
}

/**
 * @brief Test page table level count
 * 
 * *For any* x86_64 system, the page table SHALL use 4 levels
 */
TEST_CASE(test_pbt_x86_64_page_table_levels) {
    /* Property: x86_64 must use 4-level page tables */
    uint32_t levels = x86_64_get_page_table_levels();
    ASSERT_EQ_U(levels, 4);
}

/**
 * @brief Test page size
 * 
 * *For any* x86_64 system, the standard page size SHALL be 4KB
 */
TEST_CASE(test_pbt_x86_64_page_size) {
    /* Property: Standard page size must be 4KB */
    uint32_t page_size = x86_64_get_page_size();
    ASSERT_EQ_U(page_size, 4096);
}

/* ============================================================================
 * Property 5: VMM Page Fault Interpretation (x86_64)
 * 
 * *For any* page fault exception, the VMM SHALL correctly interpret 
 * the architecture-specific fault information (CR2 and error code on x86)
 * to determine the faulting address and fault type.
 * 
 * **Validates: Requirements 5.4**
 * ============================================================================ */

/**
 * @brief Test page fault error code parsing - present bit
 * 
 * *For any* page fault error code, the present bit (bit 0) SHALL be
 * correctly interpreted
 */
TEST_CASE(test_pbt_x86_64_page_fault_present_bit) {
    /* Test error codes with present bit = 0 (page not present) */
    uint64_t not_present_codes[] = {
        0x0,    /* Read from non-present page, kernel mode */
        0x2,    /* Write to non-present page, kernel mode */
        0x4,    /* Read from non-present page, user mode */
        0x6,    /* Write to non-present page, user mode */
        0x10,   /* Instruction fetch from non-present page */
    };
    
    for (uint32_t i = 0; i < sizeof(not_present_codes)/sizeof(not_present_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(not_present_codes[i]);
        ASSERT_FALSE(info.present);
    }
    
    /* Test error codes with present bit = 1 (protection violation) */
    uint64_t present_codes[] = {
        0x1,    /* Read protection violation, kernel mode */
        0x3,    /* Write protection violation, kernel mode */
        0x5,    /* Read protection violation, user mode */
        0x7,    /* Write protection violation, user mode */
        0x11,   /* Instruction fetch protection violation */
    };
    
    for (uint32_t i = 0; i < sizeof(present_codes)/sizeof(present_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(present_codes[i]);
        ASSERT_TRUE(info.present);
    }
}

/**
 * @brief Test page fault error code parsing - write bit
 * 
 * *For any* page fault error code, the write bit (bit 1) SHALL be
 * correctly interpreted
 */
TEST_CASE(test_pbt_x86_64_page_fault_write_bit) {
    /* Test error codes with write bit = 0 (read access) */
    uint64_t read_codes[] = {
        0x0,    /* Read from non-present page */
        0x1,    /* Read protection violation */
        0x4,    /* Read from non-present page, user mode */
        0x5,    /* Read protection violation, user mode */
    };
    
    for (uint32_t i = 0; i < sizeof(read_codes)/sizeof(read_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(read_codes[i]);
        ASSERT_FALSE(info.write);
    }
    
    /* Test error codes with write bit = 1 (write access) */
    uint64_t write_codes[] = {
        0x2,    /* Write to non-present page */
        0x3,    /* Write protection violation */
        0x6,    /* Write to non-present page, user mode */
        0x7,    /* Write protection violation, user mode */
    };
    
    for (uint32_t i = 0; i < sizeof(write_codes)/sizeof(write_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(write_codes[i]);
        ASSERT_TRUE(info.write);
    }
}

/**
 * @brief Test page fault error code parsing - user bit
 * 
 * *For any* page fault error code, the user bit (bit 2) SHALL be
 * correctly interpreted
 */
TEST_CASE(test_pbt_x86_64_page_fault_user_bit) {
    /* Test error codes with user bit = 0 (kernel mode) */
    uint64_t kernel_codes[] = {
        0x0,    /* Kernel read from non-present page */
        0x1,    /* Kernel read protection violation */
        0x2,    /* Kernel write to non-present page */
        0x3,    /* Kernel write protection violation */
    };
    
    for (uint32_t i = 0; i < sizeof(kernel_codes)/sizeof(kernel_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(kernel_codes[i]);
        ASSERT_FALSE(info.user);
    }
    
    /* Test error codes with user bit = 1 (user mode) */
    uint64_t user_codes[] = {
        0x4,    /* User read from non-present page */
        0x5,    /* User read protection violation */
        0x6,    /* User write to non-present page */
        0x7,    /* User write protection violation */
    };
    
    for (uint32_t i = 0; i < sizeof(user_codes)/sizeof(user_codes[0]); i++) {
        x86_64_page_fault_info_t info = x86_64_parse_page_fault_error(user_codes[i]);
        ASSERT_TRUE(info.user);
    }
}

/**
 * @brief Test COW fault detection
 * 
 * *For any* page fault with present=1 and write=1, it SHALL be
 * detected as a potential COW fault
 */
TEST_CASE(test_pbt_x86_64_cow_fault_detection) {
    /* COW faults have present=1 and write=1 (error code & 0x3 == 0x3) */
    uint64_t cow_codes[] = {
        0x3,    /* Kernel write protection violation */
        0x7,    /* User write protection violation */
        0x0B,   /* With reserved bit */
        0x13,   /* With instruction fetch bit */
    };
    
    for (uint32_t i = 0; i < sizeof(cow_codes)/sizeof(cow_codes[0]); i++) {
        bool is_cow = x86_64_is_cow_fault(cow_codes[i]);
        ASSERT_TRUE(is_cow);
    }
    
    /* Non-COW faults */
    uint64_t non_cow_codes[] = {
        0x0,    /* Page not present, read */
        0x1,    /* Read protection violation */
        0x2,    /* Page not present, write */
        0x4,    /* Page not present, user read */
        0x5,    /* User read protection violation */
        0x6,    /* Page not present, user write */
    };
    
    for (uint32_t i = 0; i < sizeof(non_cow_codes)/sizeof(non_cow_codes[0]); i++) {
        bool is_cow = x86_64_is_cow_fault(non_cow_codes[i]);
        ASSERT_FALSE(is_cow);
    }
}

/**
 * @brief Test page table entry validation
 * 
 * *For any* page table entry, the validation function SHALL correctly
 * identify valid and invalid entries
 */
TEST_CASE(test_pbt_x86_64_pte_validation) {
    /* Valid entries (present with page-aligned addresses) */
    pte64_t valid_entries[] = {
        0x0000000000001003ULL,   /* Present, writable, page at 0x1000 */
        0x0000000000002007ULL,   /* Present, writable, user, page at 0x2000 */
        0x00000000FFFFF003ULL,   /* Present, writable, high address */
        0x0000FFFFFFFF0003ULL,   /* Present, writable, very high address */
    };
    
    for (uint32_t i = 0; i < sizeof(valid_entries)/sizeof(valid_entries[0]); i++) {
        bool is_valid = x86_64_validate_pte_format(valid_entries[i]);
        ASSERT_TRUE(is_valid);
    }
    
    /* Non-present entries (always valid format-wise) */
    pte64_t non_present_entries[] = {
        0x0000000000000000ULL,   /* Empty entry */
        0x0000000000001000ULL,   /* Address but not present */
    };
    
    for (uint32_t i = 0; i < sizeof(non_present_entries)/sizeof(non_present_entries[0]); i++) {
        bool is_valid = x86_64_validate_pte_format(non_present_entries[i]);
        ASSERT_TRUE(is_valid);  /* Non-present entries are always "valid" */
    }
}

/* ============================================================================
 * Property 8: HAL MMU Map-Query Round-Trip (x86_64)
 * 
 * *For any* valid virtual address `virt`, physical address `phys`, and flags `flags`,
 * after `hal_mmu_map(space, virt, phys, flags)` succeeds, 
 * `hal_mmu_query(space, virt, &out_phys, &out_flags)` SHALL return `true` 
 * with `out_phys == phys`.
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip (x86_64)**
 * **Validates: Requirements 5.1**
 * ============================================================================ */

#include <hal/hal.h>
#include <mm/pmm.h>
#include <mm/mm_types.h>

/**
 * @brief Simple pseudo-random number generator for property testing
 * Uses a linear congruential generator (LCG)
 */
static uint64_t pbt_seed = 12345;

static uint64_t pbt_random(void) {
    pbt_seed = pbt_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return pbt_seed;
}

static uint64_t pbt_random_range(uint64_t min, uint64_t max) {
    if (min >= max) return min;
    return min + (pbt_random() % (max - min + 1));
}

/**
 * @brief Generate a random page-aligned user-space virtual address
 * User space on x86_64: 0x0000000000001000 - 0x00007FFFFFFFFFFF
 */
static vaddr_t pbt_random_user_vaddr(void) {
    /* Generate address in user space range, page-aligned */
    uint64_t page_num = pbt_random_range(1, 0x7FFFFFFFFULL);  /* Pages 1 to max user page */
    return (vaddr_t)(page_num << PAGE_SHIFT);
}

/**
 * @brief Test HAL MMU map-query round-trip property
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip (x86_64)**
 * **Validates: Requirements 5.1**
 * 
 * *For any* valid virtual address, physical address, and flags,
 * mapping and then querying should return the same physical address.
 */
TEST_CASE(test_pbt_x86_64_hal_mmu_map_query_roundtrip) {
    #define MAP_QUERY_ITERATIONS 100
    
    /* Get current address space */
    hal_addr_space_t space = hal_mmu_current_space();
    
    uint32_t success_count = 0;
    uint32_t skip_count = 0;
    
    for (uint32_t i = 0; i < MAP_QUERY_ITERATIONS; i++) {
        /* Generate random user-space virtual address */
        vaddr_t virt = pbt_random_user_vaddr();
        
        /* Skip if address is already mapped */
        paddr_t existing_phys;
        if (hal_mmu_query(space, virt, &existing_phys, NULL)) {
            skip_count++;
            continue;
        }
        
        /* Allocate a physical frame */
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            /* Out of memory, skip this iteration */
            skip_count++;
            continue;
        }
        
        /* Generate random flags (always include PRESENT) */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER;
        if (pbt_random() & 1) flags |= HAL_PAGE_WRITE;
        if (pbt_random() & 1) flags |= HAL_PAGE_EXEC;
        
        /* Map the page */
        bool map_result = hal_mmu_map(space, virt, phys, flags);
        if (!map_result) {
            /* Mapping failed (possibly out of memory for page tables) */
            pmm_free_frame(phys);
            skip_count++;
            continue;
        }
        
        /* Flush TLB for this address */
        hal_mmu_flush_tlb(virt);
        
        /* Query the mapping */
        paddr_t out_phys = 0;
        uint32_t out_flags = 0;
        bool query_result = hal_mmu_query(space, virt, &out_phys, &out_flags);
        
        /* Property: Query must succeed after successful map */
        ASSERT_TRUE(query_result);
        
        /* Property: Queried physical address must match mapped address */
        ASSERT_TRUE(out_phys == phys);
        
        /* Property: PRESENT flag must be set */
        ASSERT_TRUE((out_flags & HAL_PAGE_PRESENT) != 0);
        
        /* Property: USER flag must be set (we set it) */
        ASSERT_TRUE((out_flags & HAL_PAGE_USER) != 0);
        
        /* Clean up: unmap and free the frame */
        paddr_t unmapped_phys = hal_mmu_unmap(space, virt);
        ASSERT_TRUE(unmapped_phys == phys);
        
        hal_mmu_flush_tlb(virt);
        pmm_free_frame(phys);
        
        success_count++;
    }
    
    /* Ensure we ran at least some iterations successfully */
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test HAL MMU protect operation
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip (x86_64)**
 * **Validates: Requirements 5.1**
 * 
 * *For any* mapped page, modifying flags with hal_mmu_protect should
 * be reflected in subsequent hal_mmu_query calls.
 */
TEST_CASE(test_pbt_x86_64_hal_mmu_protect) {
    #define PROTECT_ITERATIONS 50
    
    hal_addr_space_t space = hal_mmu_current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < PROTECT_ITERATIONS; i++) {
        /* Generate random user-space virtual address */
        vaddr_t virt = pbt_random_user_vaddr();
        
        /* Skip if address is already mapped */
        if (hal_mmu_query(space, virt, NULL, NULL)) {
            continue;
        }
        
        /* Allocate a physical frame */
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        /* Map with write permission */
        uint32_t initial_flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (!hal_mmu_map(space, virt, phys, initial_flags)) {
            pmm_free_frame(phys);
            continue;
        }
        
        hal_mmu_flush_tlb(virt);
        
        /* Verify initial mapping */
        uint32_t out_flags = 0;
        ASSERT_TRUE(hal_mmu_query(space, virt, NULL, &out_flags));
        ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Remove write permission (simulate COW setup) */
        bool protect_result = hal_mmu_protect(space, virt, 0, HAL_PAGE_WRITE);
        ASSERT_TRUE(protect_result);
        
        hal_mmu_flush_tlb(virt);
        
        /* Verify write permission is removed */
        ASSERT_TRUE(hal_mmu_query(space, virt, NULL, &out_flags));
        ASSERT_FALSE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Restore write permission */
        protect_result = hal_mmu_protect(space, virt, HAL_PAGE_WRITE, 0);
        ASSERT_TRUE(protect_result);
        
        hal_mmu_flush_tlb(virt);
        
        /* Verify write permission is restored */
        ASSERT_TRUE(hal_mmu_query(space, virt, NULL, &out_flags));
        ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Clean up */
        hal_mmu_unmap(space, virt);
        hal_mmu_flush_tlb(virt);
        pmm_free_frame(phys);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test HAL MMU unmap returns correct physical address
 * 
 * **Feature: mm-refactor, Property 8: HAL MMU Map-Query Round-Trip (x86_64)**
 * **Validates: Requirements 5.1**
 * 
 * *For any* mapped page, hal_mmu_unmap should return the physical address
 * that was previously mapped.
 */
TEST_CASE(test_pbt_x86_64_hal_mmu_unmap_returns_phys) {
    #define UNMAP_ITERATIONS 50
    
    hal_addr_space_t space = hal_mmu_current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < UNMAP_ITERATIONS; i++) {
        vaddr_t virt = pbt_random_user_vaddr();
        
        if (hal_mmu_query(space, virt, NULL, NULL)) {
            continue;
        }
        
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        if (!hal_mmu_map(space, virt, phys, HAL_PAGE_PRESENT | HAL_PAGE_USER)) {
            pmm_free_frame(phys);
            continue;
        }
        
        hal_mmu_flush_tlb(virt);
        
        /* Unmap and verify returned physical address */
        paddr_t returned_phys = hal_mmu_unmap(space, virt);
        
        /* Property: Unmap must return the mapped physical address */
        ASSERT_TRUE(returned_phys == phys);
        
        hal_mmu_flush_tlb(virt);
        
        /* Property: After unmap, query should fail */
        ASSERT_FALSE(hal_mmu_query(space, virt, NULL, NULL));
        
        pmm_free_frame(phys);
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/* ============================================================================
 * Property 10: COW Clone Shares Physical Pages
 * Property 11: COW Write Triggers Copy
 * 
 * **Feature: mm-refactor**
 * **Validates: Requirements 5.3**
 * ============================================================================ */

/**
 * @brief Test that hal_mmu_create_space creates a valid address space
 * 
 * **Feature: mm-refactor, Property 10: COW Clone Shares Physical Pages**
 * **Validates: Requirements 5.2**
 * 
 * *For any* call to hal_mmu_create_space, the returned address space
 * SHALL have kernel mappings shared with the current address space.
 */
TEST_CASE(test_pbt_x86_64_create_space_kernel_shared) {
    /* Create a new address space */
    hal_addr_space_t new_space = hal_mmu_create_space();
    
    /* Property: Create space must succeed */
    ASSERT_TRUE(new_space != HAL_ADDR_SPACE_INVALID);
    
    /* Get current address space for comparison */
    hal_addr_space_t current_space = hal_mmu_current_space();
    
    /* Property: New space must be different from current */
    ASSERT_TRUE(new_space != current_space);
    
    /* Verify kernel space is shared by checking a kernel address mapping */
    /* Use the kernel virtual base address which should be mapped */
    vaddr_t kernel_addr = KERNEL_VIRTUAL_BASE_X64;
    
    paddr_t current_phys = 0;
    paddr_t new_phys = 0;
    uint32_t current_flags = 0;
    uint32_t new_flags = 0;
    
    bool current_mapped = hal_mmu_query(current_space, kernel_addr, &current_phys, &current_flags);
    bool new_mapped = hal_mmu_query(new_space, kernel_addr, &new_phys, &new_flags);
    
    /* Property: Kernel address must be mapped in both spaces */
    ASSERT_TRUE(current_mapped);
    ASSERT_TRUE(new_mapped);
    
    /* Property: Kernel mappings must point to same physical address */
    ASSERT_TRUE(current_phys == new_phys);
    
    /* Clean up */
    hal_mmu_destroy_space(new_space);
}

/**
 * @brief Test that hal_mmu_clone_space shares physical pages with COW
 * 
 * **Feature: mm-refactor, Property 10: COW Clone Shares Physical Pages**
 * **Validates: Requirements 5.3**
 * 
 * *For any* address space with mapped user pages, after hal_mmu_clone_space(),
 * both parent and child SHALL map the same virtual addresses to the same 
 * physical addresses (until write occurs).
 */
TEST_CASE(test_pbt_x86_64_cow_clone_shares_physical_pages) {
    #define COW_CLONE_ITERATIONS 20
    
    hal_addr_space_t current_space = hal_mmu_current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < COW_CLONE_ITERATIONS; i++) {
        /* Generate random user-space virtual address */
        vaddr_t virt = pbt_random_user_vaddr();
        
        /* Skip if address is already mapped */
        if (hal_mmu_query(current_space, virt, NULL, NULL)) {
            continue;
        }
        
        /* Allocate a physical frame */
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        /* Map with write permission in current space */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (!hal_mmu_map(current_space, virt, phys, flags)) {
            pmm_free_frame(phys);
            continue;
        }
        
        hal_mmu_flush_tlb(virt);
        
        /* Get initial reference count */
        uint32_t initial_refcount = pmm_frame_get_refcount(phys);
        
        /* Clone the address space */
        hal_addr_space_t cloned_space = hal_mmu_clone_space(current_space);
        if (cloned_space == HAL_ADDR_SPACE_INVALID) {
            hal_mmu_unmap(current_space, virt);
            hal_mmu_flush_tlb(virt);
            pmm_free_frame(phys);
            continue;
        }
        
        /* Property 10: Both spaces should map to the same physical address */
        paddr_t parent_phys = 0;
        paddr_t child_phys = 0;
        uint32_t parent_flags = 0;
        uint32_t child_flags = 0;
        
        bool parent_mapped = hal_mmu_query(current_space, virt, &parent_phys, &parent_flags);
        bool child_mapped = hal_mmu_query(cloned_space, virt, &child_phys, &child_flags);
        
        ASSERT_TRUE(parent_mapped);
        ASSERT_TRUE(child_mapped);
        
        /* Property: Both should point to same physical page */
        ASSERT_TRUE(parent_phys == child_phys);
        ASSERT_TRUE(parent_phys == phys);
        
        /* Property: Reference count should have increased */
        uint32_t new_refcount = pmm_frame_get_refcount(phys);
        ASSERT_TRUE(new_refcount > initial_refcount);
        
        /* Property: Both should have COW flag set (write removed) */
        ASSERT_TRUE((parent_flags & HAL_PAGE_COW) != 0);
        ASSERT_TRUE((child_flags & HAL_PAGE_COW) != 0);
        ASSERT_FALSE((parent_flags & HAL_PAGE_WRITE) != 0);
        ASSERT_FALSE((child_flags & HAL_PAGE_WRITE) != 0);
        
        /* Clean up: destroy cloned space first */
        hal_mmu_destroy_space(cloned_space);
        
        /* Unmap from current space */
        hal_mmu_unmap(current_space, virt);
        hal_mmu_flush_tlb(virt);
        
        /* Free the physical frame (refcount should be back to allowing free) */
        pmm_free_frame(phys);
        
        success_count++;
    }
    
    /* Ensure we ran at least some iterations successfully */
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test that COW pages have write permission removed
 * 
 * **Feature: mm-refactor, Property 11: COW Write Triggers Copy**
 * **Validates: Requirements 5.3**
 * 
 * *For any* COW-marked page, the page SHALL be marked read-only
 * (write permission removed) to trigger page fault on write.
 */
TEST_CASE(test_pbt_x86_64_cow_removes_write_permission) {
    #define COW_WRITE_ITERATIONS 20
    
    hal_addr_space_t current_space = hal_mmu_current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < COW_WRITE_ITERATIONS; i++) {
        vaddr_t virt = pbt_random_user_vaddr();
        
        if (hal_mmu_query(current_space, virt, NULL, NULL)) {
            continue;
        }
        
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        /* Map with write permission */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (!hal_mmu_map(current_space, virt, phys, flags)) {
            pmm_free_frame(phys);
            continue;
        }
        
        hal_mmu_flush_tlb(virt);
        
        /* Verify write permission is set initially */
        uint32_t out_flags = 0;
        ASSERT_TRUE(hal_mmu_query(current_space, virt, NULL, &out_flags));
        ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Clone the address space */
        hal_addr_space_t cloned_space = hal_mmu_clone_space(current_space);
        if (cloned_space == HAL_ADDR_SPACE_INVALID) {
            hal_mmu_unmap(current_space, virt);
            hal_mmu_flush_tlb(virt);
            pmm_free_frame(phys);
            continue;
        }
        
        /* Property 11: After clone, write permission should be removed */
        ASSERT_TRUE(hal_mmu_query(current_space, virt, NULL, &out_flags));
        
        /* Property: Write permission must be removed */
        ASSERT_FALSE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Property: COW flag must be set */
        ASSERT_TRUE((out_flags & HAL_PAGE_COW) != 0);
        
        /* Clean up */
        hal_mmu_destroy_space(cloned_space);
        hal_mmu_unmap(current_space, virt);
        hal_mmu_flush_tlb(virt);
        pmm_free_frame(phys);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/* ============================================================================
 * Property 15: Address Space Destruction Frees Memory
 * 
 * **Feature: mm-refactor**
 * **Validates: Requirements 5.5**
 * ============================================================================ */

/**
 * @brief Test that hal_mmu_destroy_space frees page table memory
 * 
 * **Feature: mm-refactor, Property 15: Address Space Destruction Frees Memory**
 * **Validates: Requirements 5.5**
 * 
 * *For any* address space, after hal_mmu_destroy_space(), the PMM free frame
 * count SHALL increase by the number of page table frames used.
 */
TEST_CASE(test_pbt_x86_64_destroy_space_frees_memory) {
    #define DESTROY_SPACE_ITERATIONS 10
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < DESTROY_SPACE_ITERATIONS; i++) {
        /* Record initial free frame count */
        pmm_info_t info_before = pmm_get_info();
        
        /* Create a new address space */
        hal_addr_space_t new_space = hal_mmu_create_space();
        if (new_space == HAL_ADDR_SPACE_INVALID) {
            continue;
        }
        
        /* Map some pages in the new address space */
        uint32_t pages_mapped = 0;
        
        for (uint32_t j = 0; j < 5; j++) {
            vaddr_t virt = pbt_random_user_vaddr();
            
            /* Skip if already mapped */
            if (hal_mmu_query(new_space, virt, NULL, NULL)) {
                continue;
            }
            
            paddr_t phys = pmm_alloc_frame();
            if (phys == PADDR_INVALID) {
                continue;
            }
            
            if (hal_mmu_map(new_space, virt, phys, 
                           HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE)) {
                pages_mapped++;
            } else {
                pmm_free_frame(phys);
            }
        }
        
        /* Record free frame count after mapping */
        pmm_info_t info_after_map = pmm_get_info();
        
        /* Property: Mapping should have consumed frames */
        /* At minimum: 1 for PML4 + some for page tables + mapped pages */
        ASSERT_TRUE(info_after_map.free_frames < info_before.free_frames);
        
        /* Destroy the address space */
        hal_mmu_destroy_space(new_space);
        
        /* Record free frame count after destruction */
        pmm_info_t info_after_destroy = pmm_get_info();
        
        /* Property 15: Free frame count should increase after destruction */
        /* The increase should be at least the number of mapped pages + page tables */
        ASSERT_TRUE(info_after_destroy.free_frames > info_after_map.free_frames);
        
        /* Property: Should recover most of the allocated frames */
        /* Note: We may not recover all frames due to reference counting */
        /* but we should recover at least the page table frames */
        uint64_t frames_recovered = info_after_destroy.free_frames - info_after_map.free_frames;
        ASSERT_TRUE(frames_recovered >= 1);  /* At least PML4 should be freed */
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test that destroying cloned space decrements reference counts
 * 
 * **Feature: mm-refactor, Property 15: Address Space Destruction Frees Memory**
 * **Validates: Requirements 5.5**
 * 
 * *For any* cloned address space with COW pages, destroying the clone
 * SHALL decrement reference counts on shared physical pages.
 */
TEST_CASE(test_pbt_x86_64_destroy_cloned_space_decrements_refcount) {
    #define DESTROY_CLONE_ITERATIONS 10
    
    hal_addr_space_t current_space = hal_mmu_current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < DESTROY_CLONE_ITERATIONS; i++) {
        vaddr_t virt = pbt_random_user_vaddr();
        
        if (hal_mmu_query(current_space, virt, NULL, NULL)) {
            continue;
        }
        
        paddr_t phys = pmm_alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        if (!hal_mmu_map(current_space, virt, phys, 
                        HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE)) {
            pmm_free_frame(phys);
            continue;
        }
        
        hal_mmu_flush_tlb(virt);
        
        /* Get initial reference count */
        uint32_t initial_refcount = pmm_frame_get_refcount(phys);
        
        /* Clone the address space */
        hal_addr_space_t cloned_space = hal_mmu_clone_space(current_space);
        if (cloned_space == HAL_ADDR_SPACE_INVALID) {
            hal_mmu_unmap(current_space, virt);
            hal_mmu_flush_tlb(virt);
            pmm_free_frame(phys);
            continue;
        }
        
        /* Reference count should have increased */
        uint32_t after_clone_refcount = pmm_frame_get_refcount(phys);
        ASSERT_TRUE(after_clone_refcount > initial_refcount);
        
        /* Destroy the cloned space */
        hal_mmu_destroy_space(cloned_space);
        
        /* Property 15: Reference count should decrease after destruction */
        uint32_t after_destroy_refcount = pmm_frame_get_refcount(phys);
        ASSERT_TRUE(after_destroy_refcount < after_clone_refcount);
        
        /* Property: Reference count should be back to initial (or close) */
        /* Note: The clone incremented it, destroy should decrement it */
        ASSERT_TRUE(after_destroy_refcount == initial_refcount);
        
        /* Clean up */
        hal_mmu_unmap(current_space, virt);
        hal_mmu_flush_tlb(virt);
        pmm_free_frame(phys);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/* ============================================================================
 * Test Suites
 * ============================================================================ */

TEST_SUITE(paging64_kernel_range_tests) {
    RUN_TEST(test_pbt_x86_64_kernel_base_address);
    RUN_TEST(test_pbt_x86_64_kernel_canonical_addresses);
    RUN_TEST(test_pbt_x86_64_user_canonical_addresses);
    RUN_TEST(test_pbt_x86_64_noncanonical_addresses);
    RUN_TEST(test_pbt_x86_64_page_table_levels);
    RUN_TEST(test_pbt_x86_64_page_size);
}

TEST_SUITE(paging64_page_fault_tests) {
    RUN_TEST(test_pbt_x86_64_page_fault_present_bit);
    RUN_TEST(test_pbt_x86_64_page_fault_write_bit);
    RUN_TEST(test_pbt_x86_64_page_fault_user_bit);
    RUN_TEST(test_pbt_x86_64_cow_fault_detection);
    RUN_TEST(test_pbt_x86_64_pte_validation);
}

TEST_SUITE(paging64_hal_mmu_tests) {
    RUN_TEST(test_pbt_x86_64_hal_mmu_map_query_roundtrip);
    RUN_TEST(test_pbt_x86_64_hal_mmu_protect);
    RUN_TEST(test_pbt_x86_64_hal_mmu_unmap_returns_phys);
}

TEST_SUITE(paging64_cow_tests) {
    RUN_TEST(test_pbt_x86_64_create_space_kernel_shared);
    RUN_TEST(test_pbt_x86_64_cow_clone_shares_physical_pages);
    RUN_TEST(test_pbt_x86_64_cow_removes_write_permission);
}

TEST_SUITE(paging64_destroy_space_tests) {
    RUN_TEST(test_pbt_x86_64_destroy_space_frees_memory);
    RUN_TEST(test_pbt_x86_64_destroy_cloned_space_decrements_refcount);
}

#endif /* ARCH_X86_64 */

/* ============================================================================
 * Run All Tests
 * ============================================================================ */

void run_paging64_tests(void) {
#ifdef ARCH_X86_64
    unittest_init();
    
    /* Property 4: VMM Kernel Mapping Range Correctness (x86_64) */
    /* **Validates: Requirements 5.3** */
    RUN_SUITE(paging64_kernel_range_tests);
    
    /* Property 5: VMM Page Fault Interpretation (x86_64) */
    /* **Validates: Requirements 5.4** */
    RUN_SUITE(paging64_page_fault_tests);
    
    /* Property 8: HAL MMU Map-Query Round-Trip (x86_64) */
    /* **Feature: mm-refactor, Property 8** */
    /* **Validates: Requirements 5.1** */
    RUN_SUITE(paging64_hal_mmu_tests);
    
    /* Property 10: COW Clone Shares Physical Pages */
    /* Property 11: COW Write Triggers Copy */
    /* **Feature: mm-refactor, Property 10, 11** */
    /* **Validates: Requirements 5.3** */
    RUN_SUITE(paging64_cow_tests);
    
    /* Property 15: Address Space Destruction Frees Memory */
    /* **Feature: mm-refactor, Property 15** */
    /* **Validates: Requirements 5.5** */
    RUN_SUITE(paging64_destroy_space_tests);
    
    unittest_print_summary();
#else
    kprintf("Paging64 tests skipped (not x86_64 architecture)\n");
#endif
}
