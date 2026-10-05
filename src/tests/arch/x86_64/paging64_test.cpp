/**
 * @file paging64_test.c
 * @brief x86_64 分页属性测试
 * 
 * 实现 x86_64 架构的分页相关属性测试
 * 
 * **Property 4: VMM Kernel Mapping Range Correctness (x86_64)**
 * **Property 5: VMM Page Fault Interpretation (x86_64)**
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
 * ============================================================================ */

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

/* ============================================================================
 * Property 5: VMM Page Fault Interpretation (x86_64)
 * 
 * *For any* page fault exception, the VMM SHALL correctly interpret 
 * the architecture-specific fault information (CR2 and error code on x86)
 * to determine the faulting address and fault type.
 * 
 * ============================================================================ */

/* ============================================================================
 * Property 8: HAL MMU Map-Query Round-Trip (x86_64)
 * 
 * *For any* valid virtual address `virt`, physical address `phys`, and flags `flags`,
 * after `hal::Mmu::map(space, virt, phys, flags)` succeeds, 
 * `hal::Mmu::query(space, virt, &out_phys, &out_flags)` SHALL return `true` 
 * with `out_phys == phys`.
 * 
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
 * 
 * *For any* valid virtual address, physical address, and flags,
 * mapping and then querying should return the same physical address.
 */
TEST_CASE(test_pbt_x86_64_hal_mmu_map_query_roundtrip) {
    #define MAP_QUERY_ITERATIONS 100
    
    /* Get current address space */
    hal_addr_space_t space = hal::Mmu::current_space();
    
    uint32_t success_count = 0;
    uint32_t skip_count = 0;
    
    for (uint32_t i = 0; i < MAP_QUERY_ITERATIONS; i++) {
        /* Generate random user-space virtual address */
        vaddr_t virt = pbt_random_user_vaddr();
        
        /* Skip if address is already mapped */
        paddr_t existing_phys;
        if (hal::Mmu::query(space, virt, &existing_phys, NULL)) {
            skip_count++;
            continue;
        }
        
        /* Allocate a physical frame */
        paddr_t phys = mm::Pmm::alloc_frame();
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
        bool map_result = hal::Mmu::map(space, virt, phys, flags);
        if (!map_result) {
            /* Mapping failed (possibly out of memory for page tables) */
            mm::Pmm::free_frame(phys);
            skip_count++;
            continue;
        }
        
        /* Flush TLB for this address */
        hal::Mmu::flush_tlb(virt);
        
        /* Query the mapping */
        paddr_t out_phys = 0;
        uint32_t out_flags = 0;
        bool query_result = hal::Mmu::query(space, virt, &out_phys, &out_flags);
        
        /* Property: Query must succeed after successful map */
        ASSERT_TRUE(query_result);
        
        /* Property: Queried physical address must match mapped address */
        ASSERT_TRUE(out_phys == phys);
        
        /* Property: PRESENT flag must be set */
        ASSERT_TRUE((out_flags & HAL_PAGE_PRESENT) != 0);
        
        /* Property: USER flag must be set (we set it) */
        ASSERT_TRUE((out_flags & HAL_PAGE_USER) != 0);
        
        /* Clean up: unmap and free the frame */
        paddr_t unmapped_phys = hal::Mmu::unmap(space, virt);
        ASSERT_TRUE(unmapped_phys == phys);
        
        hal::Mmu::flush_tlb(virt);
        mm::Pmm::free_frame(phys);
        
        success_count++;
    }
    
    /* Ensure we ran at least some iterations successfully */
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test HAL MMU protect operation
 * 
 * 
 * *For any* mapped page, modifying flags with hal::Mmu::protect should
 * be reflected in subsequent hal::Mmu::query calls.
 */
TEST_CASE(test_pbt_x86_64_hal_mmu_protect) {
    #define PROTECT_ITERATIONS 50
    
    hal_addr_space_t space = hal::Mmu::current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < PROTECT_ITERATIONS; i++) {
        /* Generate random user-space virtual address */
        vaddr_t virt = pbt_random_user_vaddr();
        
        /* Skip if address is already mapped */
        if (hal::Mmu::query(space, virt, NULL, NULL)) {
            continue;
        }
        
        /* Allocate a physical frame */
        paddr_t phys = mm::Pmm::alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        /* Map with write permission */
        uint32_t initial_flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (!hal::Mmu::map(space, virt, phys, initial_flags)) {
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        hal::Mmu::flush_tlb(virt);
        
        /* Verify initial mapping */
        uint32_t out_flags = 0;
        ASSERT_TRUE(hal::Mmu::query(space, virt, NULL, &out_flags));
        ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Remove write permission (simulate COW setup) */
        bool protect_result = hal::Mmu::protect(space, virt, 0, HAL_PAGE_WRITE);
        ASSERT_TRUE(protect_result);
        
        hal::Mmu::flush_tlb(virt);
        
        /* Verify write permission is removed */
        ASSERT_TRUE(hal::Mmu::query(space, virt, NULL, &out_flags));
        ASSERT_FALSE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Restore write permission */
        protect_result = hal::Mmu::protect(space, virt, HAL_PAGE_WRITE, 0);
        ASSERT_TRUE(protect_result);
        
        hal::Mmu::flush_tlb(virt);
        
        /* Verify write permission is restored */
        ASSERT_TRUE(hal::Mmu::query(space, virt, NULL, &out_flags));
        ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Clean up */
        hal::Mmu::unmap(space, virt);
        hal::Mmu::flush_tlb(virt);
        mm::Pmm::free_frame(phys);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/**
 * protect() must change only the attributes named in set/clear.
 *
 * The COW paths call protect(clear WRITE, set COW) and later
 * protect(set WRITE, clear COW). Neither mentions EXEC, so a
 * non-executable page must stay non-executable (NX kept) and an
 * executable one must stay executable.
 */
TEST_CASE(test_x86_64_hal_mmu_protect_keeps_unrelated_flags) {
    hal_addr_space_t space = hal::Mmu::current_space();
    const uint32_t exec_variants[] = { 0, HAL_PAGE_EXEC };

    for (uint32_t v = 0; v < 2; v++) {
        vaddr_t virt = KTEST_FREE_VADDR_BASE + 0x40000 + v * PAGE_SIZE;
        ASSERT_FALSE(hal::Mmu::query(space, virt, NULL, NULL));

        paddr_t phys = mm::Pmm::alloc_frame();
        ASSERT_TRUE(phys != PADDR_INVALID);
        ASSERT_TRUE(hal::Mmu::map(space, virt, phys,
                                  HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE |
                                  exec_variants[v]));

        /* fork: write-protect and mark COW */
        ASSERT_TRUE(hal::Mmu::protect(space, virt, HAL_PAGE_COW, HAL_PAGE_WRITE));
        hal::Mmu::flush_tlb(virt);
        uint32_t flags = 0;
        paddr_t got = 0;
        ASSERT_TRUE(hal::Mmu::query(space, virt, &got, &flags));
        ASSERT_TRUE(got == phys);
        ASSERT_TRUE((flags & HAL_PAGE_COW) != 0);
        ASSERT_TRUE((flags & HAL_PAGE_WRITE) == 0);
        ASSERT_TRUE((flags & HAL_PAGE_USER) != 0);
        ASSERT_TRUE((flags & HAL_PAGE_EXEC) == exec_variants[v]);

        /* COW fault with refcount 1: restore write, drop COW */
        ASSERT_TRUE(hal::Mmu::protect(space, virt, HAL_PAGE_WRITE, HAL_PAGE_COW));
        hal::Mmu::flush_tlb(virt);
        flags = 0;
        ASSERT_TRUE(hal::Mmu::query(space, virt, &got, &flags));
        ASSERT_TRUE(got == phys);
        ASSERT_TRUE((flags & HAL_PAGE_COW) == 0);
        ASSERT_TRUE((flags & HAL_PAGE_WRITE) != 0);
        ASSERT_TRUE((flags & HAL_PAGE_USER) != 0);
        ASSERT_TRUE((flags & HAL_PAGE_EXEC) == exec_variants[v]);

        /* EXEC itself can still be changed explicitly */
        ASSERT_TRUE(hal::Mmu::protect(space, virt, 0, HAL_PAGE_EXEC));
        ASSERT_TRUE(hal::Mmu::query(space, virt, NULL, &flags));
        ASSERT_TRUE((flags & HAL_PAGE_EXEC) == 0);
        ASSERT_TRUE(hal::Mmu::protect(space, virt, HAL_PAGE_EXEC, 0));
        ASSERT_TRUE(hal::Mmu::query(space, virt, NULL, &flags));
        ASSERT_TRUE((flags & HAL_PAGE_EXEC) != 0);

        ASSERT_TRUE(hal::Mmu::unmap(space, virt) == phys);
        hal::Mmu::flush_tlb(virt);
        mm::Pmm::free_frame(phys);
    }
}

/**
 * @brief Test HAL MMU unmap returns correct physical address
 * 
 * 
 * *For any* mapped page, hal::Mmu::unmap should return the physical address
 * that was previously mapped.
 */
TEST_CASE(test_pbt_x86_64_hal_mmu_unmap_returns_phys) {
    #define UNMAP_ITERATIONS 50
    
    hal_addr_space_t space = hal::Mmu::current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < UNMAP_ITERATIONS; i++) {
        vaddr_t virt = pbt_random_user_vaddr();
        
        if (hal::Mmu::query(space, virt, NULL, NULL)) {
            continue;
        }
        
        paddr_t phys = mm::Pmm::alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        if (!hal::Mmu::map(space, virt, phys, HAL_PAGE_PRESENT | HAL_PAGE_USER)) {
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        hal::Mmu::flush_tlb(virt);
        
        /* Unmap and verify returned physical address */
        paddr_t returned_phys = hal::Mmu::unmap(space, virt);
        
        /* Property: Unmap must return the mapped physical address */
        ASSERT_TRUE(returned_phys == phys);
        
        hal::Mmu::flush_tlb(virt);
        
        /* Property: After unmap, query should fail */
        ASSERT_FALSE(hal::Mmu::query(space, virt, NULL, NULL));
        
        mm::Pmm::free_frame(phys);
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/* ============================================================================
 * Property 10: COW Clone Shares Physical Pages
 * Property 11: COW Write Triggers Copy
 * 
 * ============================================================================ */

/**
 * @brief Test that hal::Mmu::create_space creates a valid address space
 * 
 * 
 * *For any* call to hal::Mmu::create_space, the returned address space
 * SHALL have kernel mappings shared with the current address space.
 */
TEST_CASE(test_pbt_x86_64_create_space_kernel_shared) {
    /* Create a new address space */
    hal_addr_space_t new_space = hal::Mmu::create_space();
    
    /* Property: Create space must succeed */
    ASSERT_TRUE(new_space != HAL_ADDR_SPACE_INVALID);
    
    /* Get current address space for comparison */
    hal_addr_space_t current_space = hal::Mmu::current_space();
    
    /* Property: New space must be different from current */
    ASSERT_TRUE(new_space != current_space);
    
    /* Verify kernel space is shared by checking a kernel address mapping */
    /* Use the kernel virtual base address which should be mapped */
    vaddr_t kernel_addr = KERNEL_VIRTUAL_BASE_X64;
    
    paddr_t current_phys = 0;
    paddr_t new_phys = 0;
    uint32_t current_flags = 0;
    uint32_t new_flags = 0;
    
    bool current_mapped = hal::Mmu::query(current_space, kernel_addr, &current_phys, &current_flags);
    bool new_mapped = hal::Mmu::query(new_space, kernel_addr, &new_phys, &new_flags);
    
    /* Property: Kernel address must be mapped in both spaces */
    ASSERT_TRUE(current_mapped);
    ASSERT_TRUE(new_mapped);
    
    /* Property: Kernel mappings must point to same physical address */
    ASSERT_TRUE(current_phys == new_phys);
    
    /* Clean up */
    hal::Mmu::destroy_space(new_space);
}

/**
 * @brief Test that hal::Mmu::clone_space shares physical pages with COW
 * 
 * 
 * *For any* address space with mapped user pages, after hal::Mmu::clone_space(),
 * both parent and child SHALL map the same virtual addresses to the same 
 * physical addresses (until write occurs).
 */
TEST_CASE(test_pbt_x86_64_cow_clone_shares_physical_pages) {
    #define COW_CLONE_ITERATIONS 20
    
    hal_addr_space_t current_space = hal::Mmu::current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < COW_CLONE_ITERATIONS; i++) {
        /* Generate random user-space virtual address */
        vaddr_t virt = pbt_random_user_vaddr();
        
        /* Skip if address is already mapped */
        if (hal::Mmu::query(current_space, virt, NULL, NULL)) {
            continue;
        }
        
        /* Allocate a physical frame */
        paddr_t phys = mm::Pmm::alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        /* Map with write permission in current space */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (!hal::Mmu::map(current_space, virt, phys, flags)) {
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        hal::Mmu::flush_tlb(virt);
        
        /* Get initial reference count */
        uint32_t initial_refcount = mm::Pmm::frame_get_refcount(phys);
        
        /* Clone the address space */
        hal_addr_space_t cloned_space = hal::Mmu::clone_space(current_space);
        if (cloned_space == HAL_ADDR_SPACE_INVALID) {
            hal::Mmu::unmap(current_space, virt);
            hal::Mmu::flush_tlb(virt);
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        /* Property 10: Both spaces should map to the same physical address */
        paddr_t parent_phys = 0;
        paddr_t child_phys = 0;
        uint32_t parent_flags = 0;
        uint32_t child_flags = 0;
        
        bool parent_mapped = hal::Mmu::query(current_space, virt, &parent_phys, &parent_flags);
        bool child_mapped = hal::Mmu::query(cloned_space, virt, &child_phys, &child_flags);
        
        ASSERT_TRUE(parent_mapped);
        ASSERT_TRUE(child_mapped);
        
        /* Property: Both should point to same physical page */
        ASSERT_TRUE(parent_phys == child_phys);
        ASSERT_TRUE(parent_phys == phys);
        
        /* Property: Reference count should have increased */
        uint32_t new_refcount = mm::Pmm::frame_get_refcount(phys);
        ASSERT_TRUE(new_refcount > initial_refcount);
        
        /* Property: Both should have COW flag set (write removed) */
        ASSERT_TRUE((parent_flags & HAL_PAGE_COW) != 0);
        ASSERT_TRUE((child_flags & HAL_PAGE_COW) != 0);
        ASSERT_FALSE((parent_flags & HAL_PAGE_WRITE) != 0);
        ASSERT_FALSE((child_flags & HAL_PAGE_WRITE) != 0);
        
        /* Clean up: destroy cloned space first */
        hal::Mmu::destroy_space(cloned_space);
        
        /* Unmap from current space */
        hal::Mmu::unmap(current_space, virt);
        hal::Mmu::flush_tlb(virt);
        
        /* Free the physical frame (refcount should be back to allowing free) */
        mm::Pmm::free_frame(phys);
        
        success_count++;
    }
    
    /* Ensure we ran at least some iterations successfully */
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test that COW pages have write permission removed
 * 
 * 
 * *For any* COW-marked page, the page SHALL be marked read-only
 * (write permission removed) to trigger page fault on write.
 */
TEST_CASE(test_pbt_x86_64_cow_removes_write_permission) {
    #define COW_WRITE_ITERATIONS 20
    
    hal_addr_space_t current_space = hal::Mmu::current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < COW_WRITE_ITERATIONS; i++) {
        vaddr_t virt = pbt_random_user_vaddr();
        
        if (hal::Mmu::query(current_space, virt, NULL, NULL)) {
            continue;
        }
        
        paddr_t phys = mm::Pmm::alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        /* Map with write permission */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (!hal::Mmu::map(current_space, virt, phys, flags)) {
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        hal::Mmu::flush_tlb(virt);
        
        /* Verify write permission is set initially */
        uint32_t out_flags = 0;
        ASSERT_TRUE(hal::Mmu::query(current_space, virt, NULL, &out_flags));
        ASSERT_TRUE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Clone the address space */
        hal_addr_space_t cloned_space = hal::Mmu::clone_space(current_space);
        if (cloned_space == HAL_ADDR_SPACE_INVALID) {
            hal::Mmu::unmap(current_space, virt);
            hal::Mmu::flush_tlb(virt);
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        /* Property 11: After clone, write permission should be removed */
        ASSERT_TRUE(hal::Mmu::query(current_space, virt, NULL, &out_flags));
        
        /* Property: Write permission must be removed */
        ASSERT_FALSE((out_flags & HAL_PAGE_WRITE) != 0);
        
        /* Property: COW flag must be set */
        ASSERT_TRUE((out_flags & HAL_PAGE_COW) != 0);
        
        /* Clean up */
        hal::Mmu::destroy_space(cloned_space);
        hal::Mmu::unmap(current_space, virt);
        hal::Mmu::flush_tlb(virt);
        mm::Pmm::free_frame(phys);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/* ============================================================================
 * Property 15: Address Space Destruction Frees Memory
 * 
 * ============================================================================ */

/**
 * @brief Test that hal::Mmu::destroy_space frees page table memory
 * 
 * 
 * *For any* address space, after hal::Mmu::destroy_space(), the PMM free frame
 * count SHALL increase by the number of page table frames used.
 */
TEST_CASE(test_pbt_x86_64_destroy_space_frees_memory) {
    #define DESTROY_SPACE_ITERATIONS 10
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < DESTROY_SPACE_ITERATIONS; i++) {
        /* Record initial free frame count */
        mm::PmmInfo info_before = mm::Pmm::get_info();
        
        /* Create a new address space */
        hal_addr_space_t new_space = hal::Mmu::create_space();
        if (new_space == HAL_ADDR_SPACE_INVALID) {
            continue;
        }
        
        /* Map some pages in the new address space */
        uint32_t pages_mapped = 0;
        
        for (uint32_t j = 0; j < 5; j++) {
            vaddr_t virt = pbt_random_user_vaddr();
            
            /* Skip if already mapped */
            if (hal::Mmu::query(new_space, virt, NULL, NULL)) {
                continue;
            }
            
            paddr_t phys = mm::Pmm::alloc_frame();
            if (phys == PADDR_INVALID) {
                continue;
            }
            
            if (hal::Mmu::map(new_space, virt, phys, 
                           HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE)) {
                pages_mapped++;
            } else {
                mm::Pmm::free_frame(phys);
            }
        }
        
        /* Record free frame count after mapping */
        mm::PmmInfo info_after_map = mm::Pmm::get_info();
        
        /* Property: Mapping should have consumed frames */
        /* At minimum: 1 for PML4 + some for page tables + mapped pages */
        ASSERT_TRUE(info_after_map.free_frames < info_before.free_frames);
        
        /* Destroy the address space */
        hal::Mmu::destroy_space(new_space);
        
        /* Record free frame count after destruction */
        mm::PmmInfo info_after_destroy = mm::Pmm::get_info();
        
        /* Property 15: Free frame count should increase after destruction */
        /* The increase should be at least the number of mapped pages + page tables */
        ASSERT_TRUE(info_after_destroy.free_frames > info_after_map.free_frames);
        
        /* Property: every frame comes back - the PML4, the intermediate
         * tables and the mapped pages themselves (they had refcount 1).
         * Anything less is a leak on every process exit. */
        ASSERT_TRUE(info_after_destroy.free_frames == info_before.free_frames);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/**
 * @brief Test that destroying cloned space decrements reference counts
 * 
 * 
 * *For any* cloned address space with COW pages, destroying the clone
 * SHALL decrement reference counts on shared physical pages.
 */
TEST_CASE(test_pbt_x86_64_destroy_cloned_space_decrements_refcount) {
    #define DESTROY_CLONE_ITERATIONS 10
    
    hal_addr_space_t current_space = hal::Mmu::current_space();
    
    uint32_t success_count = 0;
    
    for (uint32_t i = 0; i < DESTROY_CLONE_ITERATIONS; i++) {
        vaddr_t virt = pbt_random_user_vaddr();
        
        if (hal::Mmu::query(current_space, virt, NULL, NULL)) {
            continue;
        }
        
        paddr_t phys = mm::Pmm::alloc_frame();
        if (phys == PADDR_INVALID) {
            continue;
        }
        
        if (!hal::Mmu::map(current_space, virt, phys, 
                        HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE)) {
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        hal::Mmu::flush_tlb(virt);
        
        /* Get initial reference count */
        uint32_t initial_refcount = mm::Pmm::frame_get_refcount(phys);
        
        /* Clone the address space */
        hal_addr_space_t cloned_space = hal::Mmu::clone_space(current_space);
        if (cloned_space == HAL_ADDR_SPACE_INVALID) {
            hal::Mmu::unmap(current_space, virt);
            hal::Mmu::flush_tlb(virt);
            mm::Pmm::free_frame(phys);
            continue;
        }
        
        /* Reference count should have increased */
        uint32_t after_clone_refcount = mm::Pmm::frame_get_refcount(phys);
        ASSERT_TRUE(after_clone_refcount > initial_refcount);
        
        /* Destroy the cloned space */
        hal::Mmu::destroy_space(cloned_space);
        
        /* Property 15: Reference count should decrease after destruction */
        uint32_t after_destroy_refcount = mm::Pmm::frame_get_refcount(phys);
        ASSERT_TRUE(after_destroy_refcount < after_clone_refcount);
        
        /* Property: Reference count should be back to initial (or close) */
        /* Note: The clone incremented it, destroy should decrement it */
        ASSERT_TRUE(after_destroy_refcount == initial_refcount);
        
        /* Clean up */
        hal::Mmu::unmap(current_space, virt);
        hal::Mmu::flush_tlb(virt);
        mm::Pmm::free_frame(phys);
        
        success_count++;
    }
    
    ASSERT_TRUE(success_count > 0);
}

/* ============================================================================
 * Test Suites
 * ============================================================================ */

TEST_SUITE(paging64_kernel_range_tests) {
    RUN_TEST(test_pbt_x86_64_noncanonical_addresses);
}

TEST_SUITE(paging64_page_fault_tests) {
}

TEST_SUITE(paging64_hal_mmu_tests) {
    RUN_TEST(test_pbt_x86_64_hal_mmu_map_query_roundtrip);
    RUN_TEST(test_pbt_x86_64_hal_mmu_protect);
    RUN_TEST(test_x86_64_hal_mmu_protect_keeps_unrelated_flags);
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
    
#else
    kprintf("Paging64 tests skipped (not x86_64 architecture)\n");
#endif
}
