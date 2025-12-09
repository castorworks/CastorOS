/**
 * @file syscall64.c
 * @brief x86_64 System Call HAL Implementation
 * 
 * This file implements the x86_64-specific system call initialization as part
 * of the HAL (Hardware Abstraction Layer).
 *
 * **Feature: multi-arch-support**
 * **Validates: Requirements 7.5, 8.1**
 *
 * On x86_64, system calls are invoked using the SYSCALL instruction. This file
 * sets up the MSRs (Model Specific Registers) required for SYSCALL/SYSRET
 * operation:
 *   - IA32_EFER: Enable System Call Extensions (SCE)
 *   - IA32_STAR: Segment selectors for SYSCALL/SYSRET
 *   - IA32_LSTAR: Long mode SYSCALL target RIP
 *   - IA32_CSTAR: Compatibility mode SYSCALL target RIP
 *   - IA32_FMASK: RFLAGS mask for SYSCALL
 *
 * Additionally, INT 0x80 is supported for compatibility with legacy code.
 */

#include <hal/hal.h>
#include <kernel/syscall.h>
#include <gdt64.h>
#include <idt64.h>
#include <lib/klog.h>

/* External assembly functions */
extern void syscall_entry(void);
extern void syscall_entry_compat(void);
extern void syscall_init_msr(void);
extern void set_kernel_stack(uint64_t stack_ptr);

/* Global syscall handler (set by hal_syscall_init) */
static hal_syscall_handler_t g_syscall_handler = NULL;

/**
 * @brief Initialize x86_64 system call mechanism
 * @param handler The system call dispatcher function
 *
 * This function sets up the SYSCALL/SYSRET mechanism by configuring
 * the required MSRs. It also sets up INT 0x80 for compatibility.
 *
 * Requirements: 7.5, 8.1 - System call entry mechanism
 */
void hal_syscall_init(hal_syscall_handler_t handler) {
    LOG_INFO_MSG("Initializing x86_64 system call mechanism (SYSCALL/SYSRET)...\n");
    
    /* Store the handler for potential future use */
    g_syscall_handler = handler;
    
    /* Initialize MSRs for SYSCALL/SYSRET */
    syscall_init_msr();
    
    LOG_DEBUG_MSG("  SYSCALL MSRs configured\n");
    LOG_DEBUG_MSG("  LSTAR = syscall_entry\n");
    LOG_DEBUG_MSG("  CSTAR = syscall_entry_compat\n");
    
    /* Also register INT 0x80 handler for compatibility
     * Flags: Present | Ring 3 | Trap Gate
     * Using trap gate so interrupts remain enabled during system call handling
     */
    idt64_set_gate(0x80, 
                   (uint64_t)syscall_entry_compat, 
                   GDT64_KERNEL_CODE_SEGMENT,
                   IDT64_IST_NONE,
                   IDT64_ATTR_PRESENT | IDT64_ATTR_DPL_RING3 | IDT64_TYPE_TRAP);
    
    LOG_DEBUG_MSG("  INT 0x80 handler registered for compatibility\n");
    
    LOG_INFO_MSG("x86_64 system call mechanism initialized\n");
}

/**
 * @brief Get the registered syscall handler
 * @return The currently registered system call handler function
 *
 * This can be used by the assembly entry point if needed.
 */
hal_syscall_handler_t hal_get_syscall_handler(void) {
    return g_syscall_handler;
}

/**
 * @brief Set the kernel stack for syscall entry
 * @param stack_ptr Kernel stack pointer
 *
 * This function sets the kernel stack that will be used when entering
 * the kernel via SYSCALL. It should be called during task switch to
 * update the kernel stack for the current task.
 */
void hal_syscall_set_kernel_stack(uint64_t stack_ptr) {
    set_kernel_stack(stack_ptr);
}
