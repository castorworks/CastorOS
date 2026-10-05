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

namespace hal {

/**
 * @brief 系统调用入口与参数访问
 */
class Syscall {
public:
    /**
     * @brief Initialize system call entry mechanism
     * @param handler The system call dispatcher function
     */
    static void init(hal_syscall_handler_t handler);

    /* ============================================================================
     * System Call Parameter Functions
     * ========================================================================== */

};

} // namespace hal

#endif /* _HAL_HAL_SYSCALL_H_ */
