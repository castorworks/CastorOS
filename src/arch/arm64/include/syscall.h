/**
 * @file syscall.h
 * @brief ARM64 System Call Definitions
 * 
 * Defines ARM64-specific system call constants and structures.
 */

#ifndef _ARCH_ARM64_SYSCALL_H_
#define _ARCH_ARM64_SYSCALL_H_

#include <types.h>

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
extern "C" void arm64_syscall_handler(void *regs);

/**
 * @brief Enter user mode (ARM64)
 * 
 * Transitions from kernel mode (EL1) to user mode (EL0) using ERET.
 * Sets up the return address and stack pointer for user mode execution.
 * 
 * @param entry_point User code entry address
 * @param user_stack User stack pointer
 */
extern "C" void enter_usermode_arm64(uint64_t entry_point, uint64_t user_stack);

#endif /* _ARCH_ARM64_SYSCALL_H_ */

