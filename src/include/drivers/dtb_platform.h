/**
 * @file dtb_platform.h
 * @brief DTB 设备到平台设备转换接口
 * 
 * 提供将设备树（Device Tree Blob）设备转换为平台设备的功能，
 * 使驱动程序可以通过统一的平台设备接口访问 ARM64 设备。
 * 
 * @see Requirements 6.3
 */

#ifndef _DRIVERS_DTB_PLATFORM_H_
#define _DRIVERS_DTB_PLATFORM_H_

#include <drivers/platform.h>

namespace drivers {

/**
 * @brief 设备树平台设备适配
 */
class DtbPlatform {
public:
    /**
     * @brief 扫描 DTB 并创建平台设备
     * 
     * 遍历设备树中的所有设备，为每个设备创建对应的平台设备。
     * 包括 GIC、UART、Timer 等核心设备。
     * 
     * @return 创建的平台设备数量
     */
    static int scan();

    /**
     * @brief 根据 compatible 字符串查找并创建平台设备
     * 
     * @param compatible compatible 字符串
     * @return 平台设备指针，未找到返回 NULL
     */
    static platform_device_t *find_device(const char *compatible);
};

} // namespace drivers

#endif /* _DRIVERS_DTB_PLATFORM_H_ */
