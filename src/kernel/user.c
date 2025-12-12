// ============================================================================
// user.c - 用户模式支持
// ============================================================================
//
// This file implements the user mode entry mechanism for all supported
// architectures. It provides a unified interface for transitioning from
// kernel mode to user mode.
//
// **Feature: multi-arch-support**
// **Validates: Requirements 7.4**
// ============================================================================

#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/gdt.h>
#include <mm/vmm.h>
#include <lib/klog.h>

#if defined(ARCH_I686)
/* i686: Use IRET instruction via assembly */
extern void enter_usermode(uint32_t entry_point, uint32_t user_stack);

#elif defined(ARCH_X86_64)
/* x86_64: Use IRETQ instruction via assembly */
extern void enter_usermode64(uint64_t entry_point, uint64_t user_stack);
/* x86_64: Set kernel stack for SYSCALL mechanism */
extern void hal_syscall_set_kernel_stack(uint64_t stack_ptr);

#elif defined(ARCH_ARM64)
/* ARM64: Use ERET instruction via assembly (future implementation) */
extern void enter_usermode_arm64(uint64_t entry_point, uint64_t user_stack);
#endif

/**
 * @brief Enter user mode and start executing user code
 * @param entry_point User code entry point address
 * @param user_stack User stack pointer
 * 
 * This function sets up the kernel stack in the TSS, switches to the
 * task's page directory if necessary, and then transitions to user mode
 * using the architecture-specific mechanism.
 * 
 * Requirements: 7.4 - User mode transition using architecture-specific
 *               privilege transition mechanisms
 */
void task_enter_usermode(uintptr_t entry_point, uintptr_t user_stack)
{
    task_t *current = task_get_current();

    /* Set kernel stack in TSS for privilege level transitions */
    tss_set_kernel_stack(current->kernel_stack);

    /* Switch to task's page directory if different from current */
    if (current->page_dir_phys != vmm_get_page_directory()) {
        vmm_switch_page_directory(current->page_dir_phys);
    }

#if defined(ARCH_I686)
    /* i686: Use IRET to transition to user mode */
    enter_usermode((uint32_t)entry_point, (uint32_t)user_stack);
    
#elif defined(ARCH_X86_64)
    /* x86_64: Set kernel stack for SYSCALL mechanism */
    hal_syscall_set_kernel_stack((uint64_t)current->kernel_stack);
    /* x86_64: Use IRETQ to transition to user mode */
    enter_usermode64((uint64_t)entry_point, (uint64_t)user_stack);
    
#elif defined(ARCH_ARM64)
    /* ARM64: Use ERET to transition to user mode */
    enter_usermode_arm64((uint64_t)entry_point, (uint64_t)user_stack);
#else
    #error "Unsupported architecture for user mode entry"
#endif

    /* Should never reach here */
    for (;;) ;
}
