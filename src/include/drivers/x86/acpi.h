#ifndef _DRIVERS_X86_ACPI_H_
#define _DRIVERS_X86_ACPI_H_

#include <types.h>

/**
 * @file acpi.h
 * @brief 从 ACPI 的表里读出机器上有哪些 CPU、怎么关机、怎么复位
 *
 * 固件在内存里留了一组表描述这台机器。内核用其中两张：MADT 列出每个 CPU 的 Local APIC
 * （启动其余的 CPU 之前要知道有几个、APIC ID 各是多少）；FADT 说电源管理的寄存器在哪个
 * 端口上，它指向的 DSDT 里有关机时要往那个寄存器里写的值。
 * 表在固件保留的内存里，不一定在内核的直接映射区里：不在的话借一页虚拟地址临时映射过去读
 * （见 acpi.cpp 的 read_phys）。找不到表时什么都不知道，调用者自己想办法。
 */

namespace drivers {

class Acpi {
public:
    /**
     * 机器上可用的 CPU 的 APIC ID，按表里的顺序。
     * @param ids 至少能放 max 个
     * @return 一共有几个（不超过 max）；0 表示没有找到这张表
     */
    static uint32_t cpu_apic_ids(uint8_t *ids, uint32_t max);

    /**
     * 关机（进入 ACPI 的 S5 状态）。成功就不返回了；返回说明这台机器的表里找不到办法。
     */
    static void power_off();

    /**
     * 用 FADT 里的复位寄存器让机器复位。成功就不返回了；表里没有这个寄存器（老机器）、
     * 或者它不在 I/O 端口上时什么都不做。
     */
    static void reset();

    /**
     * 在一段 AML（DSDT 的内容）里找 \_S5 这个名字，读出关机时要写进电源管理寄存器的两个值。
     * 这里不解释 AML，只认这个名字最常见的写法：Name(_S5, Package() { a, b, ... })，
     * 前两项是小整数。
     * @return 找到了没有
     */
    static bool find_s5(const uint8_t *aml, size_t length, uint8_t *type_a, uint8_t *type_b);
};

} // namespace drivers

#endif // _DRIVERS_X86_ACPI_H_
