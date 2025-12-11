/**
 * @file hal_syscall.c
 * @brief i686 HAL System Call Parameter Implementation
 * 
 * This file implements the i686-specific system call parameter extraction
 * and return value setting as part of the HAL.
 *
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 7.1, 7.3**
 *
 * i686 System Call ABI:
 *   - EAX = system call number
 *   - EBX = arg1
 *   - ECX = arg2
 *   - EDX = arg3
 *   - ESI = arg4
 *   - EDI = arg5
 *   - EBP = arg6 (for syscalls like mmap that need 6 args)
 *   - Return value in EAX
 */

#include <context.h>
#include <hal/hal_syscall.h>

/**
 * @brief Extract system call arguments from i686 CPU context
 * 
 * On i686, system call arguments are passed in registers:
 *   - EAX: syscall number
 *   - EBX, ECX, EDX, ESI, EDI, EBP: args 0-5
 * 
 * @param ctx CPU context from which to extract arguments
 * @param[out] args Pointer to structure to fill with arguments
 */
void hal_syscall_get_args(hal_context_t *ctx, hal_syscall_args_t *args) {
    i686_context_t *arch_ctx = (i686_context_t *)ctx;
    
    if (!arch_ctx || !args) {
        return;
    }
    
    /* Extract syscall number from EAX */
    args->syscall_nr = (uint64_t)arch_ctx->eax;
    
    /* Extract arguments from registers */
    args->args[0] = (uint64_t)arch_ctx->ebx;  /* arg1 */
    args->args[1] = (uint64_t)arch_ctx->ecx;  /* arg2 */
    args->args[2] = (uint64_t)arch_ctx->edx;  /* arg3 */
    args->args[3] = (uint64_t)arch_ctx->esi;  /* arg4 */
    args->args[4] = (uint64_t)arch_ctx->edi;  /* arg5 */
    args->args[5] = (uint64_t)arch_ctx->ebp;  /* arg6 */
    
    /* No extra args pointer on i686 - all 6 args fit in registers */
    args->extra_args = NULL;
}

/**
 * @brief Set system call return value in i686 CPU context
 * 
 * On i686, the return value is placed in EAX.
 * 
 * @param ctx CPU context to modify
 * @param ret Return value to set
 */
void hal_syscall_set_return(hal_context_t *ctx, int64_t ret) {
    i686_context_t *arch_ctx = (i686_context_t *)ctx;
    
    if (!arch_ctx) {
        return;
    }
    
    /* Set return value in EAX (truncated to 32-bit on i686) */
    arch_ctx->eax = (uint32_t)(int32_t)ret;
}

/**
 * @brief Set system call error code in i686 CPU context
 * 
 * On i686, error codes are returned as negative values in EAX.
 * 
 * @param ctx CPU context to modify
 * @param errno Error code to set (positive value, will be negated)
 */
void hal_syscall_set_errno(hal_context_t *ctx, int32_t errno) {
    i686_context_t *arch_ctx = (i686_context_t *)ctx;
    
    if (!arch_ctx) {
        return;
    }
    
    /* Set negative error code in EAX */
    arch_ctx->eax = (uint32_t)(-errno);
}

/**
 * @brief Get a specific system call argument from i686 context
 * 
 * @param ctx CPU context
 * @param index Argument index (0-5)
 * @return Argument value, or 0 if index is out of range
 */
uint64_t hal_syscall_get_arg(hal_context_t *ctx, uint32_t index) {
    i686_context_t *arch_ctx = (i686_context_t *)ctx;
    
    if (!arch_ctx || index >= HAL_SYSCALL_MAX_ARGS) {
        return 0;
    }
    
    switch (index) {
        case 0: return (uint64_t)arch_ctx->ebx;
        case 1: return (uint64_t)arch_ctx->ecx;
        case 2: return (uint64_t)arch_ctx->edx;
        case 3: return (uint64_t)arch_ctx->esi;
        case 4: return (uint64_t)arch_ctx->edi;
        case 5: return (uint64_t)arch_ctx->ebp;
        default: return 0;
    }
}

/**
 * @brief Get system call number from i686 context
 * 
 * @param ctx CPU context
 * @return System call number from EAX
 */
uint64_t hal_syscall_get_number(hal_context_t *ctx) {
    i686_context_t *arch_ctx = (i686_context_t *)ctx;
    
    if (!arch_ctx) {
        return 0;
    }
    
    return (uint64_t)arch_ctx->eax;
}
