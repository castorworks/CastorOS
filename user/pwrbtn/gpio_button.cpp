// arm64 上的电源键：接在 GPIO 控制器（ARM 的 PL061，8 根引脚）一根引脚上的按键。
//
// 许可给我们的是这个控制器的寄存器。让那根引脚在电平由低变高时发中断；中断来了看是不是它，
// 是就清掉。

#include <syscall.h>
#include "button.h"

// 按键接在哪根引脚上写在设备树的 gpio-keys 节点里；内核的设备树解析不看那里，这里用的是
// QEMU virt 的接法
#define BUTTON_PIN      (1u << 3)

// 寄存器（偏移，每个 32 位，低 8 位每根引脚一位）
#define GPIO_DIR        0x400   // 方向：0 = 输入
#define GPIO_IS         0x404   // 中断看什么：0 = 边沿
#define GPIO_IBE        0x408   // 0 = 只看一种边沿
#define GPIO_IEV        0x40C   // 哪一种：1 = 上升沿
#define GPIO_IE         0x410   // 中断使能
#define GPIO_MIS        0x418   // 哪些引脚有（使能了的）中断
#define GPIO_IC         0x41C   // 写 1 清中断

static volatile uint32_t *regs;

static uint32_t reg_read(uint32_t offset) {
    return regs[offset / 4];
}

static void reg_write(uint32_t offset, uint32_t value) {
    regs[offset / 4] = value;
}

bool button_open(void) {
    struct hw_range where;
    if (!hw_find(HW_MEMORY, 0, &where)) {
        return false;
    }
    void *mapped = map_device((uintptr_t)where.start & ~(uintptr_t)0xFFF, 0x1000);
    if (mapped == MAP_FAILED) {
        return false;
    }
    regs = (volatile uint32_t *)((char *)mapped + (where.start & 0xFFF));

    reg_write(GPIO_IE, reg_read(GPIO_IE) & ~BUTTON_PIN);
    reg_write(GPIO_DIR, reg_read(GPIO_DIR) & ~BUTTON_PIN);
    reg_write(GPIO_IS, reg_read(GPIO_IS) & ~BUTTON_PIN);
    reg_write(GPIO_IBE, reg_read(GPIO_IBE) & ~BUTTON_PIN);
    reg_write(GPIO_IEV, reg_read(GPIO_IEV) | BUTTON_PIN);
    reg_write(GPIO_IC, BUTTON_PIN);                 // 开机以来按过的不算
    reg_write(GPIO_IE, reg_read(GPIO_IE) | BUTTON_PIN);
    return true;
}

bool button_pressed(void) {
    if (!(reg_read(GPIO_MIS) & BUTTON_PIN)) {
        return false;
    }
    reg_write(GPIO_IC, BUTTON_PIN);
    return true;
}
