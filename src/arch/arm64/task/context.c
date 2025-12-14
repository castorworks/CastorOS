/**
 * @file context.c
 * @brief ARM64 Architecture-Specific Context Switch Implementation
 * 
 * Implements the HAL context switch interface for ARM64 architecture.
 * 
 * Requirements: 7.2, 7.3, 12.1
 * 
 * **Feature: multi-arch-support, Property 9: Context Switch Register Preservation (ARM64)**
 * **Validates: Requirements 7.2**
 */

#include <hal/hal.h>
#include "../include/context.h"
#include <lib/string.h>

/* Forward declaration for serial output */
extern void serial_puts(const char *str);

/* ============================================================================
 * External Assembly Functions
 * ========================================================================== */

/**
 * @brief Assembly implementation of context switch
 * @param old_ctx Pointer to save current context
 * @param new_ctx Pointer to context to switch to
 */
extern void hal_context_switch_asm(hal_context_t **old_ctx, hal_context_t *new_ctx);

/**
 * @brief Entry point for kernel threads
 */
extern void hal_context_enter_kernel_thread(void);

/* ============================================================================
 * HAL Context Interface Implementation
 * ========================================================================== */

/**
 * @brief Get the size of the architecture-specific context structure
 * @return Size in bytes
 */
size_t hal_context_size(void) {
    return sizeof(arm64_context_t);
}

/**
 * @brief Initialize a task context
 * 
 * Sets up the initial context for a new task. The context is configured
 * so that when switched to, the task will begin execution at the specified
 * entry point with the given stack.
 * 
 * For ARM64:
 *   - X0-X7: Argument registers
 *   - X8: Indirect result location register
 *   - X9-X15: Caller-saved temporary registers
 *   - X16-X17: Intra-procedure-call scratch registers
 *   - X18: Platform register (reserved)
 *   - X19-X28: Callee-saved registers
 *   - X29: Frame pointer (FP)
 *   - X30: Link register (LR)
 *   - SP: Stack pointer
 * 
 * @param ctx Pointer to context structure to initialize
 * @param entry Entry point address
 * @param stack Stack pointer (top of stack)
 * @param is_user true if this is a user-mode context
 */
void hal_context_init(hal_context_t *ctx, uintptr_t entry, 
                      uintptr_t stack, bool is_user) {
    if (!ctx) {
        return;
    }
    
    /* Clear the context structure */
    memset(ctx, 0, sizeof(arm64_context_t));
    
    /* Cast to architecture-specific type */
    arm64_context_t *arm64_ctx = (arm64_context_t *)ctx;
    
    if (is_user) {
        /* User mode context (EL0) */
        arm64_ctx->pstate = ARM64_PSTATE_USER_DEFAULT;  /* EL0t */
        
        /* User entry point and stack */
        arm64_ctx->pc = (uint64_t)entry;
        arm64_ctx->sp = (uint64_t)stack;
        
        /* X30 (LR) should be 0 for user tasks - they shouldn't return */
        arm64_ctx->x[30] = 0;
    } else {
        /* Kernel mode context (EL1) */
        arm64_ctx->pstate = ARM64_PSTATE_KERNEL_DEFAULT;  /* EL1h */
        
        /* For kernel threads, we use hal_context_enter_kernel_thread as entry */
        /* The actual entry function address is stored in X19 (callee-saved) */
        arm64_ctx->pc = (uint64_t)hal_context_enter_kernel_thread;
        arm64_ctx->x[19] = (uint64_t)entry;  /* Store actual entry in X19 */
        
        /* Set up stack pointer */
        arm64_ctx->sp = (uint64_t)stack;
        
        /* X30 (LR) points to hal_context_enter_kernel_thread */
        arm64_ctx->x[30] = (uint64_t)hal_context_enter_kernel_thread;
    }
    
    /* TTBR0 will be set by the caller (page table) */
    arm64_ctx->ttbr0 = 0;
}

/**
 * @brief Perform a context switch
 * 
 * Saves the current CPU state to old_ctx (if not NULL) and restores
 * the CPU state from new_ctx. This function may not return to the
 * caller if switching to a different task.
 * 
 * **Feature: multi-arch-support, Property 10: Address Space Switch Correctness (ARM64)**
 * **Validates: Requirements 7.3**
 * 
 * @param old_ctx Pointer to save current context (can be NULL)
 * @param new_ctx Pointer to context to switch to
 */
void hal_context_switch(hal_context_t **old_ctx, hal_context_t *new_ctx) {
    hal_context_switch_asm(old_ctx, new_ctx);
}

/**
 * @brief Set the kernel stack for a task context
 * 
 * On ARM64, this stores the kernel stack pointer in X28 of the context.
 * The context switch code will use this to set SP_EL1 before ERET to user mode.
 * This ensures that when an exception occurs in user mode, the CPU uses
 * the correct kernel stack for that task.
 * 
 * @param ctx Pointer to the task's context
 * @param stack_top Top of the kernel stack
 */
void hal_context_set_kernel_stack_ctx(hal_context_t *ctx, uintptr_t stack_top) {
    if (!ctx) {
        return;
    }
    arm64_context_t *arm64_ctx = (arm64_context_t *)ctx;
    /* Store kernel stack in X28 - context switch will use this to set SP_EL1 */
    arm64_ctx->x[28] = (uint64_t)stack_top;
}

/**
 * @brief Set the kernel stack for the current CPU
 * 
 * On ARM64, this sets up the stack pointer that will be used when
 * taking exceptions from EL0 (user mode) to EL1 (kernel mode).
 * 
 * @param stack_top Top of the kernel stack
 */
void hal_context_set_kernel_stack(uintptr_t stack_top) {
    /* On ARM64, we set SP directly since we're in kernel mode */
    /* This is called when setting up the kernel stack for the current task */
    __asm__ volatile("mov sp, %0" : : "r"(stack_top));
}
