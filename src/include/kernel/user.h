#ifndef _KERNEL_USER_H_
#define _KERNEL_USER_H_

#include <types.h>

/**
 * @brief Enter user mode and start executing user code
 * @param entry_point User code entry point address
 * @param user_stack User stack pointer
 * 
 * This function transitions from kernel mode to user mode using
 * architecture-specific mechanisms:
 *   - i686: IRET instruction
 *   - x86_64: IRETQ instruction
 *   - arm64: ERET instruction (future)
 * 
 * This function never returns.
 */
void task_enter_usermode(uintptr_t entry_point, uintptr_t user_stack) __attribute__((noreturn));

#endif // _KERNEL_USER_H_
