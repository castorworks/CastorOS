#ifndef _DRIVERS_X86_ACPI_H_
#define _DRIVERS_X86_ACPI_H_

#include <types.h>

/**
 * @file acpi.h
 * @brief 从 ACPI 的表里读出机器上有哪些 CPU
 *
 * 固件在内存里留了一组表描述这台机器，其中 MADT 列出每个 CPU 的 Local APIC。
 * 内核只用它做一件事：启动其余的 CPU 之前知道有几个、APIC ID 各是多少。
 * 找不到表（或者表所在的内存没有映射进内核）时什么都不知道，调用者自己想办法。
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
};

} // namespace drivers

#endif // _DRIVERS_X86_ACPI_H_
