/**
 * @file dtb_platform.c
 * @brief DTB 设备到平台设备的转换
 * 
 * 将设备树（Device Tree Blob）发现的设备转换为平台设备，
 * 使驱动程序可以通过统一的平台设备接口访问 ARM64 设备资源。
 * 
 * @see Requirements 6.3
 */

#include <drivers/platform.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* DTB 支持仅在 ARM64 架构上可用 */
#ifdef ARCH_ARM64

#include <dtb.h>

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 从 DTB 设备创建平台设备
 */
static platform_device_t *create_platform_device_from_dtb(const dtb_device_t *dtb_dev) {
    if (!dtb_dev || !dtb_dev->valid) return NULL;
    
    /* 分配平台设备 */
    platform_device_t *pdev = platform_device_alloc(dtb_dev->name, -1);
    if (!pdev) {
        LOG_WARN_MSG("dtb_platform: Failed to allocate platform device\n");
        return NULL;
    }
    
    /* 设置来源 */
    pdev->source = PLATFORM_SRC_DTB;
    
    /* 填充 DTB 信息 */
    pdev->dtb.compatible = dtb_dev->name;
    pdev->dtb.node_name = dtb_dev->name;
    pdev->dtb.phandle = 0;
    
    /* 添加内存资源 */
    if (dtb_dev->base_addr != 0 && dtb_dev->size != 0) {
        if (pdev->num_resources < PLATFORM_MAX_RESOURCES) {
            platform_resource_t *res = &pdev->resources[pdev->num_resources++];
            res->type = PLATFORM_RES_MEM;
            res->start = dtb_dev->base_addr;
            res->end = dtb_dev->base_addr + dtb_dev->size - 1;
            res->flags = 0;
            res->name = NULL;
        }
    }
    
    /* 添加 IRQ 资源 */
    if (dtb_dev->irq != 0) {
        if (pdev->num_resources < PLATFORM_MAX_RESOURCES) {
            platform_resource_t *res = &pdev->resources[pdev->num_resources++];
            res->type = PLATFORM_RES_IRQ;
            res->start = dtb_dev->irq;
            res->end = dtb_dev->irq;
            res->flags = 0;
            res->name = NULL;
        }
    }
    
    return pdev;
}

/**
 * @brief 创建 GIC 平台设备
 */
static platform_device_t *create_gic_platform_device(const dtb_gic_info_t *gic) {
    if (!gic || !gic->found) return NULL;
    
    const char *name = (gic->version == 3) ? "arm,gic-v3" : "arm,gic-400";
    
    platform_device_t *pdev = platform_device_alloc(name, 0);
    if (!pdev) return NULL;
    
    pdev->source = PLATFORM_SRC_DTB;
    pdev->dtb.compatible = name;
    pdev->dtb.node_name = "intc";
    
    /* 添加 GICD (Distributor) 资源 */
    if (gic->distributor_base != 0) {
        platform_device_add_mem_resource(pdev, gic->distributor_base, 
                                          0x10000, 0);  /* 64KB typical */
    }
    
    /* 添加 GICC/GICR 资源 */
    if (gic->version == 2 && gic->cpu_interface_base != 0) {
        platform_device_add_mem_resource(pdev, gic->cpu_interface_base,
                                          0x2000, 0);  /* 8KB typical */
    } else if (gic->version == 3 && gic->redistributor_base != 0) {
        platform_device_add_mem_resource(pdev, gic->redistributor_base,
                                          0x20000, 0);  /* 128KB typical */
    }
    
    return pdev;
}

/**
 * @brief 创建 UART 平台设备
 */
static platform_device_t *create_uart_platform_device(const dtb_info_t *info) {
    if (!info || !info->uart_found || info->uart_base == 0) return NULL;
    
    platform_device_t *pdev = platform_device_alloc("arm,pl011", 0);
    if (!pdev) return NULL;
    
    pdev->source = PLATFORM_SRC_DTB;
    pdev->dtb.compatible = "arm,pl011";
    pdev->dtb.node_name = "uart";
    
    /* 添加 MMIO 资源 */
    platform_device_add_mem_resource(pdev, info->uart_base, 0x1000, 0);
    
    /* 添加 IRQ 资源 */
    if (info->uart_irq != 0) {
        platform_device_add_irq_resource(pdev, info->uart_irq, 0);
    }
    
    return pdev;
}

/**
 * @brief 创建 Timer 平台设备
 */
static platform_device_t *create_timer_platform_device(const dtb_info_t *info) {
    if (!info || !info->timer_found) return NULL;
    
    platform_device_t *pdev = platform_device_alloc("arm,armv8-timer", 0);
    if (!pdev) return NULL;
    
    pdev->source = PLATFORM_SRC_DTB;
    pdev->dtb.compatible = "arm,armv8-timer";
    pdev->dtb.node_name = "timer";
    
    /* Timer 没有 MMIO，只有 IRQ */
    if (info->timer_irq != 0) {
        platform_device_add_irq_resource(pdev, info->timer_irq, 0);
    }
    
    return pdev;
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/**
 * @brief 扫描 DTB 并创建平台设备
 * 
 * @return 创建的平台设备数量
 */
int dtb_platform_scan(void) {
    int count = 0;
    
    dtb_info_t *info = dtb_get_info();
    if (!info || !info->valid) {
        LOG_WARN_MSG("dtb_platform: DTB not available or invalid\n");
        return 0;
    }
    
    LOG_INFO_MSG("dtb_platform: Scanning DTB for devices\n");
    
    /* 创建 GIC 平台设备 */
    if (info->gic.found) {
        platform_device_t *pdev = create_gic_platform_device(&info->gic);
        if (pdev && HAL_SUCCESS(platform_device_register(pdev))) {
            count++;
            LOG_DEBUG_MSG("dtb_platform: Created GICv%d platform device\n",
                          info->gic.version);
        }
    }
    
    /* 创建 UART 平台设备 */
    if (info->uart_found) {
        platform_device_t *pdev = create_uart_platform_device(info);
        if (pdev && HAL_SUCCESS(platform_device_register(pdev))) {
            count++;
            LOG_DEBUG_MSG("dtb_platform: Created UART platform device @ 0x%llx\n",
                          (unsigned long long)info->uart_base);
        }
    }
    
    /* 创建 Timer 平台设备 */
    if (info->timer_found) {
        platform_device_t *pdev = create_timer_platform_device(info);
        if (pdev && HAL_SUCCESS(platform_device_register(pdev))) {
            count++;
            LOG_DEBUG_MSG("dtb_platform: Created Timer platform device\n");
        }
    }
    
    /* 创建其他设备的平台设备 */
    for (uint32_t i = 0; i < info->num_devices; i++) {
        const dtb_device_t *dtb_dev = &info->devices[i];
        if (!dtb_dev->valid) continue;
        
        platform_device_t *pdev = create_platform_device_from_dtb(dtb_dev);
        if (pdev && HAL_SUCCESS(platform_device_register(pdev))) {
            count++;
            LOG_DEBUG_MSG("dtb_platform: Created platform device '%s'\n",
                          dtb_dev->name);
        }
    }
    
    LOG_INFO_MSG("dtb_platform: Created %d platform devices from DTB\n", count);
    
    return count;
}

/**
 * @brief 根据 compatible 字符串查找平台设备
 * 
 * @param compatible compatible 字符串
 * @return 平台设备指针，未找到返回 NULL
 */
platform_device_t *dtb_platform_find_device(const char *compatible) {
    if (!compatible) return NULL;
    
    /* 首先在 DTB 中查找 */
    const dtb_device_t *dtb_dev = dtb_find_device(compatible);
    if (!dtb_dev) return NULL;
    
    /* 创建平台设备 */
    platform_device_t *pdev = create_platform_device_from_dtb(dtb_dev);
    if (pdev) {
        platform_device_register(pdev);
    }
    
    return pdev;
}

#else /* !ARCH_ARM64 */

/* 非 ARM64 架构的空实现 */

int dtb_platform_scan(void) {
    LOG_DEBUG_MSG("dtb_platform: DTB not supported on this architecture\n");
    return 0;
}

platform_device_t *dtb_platform_find_device(const char *compatible) {
    (void)compatible;
    return NULL;
}

#endif /* ARCH_ARM64 */
