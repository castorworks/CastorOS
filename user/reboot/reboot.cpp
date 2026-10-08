// reboot - 重启：请 init 来做（power.h）。成功的话回不到这里

#include <power.h>
#include <stdio.h>

int main() {
    power_request(POWER_REBOOT);
    eprintf("reboot: init refused\n");
    return 1;
}
