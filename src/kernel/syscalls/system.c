#include <kernel/syscalls/system.h>
#include <kernel/system.h>

uint32_t sys_reboot(void) {
    system_reboot();
    return 0;
}

uint32_t sys_poweroff(void) {
    system_poweroff();
    return 0;
}


