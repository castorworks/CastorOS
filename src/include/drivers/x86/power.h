#ifndef _DRIVERS_X86_POWER_H_
#define _DRIVERS_X86_POWER_H_

/**
 * @file power.h
 * @brief PC 的关机和复位（i686 和 x86_64 共用）
 */

struct device_info;

namespace drivers {

class Power {
public:
    /** 关机。成功就不返回了；返回说明这台机器没有给出办法（见 Acpi::power_off） */
    static void off();

    /**
     * PC 上固件告诉内核的设备只有一个：电源键，型号写作 "acpi,power-button"。
     * info->base 得到它的寄存器所在的端口（不是内存地址），size 是端口数，irq 是它的中断线。
     * @return 问的是别的型号、或者这台机器没有时返回 false
     */
    static bool find_device(struct device_info *info);

    /** 让机器复位，从固件重新启动。不返回 */
    [[noreturn]] static void reboot();
};

} // namespace drivers

#endif // _DRIVERS_X86_POWER_H_
