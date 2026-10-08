// ============================================================================
// power.cpp - PC 的关机和复位
// ============================================================================
//
// 关机只有 ACPI 一条路。复位的办法 PC 上攒了好几代，哪一种管用看机器，所以从新到旧
// 一个个试，每个试完等一会儿，还在运行就试下一个：
//   1. ACPI 的复位寄存器（固件自己说的办法）
//   2. 键盘控制器：它有一根输出线接在 CPU 的复位脚上
//   3. 芯片组的复位控制寄存器（端口 0xCF9）
//   4. 三重故障：中断描述符表清空之后触发一个异常，CPU 处理不了，只好复位

#include <drivers/x86/power.h>
#include <drivers/x86/acpi.h>
#include <hal/hal.h>
#include <kernel/syscall.h>
#include <lib/string.h>

#define KBC_STATUS          0x64    // 键盘控制器：读是状态，写是命令
#define KBC_INPUT_FULL      0x02    // 状态：上一条命令它还没取走
#define KBC_PULSE_RESET     0xFE    // 命令：把复位线拉低一下

#define RESET_CONTROL       0xCF9
#define RESET_CONTROL_CPU   0x02    // 复位的种类：连同总线一起
#define RESET_CONTROL_GO    0x04    // 由 0 变 1 时复位

static void io_delay(uint32_t writes) {
    for (uint32_t i = 0; i < writes; i++) {
        hal::Port::write8(0x80, 0);
    }
}

namespace drivers {

void Power::off() {
    Acpi::power_off();
}

bool Power::find_device(struct device_info *info) {
    uint16_t port;
    uint32_t length, irq;
    if (strcmp(info->compatible, "acpi,power-button") != 0 || info->index != 0 ||
        !Acpi::power_button(&port, &length, &irq)) {
        return false;
    }
    info->base = port;
    info->size = length;
    info->irq = irq;
    info->has_irq = 1;
    strcpy(info->name, "acpi-power-button");
    return true;
}

void Power::reboot() {
    __asm__ volatile("cli");
    Acpi::reset();

    for (int i = 0; i < 10000 && (hal::Port::read8(KBC_STATUS) & KBC_INPUT_FULL); i++) {
        io_delay(10);
    }
    hal::Port::write8(KBC_STATUS, KBC_PULSE_RESET);
    io_delay(100000);

    hal::Port::write8(RESET_CONTROL, RESET_CONTROL_CPU);
    hal::Port::write8(RESET_CONTROL, RESET_CONTROL_CPU | RESET_CONTROL_GO);
    io_delay(100000);

    struct {
        uint16_t limit;
        uintptr_t base;
    } __attribute__((packed)) no_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int3" : : "m"(no_idt));
    for (;;) {
        __asm__ volatile("hlt");
    }
}

} // namespace drivers
