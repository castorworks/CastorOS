/**
 * @file pci_platform.h
 * @brief PCI 设备到平台设备转换接口
 * 
 * 提供将 PCI 设备转换为平台设备的功能，使驱动程序可以通过
 * 统一的平台设备接口访问 PCI 设备。
 * 
 * @see Requirements 6.2
 */

#ifndef _DRIVERS_PCI_PLATFORM_H_
#define _DRIVERS_PCI_PLATFORM_H_

#include <drivers/platform.h>

#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/pci.h>
#endif

/**
 * @brief 扫描 PCI 总线并创建平台设备
 * 
 * 遍历所有已发现的 PCI 设备，为每个设备创建对应的平台设备。
 * 
 * @return 创建的平台设备数量
 */
int pci_platform_scan(void);

/**
 * @brief 从 PCI 设备创建单个平台设备
 * 
 * @param vendor_id PCI 厂商 ID
 * @param device_id PCI 设备 ID
 * @return 平台设备指针，失败返回 NULL
 */
platform_device_t *pci_platform_create_device(uint16_t vendor_id, 
                                               uint16_t device_id);

/**
 * @brief 启用平台设备的 PCI 总线主控功能
 * 
 * @param pdev 平台设备指针
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t pci_platform_enable_bus_master(platform_device_t *pdev);

/**
 * @brief 启用平台设备的 PCI 内存空间访问
 * 
 * @param pdev 平台设备指针
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t pci_platform_enable_memory_space(platform_device_t *pdev);

#if defined(ARCH_I686) || defined(ARCH_X86_64)
/**
 * @brief 获取平台设备对应的原始 PCI 设备
 * 
 * @param pdev 平台设备指针
 * @return PCI 设备指针，如果不是 PCI 设备则返回 NULL
 */
pci_device_t *pci_platform_get_pci_device(platform_device_t *pdev);
#endif

#endif /* _DRIVERS_PCI_PLATFORM_H_ */
