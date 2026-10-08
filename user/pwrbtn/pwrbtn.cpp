// pwrbtn - 电源键驱动
//
// 用户态驱动，没有特权：只碰得到 init 许可给它的那个设备（PC 上是 ACPI 电源管理的事件
// 寄存器，arm64 上是一个 GPIO 控制器）。它只做一件事：电源键按下时请 init 关机（power.h），
// 和在命令行里敲 poweroff 是同一条路。机器上没有电源键时直接退出。

#include <syscall.h>
#include <stdio.h>
#include <names.h>
#include <power.h>
#include "button.h"

int main() {
    struct hw_range irq;
    if (!hw_find(HW_IRQ, 0, &irq)) {
        printf("pwrbtn: no power button\n");
        return 1;
    }
    int line = (int)irq.start;
    if (irq_claim(line) != 0) {
        printf("pwrbtn: cannot claim IRQ %d\n", line);
        return 1;
    }
    if (!button_open()) {
        printf("pwrbtn: cannot use the power button\n");
        return 1;
    }
    if (name_register(POWER_BUTTON_NAME) != 0) {
        printf("pwrbtn: cannot register name\n");
        return 1;
    }
    printf("pwrbtn: driver ready (pid %d, irq %d)\n", getpid(), line);

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_FROM_KERNEL, &m) != 0 || m.label != IPC_LABEL_IRQ) {
            continue;
        }
        bool pressed = button_pressed();
        irq_ack(line);
        if (pressed) {
            printf("pwrbtn: power button pressed\n");
            // init 接下了就不应答，我们停在这里直到断电；它已经在关机时会拒绝，那就接着等
            power_request(POWER_OFF);
        }
    }
}
