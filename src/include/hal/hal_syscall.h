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

    /**
     * @brief The sixth argument of the system call whose saved registers are at frame
     *
     * The dispatcher receives the first five arguments in registers. The sixth stays in
     * the register frame saved by the entry stub, in the register the user library's
     * syscall6 puts it in: EBP on i686, R9 on x86_64, X5 on arm64.
     */
    static uintptr_t arg6(const uintptr_t *frame);

};

} // namespace hal

#endif /* _HAL_HAL_SYSCALL_H_ */
