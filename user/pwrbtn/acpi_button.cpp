// PC 的电源键：ACPI 的“固定事件”之一。
//
// 许可给我们的是电源管理的事件寄存器块：一段端口，前一半是状态寄存器，后一半是使能寄存器
// （各 16 位起），里面各有一位是电源键的。使能位置上之后，键按下时状态位变成 1，电源管理的
// 那条中断线（SCI）就有中断；往状态位写 1 把它清掉，中断才撤销。

#include <syscall.h>
#include "button.h"

#define PM1_POWER_BUTTON    (1u << 8)   // 状态寄存器和使能寄存器里电源键的那一位

static uint32_t status_port, enable_port;

bool button_open(void) {
    struct hw_range ports;
    if (!hw_find(HW_PORTS, 0, &ports) || ports.count < 4) {
        return false;
    }
    status_port = (uint32_t)ports.start;
    enable_port = (uint32_t)(ports.start + ports.count / 2);

    uint32_t enabled;
    io_write(status_port, 2, PM1_POWER_BUTTON);     // 开机以来按过的不算
    if (io_read(enable_port, 2, &enabled) != 0) {
        return false;
    }
    io_write(enable_port, 2, enabled | PM1_POWER_BUTTON);
    return true;
}

bool button_pressed(void) {
    uint32_t status = 0;
    io_read(status_port, 2, &status);
    if (!(status & PM1_POWER_BUTTON)) {
        return false;
    }
    io_write(status_port, 2, PM1_POWER_BUTTON);
    return true;
}
