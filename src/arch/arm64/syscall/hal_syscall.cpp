/**
 * @file hal_syscall.c
 * @brief ARM64 HAL System Call Parameter Implementation
 * 
 * This file implements the ARM64-specific system call parameter extraction
 * and return value setting as part of the HAL.
 *
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 7.1, 7.3**
 *
 * ARM64 System Call ABI (AAPCS64):
 *   - X8  = system call number
 *   - X0  = arg1 (also return value)
 *   - X1  = arg2
 *   - X2  = arg3
 *   - X3  = arg4
 *   - X4  = arg5
 *   - X5  = arg6
 *   - Return value in X0
 */

#include <context.h>
#include <hal/hal_syscall.h>

/**
 * @brief Extract system call arguments from ARM64 CPU context
 * 
 * On ARM64, system call arguments are passed in registers:
 *   - X8: syscall number
 *   - X0-X5: args 0-5
 * 
 * @param ctx CPU context from which to extract arguments
 * @param[out] args Pointer to structure to fill with arguments
 */
void hal_syscall_get_args(hal_context_t *ctx, hal_syscall_args_t *args) {
    arm64_context_t *arch_ctx = (arm64_context_t *)ctx;
    
    if (!arch_ctx || !args) {
        return;
    }
    
    /* Extract syscall number from X8 */
    args->syscall_nr = arch_ctx->x[8];
    
    /* Extract arguments from X0-X5 */
    args->args[0] = arch_ctx->x[0];  /* arg1 */
    args->args[1] = arch_ctx->x[1];  /* arg2 */
    args->args[2] = arch_ctx->x[2];  /* arg3 */
    args->args[3] = arch_ctx->x[3];  /* arg4 */
    args->args[4] = arch_ctx->x[4];  /* arg5 */
    args->args[5] = arch_ctx->x[5];  /* arg6 */
    
    /* No extra args pointer - all 6 args fit in registers */
    args->extra_args = NULL;
}

/**
 * @brief Set system call return value in ARM64 CPU context
 * 
 * On ARM64, the return value is placed in X0.
 * 
 * @param ctx CPU context to modify
 * @param ret Return value to set
 */
void hal_syscall_set_return(hal_context_t *ctx, int64_t ret) {
    arm64_context_t *arch_ctx = (arm64_context_t *)ctx;
    
    if (!arch_ctx) {
        return;
    }
    
    /* Set return value in X0 */
    arch_ctx->x[0] = (uint64_t)ret;
}

/**
 * @brief Set system call error code in ARM64 CPU context
 * 
 * On ARM64, error codes are returned as negative values in X0.
 * 
 * @param ctx CPU context to modify
 * @param errno Error code to set (positive value, will be negated)
 */
void hal_syscall_set_errno(hal_context_t *ctx, int32_t errno) {
    arm64_context_t *arch_ctx = (arm64_context_t *)ctx;
    
    if (!arch_ctx) {
        return;
    }
    
    /* Set negative error code in X0 */
    arch_ctx->x[0] = (uint64_t)(int64_t)(-errno);
}

/**
 * @brief Get a specific system call argument from ARM64 context
 * 
 * @param ctx CPU context
 * @param index Argument index (0-5)
 * @return Argument value, or 0 if index is out of range
 */
uint64_t hal_syscall_get_arg(hal_context_t *ctx, uint32_t index) {
    arm64_context_t *arch_ctx = (arm64_context_t *)ctx;
    
    if (!arch_ctx || index >= HAL_SYSCALL_MAX_ARGS) {
        return 0;
    }
    
    /* Arguments are in X0-X5 */
    return arch_ctx->x[index];
}

/**
 * @brief Get system call number from ARM64 context
 * 
 * @param ctx CPU context
 * @return System call number from X8
 */
uint64_t hal_syscall_get_number(hal_context_t *ctx) {
    arm64_context_t *arch_ctx = (arm64_context_t *)ctx;
    
    if (!arch_ctx) {
        return 0;
    }
    
    return arch_ctx->x[8];
}
