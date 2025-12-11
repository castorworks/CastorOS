/**
 * @file pci_platform.c
 * @brief PCI 设备到平台设备的转换
 * 
 * 将 PCI 枚举发现的设备转换为平台设备，使驱动程序可以通过
 * 统一的平台设备接口访问 PCI 设备资源。
 * 
 * @see Requirements 6.2
 */

#include <drivers/platform.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* PCI 支持仅在 x86 架构上可用 */
#if defined(ARCH_I686) || defined(ARCH_X86_64)

#include <drivers/pci.h>

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 生成 PCI 设备名称
 * 
 * 格式: pci_VVVV_DDDD (VVVV=vendor_id, DDDD=device_id)
 */
static void generate_pci_device_name(char *buf, size_t size,
                                     uint16_t vendor_id, uint16_t device_id) {
    /* 简单的十六进制转换 */
    static const char hex[] = "0123456789abcdef";
    
    if (size < 14) return;  /* "pci_XXXX_XXXX" + null */
    
    buf[0] = 'p';
    buf[1] = 'c';
    buf[2] = 'i';
    buf[3] = '_';
    buf[4] = hex[(vendor_id >> 12) & 0xF];
    buf[5] = hex[(vendor_id >> 8) & 0xF];
    buf[6] = hex[(vendor_id >> 4) & 0xF];
    buf[7] = hex[vendor_id & 0xF];
    buf[8] = '_';
    buf[9] = hex[(device_id >> 12) & 0xF];
    buf[10] = hex[(device_id >> 8) & 0xF];
    buf[11] = hex[(device_id >> 4) & 0xF];
    buf[12] = hex[device_id & 0xF];
    buf[13] = '\0';
}

/**
 * @brief 从 PCI 设备创建平台设备
 */
static platform_device_t *create_platform_device_from_pci(pci_device_t *pci_dev) {
    if (!pci_dev) return NULL;
    
    /* 生成设备名称 */
    char name[PLATFORM_NAME_MAX];
    generate_pci_device_name(name, sizeof(name), 
                             pci_dev->vendor_id, pci_dev->device_id);
    
    /* 分配平台设备 */
    platform_device_t *pdev = platform_device_alloc(name, -1);
    if (!pdev) {
        LOG_WARN_MSG("pci_platform: Failed to allocate platform device\n");
        return NULL;
    }
    
    /* 设置来源 */
    pdev->source = PLATFORM_SRC_PCI;
    
    /* 填充 PCI 信息 */
    pdev->pci.vendor_id = pci_dev->vendor_id;
    pdev->pci.device_id = pci_dev->device_id;
    pdev->pci.bus = pci_dev->bus;
    pdev->pci.slot = pci_dev->slot;
    pdev->pci.func = pci_dev->func;
    pdev->pci.class_code = pci_dev->class_code;
    pdev->pci.subclass = pci_dev->subclass;
    pdev->pci.prog_if = pci_dev->prog_if;
    
    /* 添加 BAR 资源 */
    for (int i = 0; i < 6; i++) {
        if (pci_dev->bar[i] == 0) continue;
        
        uint32_t addr = pci_get_bar_address(pci_dev, i);
        uint32_t size = pci_get_bar_size(pci_dev, i);
        
        if (addr == 0 || size == 0) continue;
        
        if (pci_bar_is_io(pci_dev, i)) {
            /* I/O 端口资源 */
            if (pdev->num_resources < PLATFORM_MAX_RESOURCES) {
                platform_resource_t *res = &pdev->resources[pdev->num_resources++];
                res->type = PLATFORM_RES_IO;
                res->start = addr;
                res->end = addr + size - 1;
                res->flags = 0;
                res->name = NULL;
            }
        } else {
            /* 内存映射资源 */
            uint32_t flags = 0;
            if (pci_dev->bar[i] & 0x08) {  /* Prefetchable */
                flags |= PLATFORM_RES_FLAG_PREFETCH;
            }
            if ((pci_dev->bar[i] & 0x06) == 0x04) {  /* 64-bit */
                flags |= PLATFORM_RES_FLAG_64BIT;
            }
            
            if (pdev->num_resources < PLATFORM_MAX_RESOURCES) {
                platform_resource_t *res = &pdev->resources[pdev->num_resources++];
                res->type = PLATFORM_RES_MEM;
                res->start = addr;
                res->end = addr + size - 1;
                res->flags = flags;
                res->name = NULL;
            }
        }
    }
    
    /* 添加 IRQ 资源 */
    if (pci_dev->interrupt_line != 0 && pci_dev->interrupt_line != 0xFF) {
        if (pdev->num_resources < PLATFORM_MAX_RESOURCES) {
            platform_resource_t *res = &pdev->resources[pdev->num_resources++];
            res->type = PLATFORM_RES_IRQ;
            res->start = pci_dev->interrupt_line;
            res->end = pci_dev->interrupt_line;
            res->flags = 0;
            res->name = NULL;
        }
    }
    
    return pdev;
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/**
 * @brief 扫描 PCI 总线并创建平台设备
 * 
 * @return 创建的平台设备数量
 */
int pci_platform_scan(void) {
    int count = 0;
    int pci_count = pci_get_device_count();
    
    LOG_INFO_MSG("pci_platform: Scanning %d PCI devices\n", pci_count);
    
    for (int i = 0; i < pci_count; i++) {
        pci_device_t *pci_dev = pci_get_device(i);
        if (!pci_dev) continue;
        
        /* 创建平台设备 */
        platform_device_t *pdev = create_platform_device_from_pci(pci_dev);
        if (!pdev) continue;
        
        /* 注册平台设备 */
        hal_error_t err = platform_device_register(pdev);
        if (HAL_SUCCESS(err)) {
            count++;
            LOG_DEBUG_MSG("pci_platform: Created platform device for PCI %02x:%02x.%x "
                          "(%04x:%04x)\n",
                          pci_dev->bus, pci_dev->slot, pci_dev->func,
                          pci_dev->vendor_id, pci_dev->device_id);
        } else {
            platform_device_free(pdev);
        }
    }
    
    LOG_INFO_MSG("pci_platform: Created %d platform devices from PCI\n", count);
    
    return count;
}

/**
 * @brief 从 PCI 设备创建单个平台设备
 * 
 * @param vendor_id PCI 厂商 ID
 * @param device_id PCI 设备 ID
 * @return 平台设备指针，失败返回 NULL
 */
platform_device_t *pci_platform_create_device(uint16_t vendor_id, 
                                               uint16_t device_id) {
    pci_device_t *pci_dev = pci_find_device(vendor_id, device_id);
    if (!pci_dev) {
        LOG_WARN_MSG("pci_platform: PCI device %04x:%04x not found\n",
                     vendor_id, device_id);
        return NULL;
    }
    
    platform_device_t *pdev = create_platform_device_from_pci(pci_dev);
    if (pdev) {
        platform_device_register(pdev);
    }
    
    return pdev;
}

/**
 * @brief 获取平台设备对应的原始 PCI 设备
 * 
 * @param pdev 平台设备指针
 * @return PCI 设备指针，如果不是 PCI 设备则返回 NULL
 */
pci_device_t *pci_platform_get_pci_device(platform_device_t *pdev) {
    if (!pdev || pdev->source != PLATFORM_SRC_PCI) {
        return NULL;
    }
    
    /* 通过 bus/slot/func 查找原始 PCI 设备 */
    int count = pci_get_device_count();
    for (int i = 0; i < count; i++) {
        pci_device_t *pci_dev = pci_get_device(i);
        if (pci_dev &&
            pci_dev->bus == pdev->pci.bus &&
            pci_dev->slot == pdev->pci.slot &&
            pci_dev->func == pdev->pci.func) {
            return pci_dev;
        }
    }
    
    return NULL;
}

/**
 * @brief 启用平台设备的 PCI 总线主控功能
 * 
 * @param pdev 平台设备指针
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t pci_platform_enable_bus_master(platform_device_t *pdev) {
    pci_device_t *pci_dev = pci_platform_get_pci_device(pdev);
    if (!pci_dev) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    pci_enable_bus_master(pci_dev);
    return HAL_OK;
}

/**
 * @brief 启用平台设备的 PCI 内存空间访问
 * 
 * @param pdev 平台设备指针
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t pci_platform_enable_memory_space(platform_device_t *pdev) {
    pci_device_t *pci_dev = pci_platform_get_pci_device(pdev);
    if (!pci_dev) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    pci_enable_memory_space(pci_dev);
    return HAL_OK;
}

#else /* !ARCH_I686 && !ARCH_X86_64 */

/* 非 x86 架构的空实现 */

int pci_platform_scan(void) {
    LOG_DEBUG_MSG("pci_platform: PCI not supported on this architecture\n");
    return 0;
}

platform_device_t *pci_platform_create_device(uint16_t vendor_id, 
                                               uint16_t device_id) {
    (void)vendor_id;
    (void)device_id;
    return NULL;
}

hal_error_t pci_platform_enable_bus_master(platform_device_t *pdev) {
    (void)pdev;
    return HAL_ERR_NOT_SUPPORTED;
}

hal_error_t pci_platform_enable_memory_space(platform_device_t *pdev) {
    (void)pdev;
    return HAL_ERR_NOT_SUPPORTED;
}

#endif /* ARCH_I686 || ARCH_X86_64 */
