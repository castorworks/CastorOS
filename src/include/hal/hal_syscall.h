/**
 * @file hal_syscall.h
 * @brief HAL System Call Parameter Abstraction Interface
 * 
 * This header defines the unified system call parameter interface that abstracts
 * architecture-specific system call argument passing conventions.
 * 
 * System call ABI differences:
 *   - i686: Arguments passed on stack and in registers (EBX, ECX, EDX, ESI, EDI, EBP)
 *   - x86_64: Arguments in RDI, RSI, RDX, R10, R8, R9
 *   - ARM64: Arguments in X0-X5, syscall number in X8
 * 
 * **Feature: multi-arch-optimization**
 * **Validates: Requirements 7.1, 7.2**
 */

#ifndef _HAL_HAL_SYSCALL_H_
#define _HAL_HAL_SYSCALL_H_

#include <types.h>
#include <hal/hal.h>

/* ============================================================================
 * System Call Argument Structure
 * ========================================================================== */

/**
 * @brief Maximum number of system call arguments
 * 
 * Most system calls use 6 or fewer arguments. For system calls with more
 * arguments, the extra_args pointer can be used.
 */
#define HAL_SYSCALL_MAX_ARGS    6

/**
 * @brief Unified system call arguments structure
 * 
 * This structure provides an architecture-independent representation of
 * system call arguments. The HAL extracts arguments from architecture-specific
 * locations (registers, stack) and populates this structure.
 * 
 * @see Requirements 7.1
 */
typedef struct hal_syscall_args {
    uint64_t syscall_nr;                    /**< System call number */
    uint64_t args[HAL_SYSCALL_MAX_ARGS];    /**< Arguments 0-5 */
    void *extra_args;                       /**< Extra arguments pointer (>6 args) */
} hal_syscall_args_t;

/* ============================================================================
 * System Call Parameter Functions
 * ========================================================================== */

/**
 * @brief Extract system call arguments from CPU context
 * 
 * Reads the system call number and arguments from the architecture-specific
 * CPU context and populates the hal_syscall_args_t structure.
 * 
 * Architecture-specific behavior:
 *   - i686: Extracts from EAX (syscall_nr), EBX, ECX, EDX, ESI, EDI, EBP
 *   - x86_64: Extracts from RAX (syscall_nr), RDI, RSI, RDX, R10, R8, R9
 *   - ARM64: Extracts from X8 (syscall_nr), X0, X1, X2, X3, X4, X5
 * 
 * @param ctx CPU context from which to extract arguments
 * @param[out] args Pointer to structure to fill with arguments
 * 
 * @see Requirements 7.1, 7.3
 */
void hal_syscall_get_args(hal_context_t *ctx, hal_syscall_args_t *args);

/**
 * @brief Set system call return value in CPU context
 * 
 * Places the return value in the architecture-appropriate register so that
 * it will be available to the user program when the system call returns.
 * 
 * Architecture-specific behavior:
 *   - i686: Sets EAX
 *   - x86_64: Sets RAX
 *   - ARM64: Sets X0
 * 
 * @param ctx CPU context to modify
 * @param ret Return value to set
 * 
 * @see Requirements 7.2
 */
void hal_syscall_set_return(hal_context_t *ctx, int64_t ret);

/**
 * @brief Set system call error code in CPU context
 * 
 * Sets an error code in the appropriate location. On most architectures,
 * this is the same as setting a negative return value, but some architectures
 * may have separate error registers.
 * 
 * @param ctx CPU context to modify
 * @param errno Error code to set (positive value, will be negated)
 * 
 * @see Requirements 7.2
 */
void hal_syscall_set_errno(hal_context_t *ctx, int32_t errno);

/**
 * @brief Get a specific system call argument from context
 * 
 * Convenience function to get a single argument without extracting all.
 * 
 * @param ctx CPU context
 * @param index Argument index (0-5)
 * @return Argument value, or 0 if index is out of range
 */
uint64_t hal_syscall_get_arg(hal_context_t *ctx, uint32_t index);

/**
 * @brief Get system call number from context
 * 
 * Convenience function to get just the system call number.
 * 
 * @param ctx CPU context
 * @return System call number
 */
uint64_t hal_syscall_get_number(hal_context_t *ctx);

#endif /* _HAL_HAL_SYSCALL_H_ */
