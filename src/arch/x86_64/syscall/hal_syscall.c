/**
 * @file hal_syscall.c
 * @brief x86_64 HAL System Call Parameter Implementation
 * 
 * This file implements the x86_64-specific system call parameter extraction
 * and return value setting as part of the HAL.
 *
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 7.1, 7.3**
 *
 * x86_64 System Call ABI (System V AMD64):
 *   - RAX = system call number
 *   - RDI = arg1
 *   - RSI = arg2
 *   - RDX = arg3
 *   - R10 = arg4 (RCX is clobbered by SYSCALL instruction)
 *   - R8  = arg5
 *   - R9  = arg6
 *   - Return value in RAX
 */

#include <context64.h>
#include <hal/hal_syscall.h>

/**
 * @brief Extract system call arguments from x86_64 CPU context
 * 
 * On x86_64, system call arguments are passed in registers:
 *   - RAX: syscall number
 *   - RDI, RSI, RDX, R10, R8, R9: args 0-5
 * 
 * Note: R10 is used instead of RCX because the SYSCALL instruction
 * clobbers RCX (it stores the return address there).
 * 
 * @param ctx CPU context from which to extract arguments
 * @param[out] args Pointer to structure to fill with arguments
 */
void hal_syscall_get_args(hal_context_t *ctx, hal_syscall_args_t *args) {
    x86_64_context_t *arch_ctx = (x86_64_context_t *)ctx;
    
    if (!arch_ctx || !args) {
        return;
    }
    
    /* Extract syscall number from RAX */
    args->syscall_nr = arch_ctx->rax;
    
    /* Extract arguments from registers */
    args->args[0] = arch_ctx->rdi;   /* arg1 */
    args->args[1] = arch_ctx->rsi;   /* arg2 */
    args->args[2] = arch_ctx->rdx;   /* arg3 */
    args->args[3] = arch_ctx->r10;   /* arg4 (R10, not RCX) */
    args->args[4] = arch_ctx->r8;    /* arg5 */
    args->args[5] = arch_ctx->r9;    /* arg6 */
    
    /* No extra args pointer - all 6 args fit in registers */
    args->extra_args = NULL;
}

/**
 * @brief Set system call return value in x86_64 CPU context
 * 
 * On x86_64, the return value is placed in RAX.
 * 
 * @param ctx CPU context to modify
 * @param ret Return value to set
 */
void hal_syscall_set_return(hal_context_t *ctx, int64_t ret) {
    x86_64_context_t *arch_ctx = (x86_64_context_t *)ctx;
    
    if (!arch_ctx) {
        return;
    }
    
    /* Set return value in RAX */
    arch_ctx->rax = (uint64_t)ret;
}

/**
 * @brief Set system call error code in x86_64 CPU context
 * 
 * On x86_64, error codes are returned as negative values in RAX.
 * 
 * @param ctx CPU context to modify
 * @param errno Error code to set (positive value, will be negated)
 */
void hal_syscall_set_errno(hal_context_t *ctx, int32_t errno) {
    x86_64_context_t *arch_ctx = (x86_64_context_t *)ctx;
    
    if (!arch_ctx) {
        return;
    }
    
    /* Set negative error code in RAX */
    arch_ctx->rax = (uint64_t)(int64_t)(-errno);
}

/**
 * @brief Get a specific system call argument from x86_64 context
 * 
 * @param ctx CPU context
 * @param index Argument index (0-5)
 * @return Argument value, or 0 if index is out of range
 */
uint64_t hal_syscall_get_arg(hal_context_t *ctx, uint32_t index) {
    x86_64_context_t *arch_ctx = (x86_64_context_t *)ctx;
    
    if (!arch_ctx || index >= HAL_SYSCALL_MAX_ARGS) {
        return 0;
    }
    
    switch (index) {
        case 0: return arch_ctx->rdi;
        case 1: return arch_ctx->rsi;
        case 2: return arch_ctx->rdx;
        case 3: return arch_ctx->r10;
        case 4: return arch_ctx->r8;
        case 5: return arch_ctx->r9;
        default: return 0;
    }
}

/**
 * @brief Get system call number from x86_64 context
 * 
 * @param ctx CPU context
 * @return System call number from RAX
 */
uint64_t hal_syscall_get_number(hal_context_t *ctx) {
    x86_64_context_t *arch_ctx = (x86_64_context_t *)ctx;
    
    if (!arch_ctx) {
        return 0;
    }
    
    return arch_ctx->rax;
}
