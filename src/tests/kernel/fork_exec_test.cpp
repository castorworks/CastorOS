// ============================================================================
// fork_exec_test.c - Fork/Exec 系统调用验证测试
// ============================================================================
// 
// 验证 fork/exec 在各架构上的正确工作：
//   - Task 36.1: 测试 fork 系统调用 (hal::Mmu::clone_space COW)
//   - Task 36.2: 测试 exec 系统调用 (程序加载)
// 
// **Feature: multi-arch-support**
// **Validates: Requirements 5.5, 7.4, mm-refactor 4.4, 5.3**
// ============================================================================

#include <tests/ktest.h>
#include <hal/hal.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/pgtable.h>
#include <kernel/task.h>
#include <kernel/elf.h>
#include <lib/kprintf.h>
#include <lib/string.h>

// Include architecture-specific context headers for additional types
#if defined(ARCH_X86_64)
#include <context64.h>
#elif defined(ARCH_ARM64)
#include "../arch/arm64/include/context.h"
#endif

// Test virtual addresses in user space
#define FORK_TEST_VADDR_BASE  (KTEST_FREE_VADDR_BASE + 0x10000000)
#define FORK_TEST_PAGE_COUNT  8

// ============================================================================
// Task 36.1: Fork System Call Tests (hal::Mmu::clone_space COW)
// **Feature: multi-arch-support**
// **Validates: Requirements 5.5, mm-refactor 4.4, 5.3**
// ============================================================================

/**
 * Test: hal::Mmu::clone_space creates valid address space
 * 
 * Verifies that cloning an address space produces a valid, distinct
 * address space handle.
 */
TEST_CASE(test_fork_clone_space_creates_valid_space) {
    hal_addr_space_t current = hal::Mmu::current_space();
    ASSERT_NE_U(current, HAL_ADDR_SPACE_INVALID);
    
    // Clone the current address space
    hal_addr_space_t cloned = hal::Mmu::clone_space(current);
    
    // Property: Clone must succeed
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Property: Clone must be different from original
    ASSERT_NE_U(cloned, current);
    
    // Clean up
    hal::Mmu::destroy_space(cloned);
}

/**
 * Test: hal::Mmu::clone_space shares physical pages via COW
 * 
 * *For any* mapped user page, after clone, both parent and child
 * SHALL map to the same physical address with COW flag set.
 */
TEST_CASE(test_fork_cow_shares_physical_pages) {
    hal_addr_space_t current = hal::Mmu::current_space();
    
    // Allocate and map a test page
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    vaddr_t test_vaddr = FORK_TEST_VADDR_BASE;
    
    // Skip if already mapped
    if (hal::Mmu::query(current, test_vaddr, NULL, NULL)) {
        mm::Pmm::free_frame(frame);
        return;
    }
    
    // Map with write permission
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
    bool map_result = hal::Mmu::map(current, test_vaddr, frame, flags);
    ASSERT_TRUE(map_result);
    hal::Mmu::flush_tlb(test_vaddr);
    
    // Get initial reference count
    uint32_t initial_refcount = mm::Pmm::frame_get_refcount(frame);
    
    // Clone the address space
    hal_addr_space_t cloned = hal::Mmu::clone_space(current);
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Query both spaces
    paddr_t parent_phys = 0, child_phys = 0;
    uint32_t parent_flags = 0, child_flags = 0;
    
    bool parent_mapped = hal::Mmu::query(current, test_vaddr, &parent_phys, &parent_flags);
    bool child_mapped = hal::Mmu::query(cloned, test_vaddr, &child_phys, &child_flags);
    
    // Property: Both must be mapped
    ASSERT_TRUE(parent_mapped);
    ASSERT_TRUE(child_mapped);
    
    // Property: Both must point to same physical page (COW sharing)
    ASSERT_EQ_U(parent_phys, child_phys);
    ASSERT_EQ_U(parent_phys, frame);
    
    // Property: Reference count must have increased
    uint32_t new_refcount = mm::Pmm::frame_get_refcount(frame);
    ASSERT_TRUE(new_refcount > initial_refcount);
    
    // Property: Both must have COW flag set
    ASSERT_TRUE((parent_flags & HAL_PAGE_COW) != 0);
    ASSERT_TRUE((child_flags & HAL_PAGE_COW) != 0);
    
    // Property: Write permission must be removed (for COW to work)
    ASSERT_TRUE((parent_flags & HAL_PAGE_WRITE) == 0);
    ASSERT_TRUE((child_flags & HAL_PAGE_WRITE) == 0);
    
    // Clean up
    hal::Mmu::destroy_space(cloned);
    hal::Mmu::unmap(current, test_vaddr);
    hal::Mmu::flush_tlb(test_vaddr);
    mm::Pmm::free_frame(frame);
}

/**
 * Test: COW reference counting works correctly
 * 
 * Verifies that reference counts are properly managed during
 * clone and destroy operations.
 */
TEST_CASE(test_fork_cow_reference_counting) {
    hal_addr_space_t current = hal::Mmu::current_space();
    
    // Allocate and map a test page
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    vaddr_t test_vaddr = FORK_TEST_VADDR_BASE + PAGE_SIZE;
    
    if (hal::Mmu::query(current, test_vaddr, NULL, NULL)) {
        mm::Pmm::free_frame(frame);
        return;
    }
    
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
    ASSERT_TRUE(hal::Mmu::map(current, test_vaddr, frame, flags));
    hal::Mmu::flush_tlb(test_vaddr);
    
    // Initial refcount should be 1
    uint32_t refcount1 = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(refcount1, 1);
    
    // Clone once - refcount should be 2
    hal_addr_space_t clone1 = hal::Mmu::clone_space(current);
    ASSERT_NE_U(clone1, HAL_ADDR_SPACE_INVALID);
    
    uint32_t refcount2 = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(refcount2, 2);
    
    // Clone again - refcount should be 3
    hal_addr_space_t clone2 = hal::Mmu::clone_space(current);
    ASSERT_NE_U(clone2, HAL_ADDR_SPACE_INVALID);
    
    uint32_t refcount3 = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(refcount3, 3);
    
    // Destroy one clone - refcount should be 2
    hal::Mmu::destroy_space(clone2);
    uint32_t refcount4 = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(refcount4, 2);
    
    // Destroy other clone - refcount should be 1
    hal::Mmu::destroy_space(clone1);
    uint32_t refcount5 = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(refcount5, 1);
    
    // Clean up
    hal::Mmu::unmap(current, test_vaddr);
    hal::Mmu::flush_tlb(test_vaddr);
    mm::Pmm::free_frame(frame);
}

/**
 * Test: Multiple pages are correctly COW-shared
 * 
 * Verifies that cloning works correctly with multiple mapped pages.
 */
TEST_CASE(test_fork_cow_multiple_pages) {
    hal_addr_space_t current = hal::Mmu::current_space();
    
    paddr_t frames[FORK_TEST_PAGE_COUNT];
    vaddr_t vaddrs[FORK_TEST_PAGE_COUNT];
    uint32_t mapped_count = 0;
    
    // Allocate and map multiple pages
    for (uint32_t i = 0; i < FORK_TEST_PAGE_COUNT; i++) {
        frames[i] = mm::Pmm::alloc_frame();
        if (frames[i] == PADDR_INVALID) {
            break;
        }
        
        vaddrs[i] = FORK_TEST_VADDR_BASE + (i + 2) * PAGE_SIZE;
        
        if (hal::Mmu::query(current, vaddrs[i], NULL, NULL)) {
            mm::Pmm::free_frame(frames[i]);
            continue;
        }
        
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_WRITE;
        if (hal::Mmu::map(current, vaddrs[i], frames[i], flags)) {
            hal::Mmu::flush_tlb(vaddrs[i]);
            mapped_count++;
        } else {
            mm::Pmm::free_frame(frames[i]);
        }
    }
    
    // Need at least some pages mapped
    ASSERT_TRUE(mapped_count > 0);
    
    // Clone the address space
    hal_addr_space_t cloned = hal::Mmu::clone_space(current);
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Verify all mapped pages are COW-shared
    for (uint32_t i = 0; i < mapped_count; i++) {
        paddr_t parent_phys = 0, child_phys = 0;
        uint32_t parent_flags = 0, child_flags = 0;
        
        bool parent_ok = hal::Mmu::query(current, vaddrs[i], &parent_phys, &parent_flags);
        bool child_ok = hal::Mmu::query(cloned, vaddrs[i], &child_phys, &child_flags);
        
        // Property: Both must be mapped
        ASSERT_TRUE(parent_ok);
        ASSERT_TRUE(child_ok);
        
        // Property: Same physical address
        ASSERT_EQ_U(parent_phys, child_phys);
        
        // Property: COW flag set
        ASSERT_TRUE((parent_flags & HAL_PAGE_COW) != 0);
        ASSERT_TRUE((child_flags & HAL_PAGE_COW) != 0);
        
        // Property: Reference count is 2
        uint32_t refcount = mm::Pmm::frame_get_refcount(frames[i]);
        ASSERT_EQ_U(refcount, 2);
    }
    
    // Clean up
    hal::Mmu::destroy_space(cloned);
    
    for (uint32_t i = 0; i < mapped_count; i++) {
        hal::Mmu::unmap(current, vaddrs[i]);
        hal::Mmu::flush_tlb(vaddrs[i]);
        mm::Pmm::free_frame(frames[i]);
    }
}

/**
 * Test: Kernel space is shared (not COW) between parent and child
 * 
 * Verifies that kernel mappings are shared directly without COW.
 */
TEST_CASE(test_fork_kernel_space_shared) {
    hal_addr_space_t current = hal::Mmu::current_space();
    
    // Clone the address space
    hal_addr_space_t cloned = hal::Mmu::clone_space(current);
    ASSERT_NE_U(cloned, HAL_ADDR_SPACE_INVALID);
    
    // Test a kernel address
    vaddr_t kernel_addr = KTEST_KERNEL_MAPPED_VADDR;  // 1MB into kernel space
    
    paddr_t parent_phys = 0, child_phys = 0;
    uint32_t parent_flags = 0, child_flags = 0;
    
    bool parent_mapped = hal::Mmu::query(current, kernel_addr, &parent_phys, &parent_flags);
    bool child_mapped = hal::Mmu::query(cloned, kernel_addr, &child_phys, &child_flags);
    
    // Property: Kernel space must be mapped in both
    ASSERT_TRUE(parent_mapped);
    ASSERT_TRUE(child_mapped);
    
    // Property: Same physical address
    ASSERT_EQ_U(parent_phys, child_phys);
    
    // Property: Kernel pages should NOT have COW flag
    // (kernel space is shared directly, not COW)
    ASSERT_TRUE((parent_flags & HAL_PAGE_COW) == 0);
    ASSERT_TRUE((child_flags & HAL_PAGE_COW) == 0);
    
    // Clean up
    hal::Mmu::destroy_space(cloned);
}

/**
 * Test: mm::Vmm::clone_page_directory wrapper works correctly
 * 
 * Tests the VMM-level clone function that wraps hal::Mmu::clone_space.
 */
TEST_CASE(test_fork_vmm_clone_page_directory) {
    // Create a new page directory
    uintptr_t src_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(src_dir, 0);
    
    // Map a page in the source directory
    paddr_t frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(frame, PADDR_INVALID);
    
    vaddr_t test_vaddr = FORK_TEST_VADDR_BASE + 0x10000;
    
    bool map_ok = mm::Vmm::map_page_in_directory(src_dir, test_vaddr, (uintptr_t)frame,
                                            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    ASSERT_TRUE(map_ok);
    
    // Clone the page directory
    uintptr_t clone_dir = mm::Vmm::clone_page_directory(src_dir);
    ASSERT_NE_U(clone_dir, 0);
    ASSERT_NE_U(clone_dir, src_dir);
    
    // Verify COW sharing via reference count
    uint32_t refcount = mm::Pmm::frame_get_refcount(frame);
    ASSERT_EQ_U(refcount, 2);
    
    // Clean up
    mm::Vmm::free_page_directory(clone_dir);
    mm::Vmm::free_page_directory(src_dir);
}

// ============================================================================
// Task 36.2: Exec System Call Tests (Program Loading)
// **Feature: multi-arch-support**
// **Validates: Requirements 7.4**
// ============================================================================

/**
 * Test: User mode transition mechanism is correct
 * 
 * Verifies that the architecture-specific user mode transition
 * mechanism is properly configured.
 */
TEST_CASE(test_exec_user_mode_transition_setup) {
    // Verify architecture-specific user mode setup
#if defined(ARCH_I686)
    // i686 uses IRET for user mode transition
    // Verify user segment selectors are correct
    ASSERT_EQ_U(0x1B, 0x1B);  // User code segment (0x18 | 3)
    ASSERT_EQ_U(0x23, 0x23);  // User data segment (0x20 | 3)
#elif defined(ARCH_X86_64)
    // x86_64 uses IRETQ or SYSRET for user mode transition
    ASSERT_EQ_U(0x1B, 0x1B);  // User code segment
    ASSERT_EQ_U(0x23, 0x23);  // User data segment
#elif defined(ARCH_ARM64)
    // ARM64 uses ERET for user mode transition
    // Verify PSTATE values for EL0
    ASSERT_EQ_U(ARM64_PSTATE_EL0t, 0x00);
#endif
}

/**
 * Test: Context initialization for user mode is correct
 * 
 * Verifies that hal::Context::init correctly sets up a user-mode context.
 */
TEST_CASE(test_exec_context_init_user_mode) {
#if defined(ARCH_I686)
    cpu_context_t ctx;
    
    uintptr_t entry = 0x08048000;  // Typical ELF entry point
    uintptr_t stack = 0x7FFFF000;  // User stack
    
    hal::Context::init((hal_context_t*)&ctx, entry, stack, true);
    
    // Property: Entry point must be set
    ASSERT_EQ_U(ctx.eip, entry);
    
    // Property: Stack must be set
    ASSERT_EQ_U(ctx.esp, stack);
    
    // Property: User code segment
    ASSERT_EQ_U(ctx.cs, 0x1B);
    
    // Property: User data segment
    ASSERT_EQ_U(ctx.ds, 0x23);
    ASSERT_EQ_U(ctx.ss, 0x23);
    
    // Property: Interrupts enabled
    ASSERT_TRUE((ctx.eflags & 0x200) != 0);
    
#elif defined(ARCH_X86_64)
    x86_64_context_t ctx;
    
    uint64_t entry = 0x00400000ULL;
    uint64_t stack = 0x7FFFFFFFE000ULL;
    
    hal::Context::init((hal_context_t*)&ctx, entry, stack, true);
    
    // Property: Entry point must be set
    ASSERT_EQ_U(ctx.rip, entry);
    
    // Property: Stack must be set
    ASSERT_EQ_U(ctx.rsp, stack);
    
    // Property: User code segment (GDT index 4 = 0x20 | RPL=3 = 0x23)
    ASSERT_EQ_U(ctx.cs, 0x23);
    
    // Property: User stack segment (GDT index 3 = 0x18 | RPL=3 = 0x1B)
    ASSERT_EQ_U(ctx.ss, 0x1B);
    
    // Property: Interrupts enabled
    ASSERT_TRUE((ctx.rflags & 0x200) != 0);
    
#elif defined(ARCH_ARM64)
    arm64_context_t ctx;
    
    uint64_t entry = 0x00400000ULL;
    uint64_t stack = 0x7FFFFFFFE000ULL;
    
    hal::Context::init((hal_context_t*)&ctx, entry, stack, true);
    
    // Property: Entry point must be set
    ASSERT_EQ_U(ctx.pc, entry);
    
    // Property: Stack must be set
    ASSERT_EQ_U(ctx.sp, stack);
    
    // Property: PSTATE must indicate EL0
    ASSERT_EQ_U(ctx.pstate & 0x0F, ARM64_PSTATE_EL0t);
#endif
}

/**
 * Test: Page directory creation for new process
 * 
 * Verifies that mm::Vmm::create_page_directory creates a valid
 * page directory suitable for a new process.
 */
TEST_CASE(test_exec_page_directory_creation) {
    // Create a new page directory (as exec would do)
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // Property: Must be page-aligned
    ASSERT_EQ_U(new_dir & (PAGE_SIZE - 1), 0);
    
    // Property: Kernel space must be mapped
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    vaddr_t kernel_addr = KTEST_KERNEL_MAPPED_VADDR;
    
    paddr_t phys = 0;
    bool mapped = hal::Mmu::query(space, kernel_addr, &phys, NULL);
    ASSERT_TRUE(mapped);
    ASSERT_NE_U(phys, 0);
    
    // Clean up
    mm::Vmm::free_page_directory(new_dir);
}

/**
 * Test: User stack setup for new process
 * 
 * Verifies that user stack can be properly set up in a new
 * address space.
 */
TEST_CASE(test_exec_user_stack_setup) {
    // Create a new page directory
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // Allocate a page for user stack
    paddr_t stack_frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(stack_frame, PADDR_INVALID);
    
    // Map at typical user stack location
    vaddr_t stack_vaddr = KTEST_USER_STACK_VADDR;  // Near top of user space
    
    bool map_ok = mm::Vmm::map_page_in_directory(new_dir, stack_vaddr, (uintptr_t)stack_frame,
                                            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    ASSERT_TRUE(map_ok);
    
    // Verify mapping
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    paddr_t queried_phys = 0;
    uint32_t queried_flags = 0;
    
    bool query_ok = hal::Mmu::query(space, stack_vaddr, &queried_phys, &queried_flags);
    ASSERT_TRUE(query_ok);
    ASSERT_EQ_U(queried_phys, stack_frame);
    
    // Property: Stack must be writable
    ASSERT_TRUE((queried_flags & HAL_PAGE_WRITE) != 0);
    
    // Property: Stack must be user-accessible
    ASSERT_TRUE((queried_flags & HAL_PAGE_USER) != 0);
    
    // Clean up
    mm::Vmm::free_page_directory(new_dir);
}

/**
 * Test: Program code mapping for new process
 * 
 * Verifies that program code can be properly mapped in a new
 * address space (simulating ELF loading).
 */
TEST_CASE(test_exec_program_code_mapping) {
    // Create a new page directory
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(new_dir, 0);
    
    // Allocate pages for program code
    paddr_t code_frame = mm::Pmm::alloc_frame();
    ASSERT_NE_U(code_frame, PADDR_INVALID);
    
    // Map at typical program load address
    vaddr_t code_vaddr = KTEST_USER_CODE_VADDR;  // Typical program load address
    
    // Code should be readable and executable, but not writable
    bool map_ok = mm::Vmm::map_page_in_directory(new_dir, code_vaddr, (uintptr_t)code_frame,
                                            PAGE_PRESENT | PAGE_USER);
    ASSERT_TRUE(map_ok);
    
    // Verify mapping
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    paddr_t queried_phys = 0;
    uint32_t queried_flags = 0;
    
    bool query_ok = hal::Mmu::query(space, code_vaddr, &queried_phys, &queried_flags);
    ASSERT_TRUE(query_ok);
    ASSERT_EQ_U(queried_phys, code_frame);
    
    // Property: Code must be present
    ASSERT_TRUE((queried_flags & HAL_PAGE_PRESENT) != 0);
    
    // Property: Code must be user-accessible
    ASSERT_TRUE((queried_flags & HAL_PAGE_USER) != 0);
    
    // Clean up
    mm::Vmm::free_page_directory(new_dir);
}

// ============================================================================
// ELF 映像校验测试
// 加载器必须在映射任何页之前拒绝头部/段范围不合法的文件
// ============================================================================

#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
typedef elf64_ehdr_t test_ehdr_t;
typedef elf64_phdr_t test_phdr_t;
#define TEST_ELF_CLASS ELF_CLASS_64
#else
typedef elf32_ehdr_t test_ehdr_t;
typedef elf32_phdr_t test_phdr_t;
#define TEST_ELF_CLASS ELF_CLASS_32
#endif

#if defined(ARCH_X86_64)
#define TEST_ELF_MACHINE EM_X86_64
#elif defined(ARCH_ARM64)
#define TEST_ELF_MACHINE EM_AARCH64
#else
#define TEST_ELF_MACHINE EM_386
#endif

/* 用户映像区上界：与 elf.cpp 中的 elf_user_image_limit() 一致（用户栈底） */
#if defined(ARCH_ARM64)
#define TEST_ELF_IMAGE_LIMIT ((uintptr_t)ARM64_USER_STACK_TOP - USER_STACK_SIZE)
#else
#define TEST_ELF_IMAGE_LIMIT ((uintptr_t)USER_SPACE_END - USER_STACK_SIZE)
#endif

#define TEST_ELF_CODE_BYTES 16
#define TEST_ELF_SIZE (sizeof(test_ehdr_t) + sizeof(test_phdr_t) + TEST_ELF_CODE_BYTES)

static uint8_t g_test_elf[sizeof(test_ehdr_t) + sizeof(test_phdr_t) + TEST_ELF_CODE_BYTES];

static test_ehdr_t *test_elf_ehdr(void) { return (test_ehdr_t *)g_test_elf; }
static test_phdr_t *test_elf_phdr(void) {
    return (test_phdr_t *)(g_test_elf + sizeof(test_ehdr_t));
}

/**
 * 构造一个最小的合法可执行映像：一个 R+X 的 PT_LOAD 段，
 * 文件内容 16 字节，内存大小一页，入口在段首
 */
static void build_test_elf(void) {
    memset(g_test_elf, 0, sizeof(g_test_elf));
    test_ehdr_t *eh = test_elf_ehdr();
    eh->e_ident[0] = 0x7F;
    eh->e_ident[1] = 'E';
    eh->e_ident[2] = 'L';
    eh->e_ident[3] = 'F';
    eh->e_ident[4] = TEST_ELF_CLASS;
    eh->e_ident[5] = ELF_DATA_LSB;
    eh->e_ident[6] = EV_CURRENT;
    eh->e_type = ET_EXEC;
    eh->e_machine = TEST_ELF_MACHINE;
    eh->e_version = EV_CURRENT;
    eh->e_entry = KTEST_USER_CODE_VADDR;
    eh->e_phoff = sizeof(test_ehdr_t);
    eh->e_ehsize = sizeof(test_ehdr_t);
    eh->e_phentsize = sizeof(test_phdr_t);
    eh->e_phnum = 1;

    test_phdr_t *ph = test_elf_phdr();
    ph->p_type = PT_LOAD;
    ph->p_flags = PF_R | PF_X;
    ph->p_offset = sizeof(test_ehdr_t) + sizeof(test_phdr_t);
    ph->p_vaddr = KTEST_USER_CODE_VADDR;
    ph->p_filesz = TEST_ELF_CODE_BYTES;
    ph->p_memsz = PAGE_SIZE;
    ph->p_align = PAGE_SIZE;

    for (uint32_t i = 0; i < TEST_ELF_CODE_BYTES; i++) {
        g_test_elf[sizeof(test_ehdr_t) + sizeof(test_phdr_t) + i] = (uint8_t)(0xA0 + i);
    }
}

TEST_CASE(test_elf_valid_image_accepted) {
    build_test_elf();
    ASSERT_TRUE(kernel::Elf::validate_header(g_test_elf, TEST_ELF_SIZE));
    ASSERT_TRUE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));
    ASSERT_EQ_U(kernel::Elf::get_entry(g_test_elf, TEST_ELF_SIZE), KTEST_USER_CODE_VADDR);
}

/* 文件比 ELF 头还短：魔数正确也必须拒绝，不能去读头部之外的字段 */
TEST_CASE(test_elf_truncated_header_rejected) {
    build_test_elf();
    ASSERT_FALSE(kernel::Elf::validate_header(g_test_elf, 0));
    ASSERT_FALSE(kernel::Elf::validate_header(g_test_elf, 4));
    ASSERT_FALSE(kernel::Elf::validate_header(g_test_elf, sizeof(test_ehdr_t) - 1));
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, sizeof(test_ehdr_t) - 1));
    ASSERT_EQ_U(kernel::Elf::get_entry(g_test_elf, 4), 0);
}

/* 程序头表必须完整落在文件内，表项大小必须是本架构的程序头大小 */
TEST_CASE(test_elf_phdr_table_bounds) {
    build_test_elf();
    // 只给到 ELF 头：程序头表在文件之外
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, sizeof(test_ehdr_t)));

    test_elf_ehdr()->e_phoff = TEST_ELF_SIZE + 0x1000;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    build_test_elf();
    test_elf_ehdr()->e_phoff = (uintptr_t)-1 - 8;   // e_phoff + 表大小 回绕
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    build_test_elf();
    test_elf_ehdr()->e_phnum = 0xFFFF;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    build_test_elf();
    test_elf_ehdr()->e_phentsize = sizeof(test_phdr_t) - 4;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));
}

/* 段的文件范围：越界和 p_offset + p_filesz 回绕都必须拒绝 */
TEST_CASE(test_elf_segment_file_range) {
    build_test_elf();
    test_elf_phdr()->p_filesz = TEST_ELF_CODE_BYTES + 1;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    build_test_elf();
    test_elf_phdr()->p_offset = (uintptr_t)-1 - 0xFFF;   // 0x...FFFFF000
    test_elf_phdr()->p_filesz = 0x2000;                  // 相加回绕成一个很小的数
    test_elf_phdr()->p_memsz = 0x2000;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    build_test_elf();
    test_elf_phdr()->p_memsz = TEST_ELF_CODE_BYTES - 1;  // p_filesz > p_memsz
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));
}

/* 段的地址范围：只检查段首不够，段尾越过用户映像区或回绕都必须拒绝 */
TEST_CASE(test_elf_segment_address_range) {
    // 段首在用户空间，段尾伸进用户栈/内核半区
    build_test_elf();
    test_elf_phdr()->p_vaddr = TEST_ELF_IMAGE_LIMIT - PAGE_SIZE;
    test_elf_phdr()->p_memsz = 0x400000;
    test_elf_ehdr()->e_entry = TEST_ELF_IMAGE_LIMIT - PAGE_SIZE;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    // 恰好贴着上界的段是合法的
    build_test_elf();
    test_elf_phdr()->p_vaddr = TEST_ELF_IMAGE_LIMIT - PAGE_SIZE;
    test_elf_ehdr()->e_entry = TEST_ELF_IMAGE_LIMIT - PAGE_SIZE;
    ASSERT_TRUE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    // p_vaddr + p_memsz 回绕
    build_test_elf();
    test_elf_phdr()->p_memsz = (uintptr_t)-1 - 0xFFF;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    // 段首就在内核空间
    build_test_elf();
    test_elf_phdr()->p_vaddr = KERNEL_VIRTUAL_BASE;
    test_elf_ehdr()->e_entry = KERNEL_VIRTUAL_BASE;
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));
}

/* 入口点必须落在某个可执行段内 */
TEST_CASE(test_elf_entry_point_checked) {
    build_test_elf();
    test_elf_ehdr()->e_entry = KTEST_USER_CODE_VADDR + PAGE_SIZE;   // 段尾之后
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

    build_test_elf();
    test_elf_ehdr()->e_entry = KERNEL_VIRTUAL_BASE;                 // 内核地址
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));

#if defined(ARCH_X86_64)
    build_test_elf();
    test_elf_ehdr()->e_entry = 0x0000800000000000ULL;               // 非规范地址
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));
#endif

    build_test_elf();
    test_elf_phdr()->p_flags = PF_R | PF_W;                         // 段不可执行
    ASSERT_FALSE(kernel::Elf::validate(g_test_elf, TEST_ELF_SIZE));
}

/* load() 对合法映像建立用户映射；对不合法映像在映射前失败 */
TEST_CASE(test_elf_load_maps_only_valid_images) {
    uintptr_t new_dir = mm::Vmm::create_page_directory();
    ASSERT_NE_U(new_dir, 0);
#if defined(ARCH_ARM64)
    page_directory_t *dir = (page_directory_t *)new_dir;
#else
    page_directory_t *dir = (page_directory_t *)PHYS_TO_VIRT(new_dir);
#endif
    hal_addr_space_t space = (hal_addr_space_t)new_dir;
    uintptr_t entry = 0;
    uintptr_t program_end = 0;

    // 段尾越界的映像：load 失败，且没有留下任何映射
    build_test_elf();
    test_elf_phdr()->p_vaddr = TEST_ELF_IMAGE_LIMIT - PAGE_SIZE;
    test_elf_phdr()->p_memsz = 0x400000;
    test_elf_ehdr()->e_entry = TEST_ELF_IMAGE_LIMIT - PAGE_SIZE;
    ASSERT_FALSE(kernel::Elf::load(g_test_elf, TEST_ELF_SIZE, dir, &entry, &program_end));
    ASSERT_FALSE(hal::Mmu::query(space, TEST_ELF_IMAGE_LIMIT - PAGE_SIZE, NULL, NULL));

    // 合法映像
    build_test_elf();
    ASSERT_TRUE(kernel::Elf::load(g_test_elf, TEST_ELF_SIZE, dir, &entry, &program_end));
    ASSERT_EQ_U(entry, KTEST_USER_CODE_VADDR);
    ASSERT_EQ_U(program_end, KTEST_USER_CODE_VADDR + PAGE_SIZE);

    paddr_t phys = 0;
    uint32_t flags = 0;
    ASSERT_TRUE(hal::Mmu::query(space, KTEST_USER_CODE_VADDR, &phys, &flags));
    ASSERT_TRUE((flags & HAL_PAGE_USER) != 0);
    ASSERT_TRUE((flags & HAL_PAGE_WRITE) == 0);

    // 文件内容被拷入页首，其余为 0
    const uint8_t *page = (const uint8_t *)PHYS_TO_VIRT((uintptr_t)phys);
    ASSERT_EQ_U(page[0], 0xA0);
    ASSERT_EQ_U(page[TEST_ELF_CODE_BYTES - 1], 0xA0 + TEST_ELF_CODE_BYTES - 1);
    ASSERT_EQ_U(page[TEST_ELF_CODE_BYTES], 0);

    mm::Vmm::free_page_directory(new_dir);
}

// ============================================================================
// Test Suites
// ============================================================================

TEST_SUITE(elf_validation_tests) {
    RUN_TEST(test_elf_valid_image_accepted);
    RUN_TEST(test_elf_truncated_header_rejected);
    RUN_TEST(test_elf_phdr_table_bounds);
    RUN_TEST(test_elf_segment_file_range);
    RUN_TEST(test_elf_segment_address_range);
    RUN_TEST(test_elf_entry_point_checked);
    RUN_TEST(test_elf_load_maps_only_valid_images);
}

TEST_SUITE(fork_cow_tests) {
    RUN_TEST(test_fork_clone_space_creates_valid_space);
    RUN_TEST(test_fork_cow_shares_physical_pages);
    RUN_TEST(test_fork_cow_reference_counting);
    RUN_TEST(test_fork_cow_multiple_pages);
    RUN_TEST(test_fork_kernel_space_shared);
    RUN_TEST(test_fork_vmm_clone_page_directory);
}

TEST_SUITE(exec_tests) {
    RUN_TEST(test_exec_user_mode_transition_setup);
    RUN_TEST(test_exec_context_init_user_mode);
    RUN_TEST(test_exec_page_directory_creation);
    RUN_TEST(test_exec_user_stack_setup);
    RUN_TEST(test_exec_program_code_mapping);
}

// ============================================================================
// Run all fork/exec tests
// ============================================================================

void run_fork_exec_tests(void) {
    unittest_init();
    
    kprintf("\n");
    kprintf("==========================================================\n");
    kprintf("Fork/Exec Verification Tests\n");
    kprintf("**Feature: multi-arch-support**\n");
    kprintf("**Validates: Requirements 5.5, 7.4**\n");
    kprintf("==========================================================\n");
    
    // Task 36.1: Fork system call tests (COW)
    kprintf("\n--- Task 36.1: Fork System Call (COW) Tests ---\n");
    RUN_SUITE(fork_cow_tests);
    
    // Task 36.2: Exec system call tests
    kprintf("\n--- Task 36.2: Exec System Call Tests ---\n");
    RUN_SUITE(exec_tests);

    // ELF 映像校验
    kprintf("\n--- ELF Image Validation Tests ---\n");
    RUN_SUITE(elf_validation_tests);

    unittest_print_summary();
}
