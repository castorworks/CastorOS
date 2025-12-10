/**
 * @file syscall.h
 * @brief ARM64 System Call Definitions
 * 
 * Defines ARM64-specific system call constants and structures.
 * 
 * **Feature: multi-arch-support**
 * **Validates: Requirements 7.5, 8.1, 8.2**
 */

#ifndef _ARCH_ARM64_SYSCALL_H_
#define _ARCH_ARM64_SYSCALL_H_

#include <types.h>

/* ============================================================================
 * ARM64 System Call Convention
 * ============================================================================
 * 
 * ARM64 uses the SVC (Supervisor Call) instruction for system calls.
 * 
 * Register usage:
 *   X8  = System call number
 *   X0  = Argument 1 / Return value
 *   X1  = Argument 2
 *   X2  = Argument 3
 *   X3  = Argument 4
 *   X4  = Argument 5
 *   X5  = Argument 6
 * 
 * The SVC instruction triggers a synchronous exception with:
 *   - Exception Class (EC) = 0x15 (ESR_EC_SVC64)
 *   - ISS field contains the immediate value from SVC instruction
 * 
 * Return value is placed in X0.
 * ========================================================================== */

/* ============================================================================
 * System Call Entry/Exit
 * ========================================================================== */

/**
 * @brief ARM64 syscall handler (assembly entry point)
 * 
 * Called from the exception handler when an SVC instruction is executed.
 * Extracts arguments from the saved register frame and calls syscall_dispatcher.
 * 
 * @param regs Pointer to saved register frame
 */
void arm64_syscall_handler(void *regs);

/**
 * @brief Enter user mode (ARM64)
 * 
 * Transitions from kernel mode (EL1) to user mode (EL0) using ERET.
 * Sets up the return address and stack pointer for user mode execution.
 * 
 * @param entry_point User code entry address
 * @param user_stack User stack pointer
 * 
 * **Feature: multi-arch-support, Property 11: User Mode Transition Correctness (ARM64)**
 * **Validates: Requirements 7.4**
 */
void enter_usermode_arm64(uint64_t entry_point, uint64_t user_stack);

#endif /* _ARCH_ARM64_SYSCALL_H_ */

