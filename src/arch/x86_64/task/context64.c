/**
 * @file context64.c
 * @brief x86_64 Architecture-Specific Context Switch Implementation
 * 
 * Implements the HAL context switch interface for x86_64 architecture.
 * 
 * Requirements: 7.1, 7.3, 12.1
 */

#include <hal/hal.h>
#include <context64.h>
#include <gdt64.h>
#include <lib/string.h>

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
    return sizeof(x86_64_context_t);
}

/**
 * @brief Initialize a task context
 * 
 * Sets up the initial context for a new task. The context is configured
 * so that when switched to, the task will begin execution at the specified
 * entry point with the given stack.
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
    memset(ctx, 0, sizeof(x86_64_context_t));
    
    /* Cast to architecture-specific type */
    x86_64_context_t *x64_ctx = (x86_64_context_t *)ctx;
    
    if (is_user) {
        /* User mode context */
        x64_ctx->cs = X86_64_USER_CS;        /* 0x23 - User code segment with RPL=3 */
        x64_ctx->ss = X86_64_USER_DS;        /* 0x1B - User data segment with RPL=3 */
        
        /* User entry point */
        x64_ctx->rip = (uint64_t)entry;
        x64_ctx->rsp = (uint64_t)stack;
    } else {
        /* Kernel mode context */
        x64_ctx->cs = X86_64_KERNEL_CS;      /* 0x08 - Kernel code segment */
        x64_ctx->ss = X86_64_KERNEL_DS;      /* 0x10 - Kernel data segment */
        
        /* For kernel threads, we use hal_context_enter_kernel_thread as entry */
        /* The actual entry function is pushed on the stack */
        x64_ctx->rip = (uint64_t)hal_context_enter_kernel_thread;
        
        /* Push the actual entry function on the stack */
        uint64_t *stack_ptr = (uint64_t *)stack;
        stack_ptr[-1] = (uint64_t)entry;    /* Entry function address */
        x64_ctx->rsp = (uint64_t)&stack_ptr[-1];
    }
    
    /* Enable interrupts in RFLAGS */
    x64_ctx->rflags = X86_64_RFLAGS_DEFAULT; /* 0x202 - IF=1 */
    
    /* CR3 will be set by the caller (page table) */
    x64_ctx->cr3 = 0;
}

/**
 * @brief Perform a context switch
 * 
 * Saves the current CPU state to old_ctx (if not NULL) and restores
 * the CPU state from new_ctx. This function may not return to the
 * caller if switching to a different task.
 * 
 * @param old_ctx Pointer to save current context (can be NULL)
 * @param new_ctx Pointer to context to switch to
 */
void hal_context_switch(hal_context_t **old_ctx, hal_context_t *new_ctx) {
    hal_context_switch_asm(old_ctx, new_ctx);
}

/**
 * @brief Set the kernel stack for the current CPU
 * 
 * Updates the TSS to use the specified kernel stack for privilege
 * level transitions (e.g., when handling interrupts from user mode).
 * 
 * @param stack_top Top of the kernel stack
 */
void hal_context_set_kernel_stack(uintptr_t stack_top) {
    tss64_set_kernel_stack((uint64_t)stack_top);
}

/**
 * @brief Get architecture name string
 * @return Architecture name
 */
const char *hal_arch_name(void) {
    return "x86_64";
}
