#ifndef _KERNEL_SYSCALLS_SYSTEM_H_
#define _KERNEL_SYSCALLS_SYSTEM_H_

#include <types.h>

uint32_t sys_reboot(void);
uint32_t sys_poweroff(void);

#endif /* _KERNEL_SYSCALLS_SYSTEM_H_ */


