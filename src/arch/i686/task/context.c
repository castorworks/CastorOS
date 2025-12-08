/**
 * @file context.c
 * @brief i686 Architecture-Specific Context Switch Implementation
 * 
 * Implements the HAL context switch interface for i686 architecture.
 * 
 * Requirements: 7.1, 12.1
 */

#include <hal/hal.h>
#include <context.h>
#include <kernel/gdt.h>
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
    return sizeof(i686_context_t);
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
    memset(ctx, 0, sizeof(i686_context_t));
    
    /* Cast to architecture-specific type */
    i686_context_t *i686_ctx = (i686_context_t *)ctx;
    
    if (is_user) {
        /* User mode context */
        i686_ctx->cs = I686_USER_CS;        /* 0x1B - User code segment with RPL=3 */
        i686_ctx->ds = I686_USER_DS;        /* 0x23 - User data segment with RPL=3 */
        i686_ctx->es = I686_USER_DS;
        i686_ctx->fs = I686_USER_DS;
        i686_ctx->gs = I686_USER_DS;
        i686_ctx->ss = I686_USER_DS;
        
        /* User entry point */
        i686_ctx->eip = (uint32_t)entry;
        i686_ctx->esp = (uint32_t)stack;
    } else {
        /* Kernel mode context */
        i686_ctx->cs = I686_KERNEL_CS;      /* 0x08 - Kernel code segment */
        i686_ctx->ds = I686_KERNEL_DS;      /* 0x10 - Kernel data segment */
        i686_ctx->es = I686_KERNEL_DS;
        i686_ctx->fs = I686_KERNEL_DS;
        i686_ctx->gs = I686_KERNEL_DS;
        i686_ctx->ss = I686_KERNEL_DS;
        
        /* For kernel threads, we use hal_context_enter_kernel_thread as entry */
        /* The actual entry function is pushed on the stack */
        i686_ctx->eip = (uint32_t)hal_context_enter_kernel_thread;
        
        /* Push the actual entry function on the stack */
        uint32_t *stack_ptr = (uint32_t *)stack;
        stack_ptr[-1] = (uint32_t)entry;    /* Entry function address */
        i686_ctx->esp = (uint32_t)&stack_ptr[-1];
    }
    
    /* Enable interrupts in EFLAGS */
    i686_ctx->eflags = I686_EFLAGS_DEFAULT; /* 0x202 - IF=1 */
    
    /* CR3 will be set by the caller (page directory) */
    i686_ctx->cr3 = 0;
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
    tss_set_kernel_stack((uint32_t)stack_top);
}

/**
 * @brief Get architecture name string
 * @return Architecture name
 */
const char *hal_arch_name(void) {
    return "i686";
}
