#ifndef _KERNEL_USER_H_
#define _KERNEL_USER_H_

#include <types.h>

void task_enter_usermode(uint32_t entry_point, uint32_t user_stack) __attribute__((noreturn));

#endif // _KERNEL_USER_H_
