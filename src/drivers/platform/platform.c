/**
 * @file platform.c
 * @brief 平台设备核心框架实现
 * 
 * 实现平台设备模型的核心功能：
 * - 驱动注册和注销
 * - 设备注册和注销
 * - 设备与驱动的匹配
 * - 资源访问
 * 
 * @see Requirements 6.1
 */

#include <drivers/platform.h>
#include <lib/string.h>

/* ARM64 has minimal library support, use stubs for logging */
#ifdef ARCH_ARM64
    #define LOG_INFO_MSG(fmt, ...)  ((void)0)
    #define LOG_WARN_MSG(fmt, ...)  ((void)0)
    #define LOG_DEBUG_MSG(fmt, ...) ((void)0)
#else
    #include <lib/klog.h>
    #include <lib/kprintf.h>
#endif

/* ============================================================================
 * 全局状态
 * ============================================================================ */

/** 已注册的平台驱动 */
static platform_driver_t *g_drivers[PLATFORM_MAX_DRIVERS];
static int g_driver_count = 0;

/** 已注册的平台设备 */
static platform_device_t g_devices[PLATFORM_MAX_DEVICES];
static int g_device_count = 0;

/** 框架是否已初始化 */
static bool g_platform_initialized = false;

/** 设备 ID 计数器 */
static uint32_t g_next_device_id = 0;

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 简单字符串比较
 */
static int platform_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 != s2;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/**
 * @brief 检查 PCI ID 是否匹配
 */
static bool match_pci_id(const platform_pci_id_t *ids, 
                         uint16_t vendor_id, uint16_t device_id) {
    if (!ids) return false;
    
    while (ids->vendor_id != PCI_ID_END) {
        if (ids->vendor_id == vendor_id && ids->device_id == device_id) {
            return true;
        }
        ids++;
    }
    return false;
}

/**
 * @brief 检查 compatible 字符串是否匹配
 */
static bool match_compatible(const char **compatibles, const char *compat) {
    if (!compatibles || !compat) return false;
    
    while (*compatibles != COMPATIBLE_END) {
        if (platform_strcmp(*compatibles, compat) == 0) {
            return true;
        }
        compatibles++;
    }
    return false;
}

/**
 * @brief 尝试将设备与驱动匹配
 */
static bool try_match(platform_device_t *dev, platform_driver_t *drv) {
    if (!dev || !drv) return false;
    
    /* 根据设备来源进行匹配 */
    switch (dev->source) {
        case PLATFORM_SRC_PCI:
            return match_pci_id(drv->pci_ids, 
                               dev->pci.vendor_id, 
                               dev->pci.device_id);
        
        case PLATFORM_SRC_DTB:
            return match_compatible(drv->compatible, dev->dtb.compatible);
        
        case PLATFORM_SRC_MANUAL:
            /* 手动注册的设备按名称匹配 */
            return platform_strcmp(dev->name, drv->name) == 0;
        
        default:
            return false;
    }
}

/**
 * @brief 探测设备
 */
static int probe_device(platform_device_t *dev, platform_driver_t *drv) {
    if (!dev || !drv || !drv->probe) {
        return -1;
    }
    
    dev->driver = drv;
    int ret = drv->probe(dev);
    
    if (ret == 0) {
        dev->probed = true;
        LOG_INFO_MSG("platform: Probed device '%s' with driver '%s'\n",
                     dev->name, drv->name);
    } else {
        dev->driver = NULL;
        LOG_WARN_MSG("platform: Failed to probe device '%s' with driver '%s' (err=%d)\n",
                     dev->name, drv->name, ret);
    }
    
    return ret;
}

/* ============================================================================
 * 驱动注册 API 实现
 * ============================================================================ */

hal_error_t platform_driver_register(platform_driver_t *drv) {
    if (!drv || !drv->name) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    if (!g_platform_initialized) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    
    if (g_driver_count >= PLATFORM_MAX_DRIVERS) {
        LOG_WARN_MSG("platform: Maximum driver count reached\n");
        return HAL_ERR_NO_MEMORY;
    }
    
    /* 检查是否已注册 */
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i] == drv) {
            return HAL_ERR_ALREADY_EXISTS;
        }
    }
    
    /* 注册驱动 */
    g_drivers[g_driver_count++] = drv;
    drv->in_use = true;
    
    LOG_INFO_MSG("platform: Registered driver '%s'\n", drv->name);
    
    /* 尝试与已注册的设备匹配 */
    for (int i = 0; i < g_device_count; i++) {
        platform_device_t *dev = &g_devices[i];
        if (dev->in_use && !dev->probed && try_match(dev, drv)) {
            probe_device(dev, drv);
        }
    }
    
    return HAL_OK;
}

hal_error_t platform_driver_unregister(platform_driver_t *drv) {
    if (!drv) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    /* 查找驱动 */
    int idx = -1;
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i] == drv) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        return HAL_ERR_NOT_FOUND;
    }
    
    /* 移除所有绑定的设备 */
    for (int i = 0; i < g_device_count; i++) {
        platform_device_t *dev = &g_devices[i];
        if (dev->in_use && dev->driver == drv) {
            if (drv->remove) {
                drv->remove(dev);
            }
            dev->driver = NULL;
            dev->probed = false;
        }
    }
    
    /* 从列表中移除 */
    for (int i = idx; i < g_driver_count - 1; i++) {
        g_drivers[i] = g_drivers[i + 1];
    }
    g_driver_count--;
    drv->in_use = false;
    
    LOG_INFO_MSG("platform: Unregistered driver '%s'\n", drv->name);
    
    return HAL_OK;
}

/* ============================================================================
 * 设备注册 API 实现
 * ============================================================================ */

platform_device_t *platform_device_alloc(const char *name, int id) {
    if (!name) {
        return NULL;
    }
    
    /* 查找空闲槽位 */
    for (int i = 0; i < PLATFORM_MAX_DEVICES; i++) {
        if (!g_devices[i].in_use) {
            platform_device_t *dev = &g_devices[i];
            memset(dev, 0, sizeof(platform_device_t));
            
            /* 复制名称 */
            size_t len = strlen(name);
            if (len >= PLATFORM_NAME_MAX) {
                len = PLATFORM_NAME_MAX - 1;
            }
            memcpy(dev->name, name, len);
            dev->name[len] = '\0';
            
            /* 分配 ID */
            dev->id = (id >= 0) ? (uint32_t)id : g_next_device_id++;
            dev->in_use = true;
            
            return dev;
        }
    }
    
    LOG_WARN_MSG("platform: No free device slots\n");
    return NULL;
}

void platform_device_free(platform_device_t *dev) {
    if (!dev) return;
    
    /* 确保设备已注销 */
    if (dev->probed && dev->driver && dev->driver->remove) {
        dev->driver->remove(dev);
    }
    
    dev->in_use = false;
    dev->probed = false;
    dev->driver = NULL;
}

hal_error_t platform_device_register(platform_device_t *dev) {
    if (!dev || !dev->in_use) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    if (!g_platform_initialized) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    
    /* 更新设备计数 */
    bool found = false;
    for (int i = 0; i < PLATFORM_MAX_DEVICES; i++) {
        if (&g_devices[i] == dev) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    /* 更新计数（如果需要） */
    int max_idx = 0;
    for (int i = 0; i < PLATFORM_MAX_DEVICES; i++) {
        if (g_devices[i].in_use) {
            max_idx = i + 1;
        }
    }
    if (max_idx > g_device_count) {
        g_device_count = max_idx;
    }
    
    LOG_DEBUG_MSG("platform: Registered device '%s' (id=%u, source=%d)\n",
                  dev->name, dev->id, dev->source);
    
    /* 尝试与已注册的驱动匹配 */
    for (int i = 0; i < g_driver_count; i++) {
        if (try_match(dev, g_drivers[i])) {
            probe_device(dev, g_drivers[i]);
            break;
        }
    }
    
    return HAL_OK;
}

hal_error_t platform_device_unregister(platform_device_t *dev) {
    if (!dev) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    /* 调用驱动的 remove 回调 */
    if (dev->probed && dev->driver && dev->driver->remove) {
        dev->driver->remove(dev);
    }
    
    dev->probed = false;
    dev->driver = NULL;
    dev->enabled = false;
    
    LOG_DEBUG_MSG("platform: Unregistered device '%s'\n", dev->name);
    
    return HAL_OK;
}

/* ============================================================================
 * 资源访问 API 实现
 * ============================================================================ */

platform_resource_t *platform_get_resource(platform_device_t *dev,
                                           platform_res_type_t type,
                                           uint32_t index) {
    if (!dev) return NULL;
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < dev->num_resources; i++) {
        if (dev->resources[i].type == type) {
            if (count == index) {
                return &dev->resources[i];
            }
            count++;
        }
    }
    
    return NULL;
}

int32_t platform_get_irq(platform_device_t *dev, uint32_t index) {
    platform_resource_t *res = platform_get_resource(dev, PLATFORM_RES_IRQ, index);
    if (!res) return -1;
    return (int32_t)res->start;
}

uint64_t platform_get_mmio_base(platform_device_t *dev, uint32_t index) {
    platform_resource_t *res = platform_get_resource(dev, PLATFORM_RES_MEM, index);
    if (!res) return 0;
    return res->start;
}

uint64_t platform_get_mmio_size(platform_device_t *dev, uint32_t index) {
    platform_resource_t *res = platform_get_resource(dev, PLATFORM_RES_MEM, index);
    if (!res) return 0;
    return res->end - res->start + 1;
}

/* ============================================================================
 * 资源添加 API 实现
 * ============================================================================ */

hal_error_t platform_device_add_mem_resource(platform_device_t *dev,
                                              uint64_t start,
                                              uint64_t size,
                                              uint32_t flags) {
    if (!dev || dev->num_resources >= PLATFORM_MAX_RESOURCES) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    platform_resource_t *res = &dev->resources[dev->num_resources++];
    res->type = PLATFORM_RES_MEM;
    res->start = start;
    res->end = start + size - 1;
    res->flags = flags;
    res->name = NULL;
    
    return HAL_OK;
}

hal_error_t platform_device_add_irq_resource(platform_device_t *dev,
                                              uint32_t irq,
                                              uint32_t flags) {
    if (!dev || dev->num_resources >= PLATFORM_MAX_RESOURCES) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    platform_resource_t *res = &dev->resources[dev->num_resources++];
    res->type = PLATFORM_RES_IRQ;
    res->start = irq;
    res->end = irq;
    res->flags = flags;
    res->name = NULL;
    
    return HAL_OK;
}

/* ============================================================================
 * 框架初始化
 * ============================================================================ */

hal_error_t platform_init(void) {
    if (g_platform_initialized) {
        return HAL_OK;
    }
    
    /* 清空设备和驱动列表 */
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_drivers, 0, sizeof(g_drivers));
    g_device_count = 0;
    g_driver_count = 0;
    g_next_device_id = 0;
    
    g_platform_initialized = true;
    
    LOG_INFO_MSG("platform: Platform device framework initialized\n");
    
    return HAL_OK;
}

int platform_match_devices(void) {
    int matched = 0;
    
    for (int i = 0; i < g_device_count; i++) {
        platform_device_t *dev = &g_devices[i];
        if (!dev->in_use || dev->probed) continue;
        
        for (int j = 0; j < g_driver_count; j++) {
            if (try_match(dev, g_drivers[j])) {
                if (probe_device(dev, g_drivers[j]) == 0) {
                    matched++;
                }
                break;
            }
        }
    }
    
    return matched;
}

/* ============================================================================
 * 调试 API 实现
 * ============================================================================ */

#ifndef ARCH_ARM64
void platform_print_devices(void) {
    kprintf("\n===== Platform Devices (%d) =====\n", g_device_count);
    
    for (int i = 0; i < PLATFORM_MAX_DEVICES; i++) {
        platform_device_t *dev = &g_devices[i];
        if (!dev->in_use) continue;
        
        kprintf("Device: %s (id=%u)\n", dev->name, dev->id);
        kprintf("  Source: ");
        switch (dev->source) {
            case PLATFORM_SRC_PCI:
                kprintf("PCI (%04x:%04x)\n", 
                        dev->pci.vendor_id, dev->pci.device_id);
                break;
            case PLATFORM_SRC_DTB:
                kprintf("DTB (%s)\n", 
                        dev->dtb.compatible ? dev->dtb.compatible : "unknown");
                break;
            case PLATFORM_SRC_MANUAL:
                kprintf("Manual\n");
                break;
            default:
                kprintf("Unknown\n");
        }
        
        kprintf("  Resources: %u\n", dev->num_resources);
        for (uint32_t j = 0; j < dev->num_resources; j++) {
            platform_resource_t *res = &dev->resources[j];
            switch (res->type) {
                case PLATFORM_RES_MEM:
                    kprintf("    MEM: 0x%llx - 0x%llx\n", 
                            (unsigned long long)res->start,
                            (unsigned long long)res->end);
                    break;
                case PLATFORM_RES_IRQ:
                    kprintf("    IRQ: %u\n", (uint32_t)res->start);
                    break;
                case PLATFORM_RES_IO:
                    kprintf("    IO: 0x%x - 0x%x\n", 
                            (uint32_t)res->start, (uint32_t)res->end);
                    break;
                default:
                    kprintf("    Unknown resource type\n");
            }
        }
        
        kprintf("  Driver: %s\n", 
                dev->driver ? dev->driver->name : "(none)");
        kprintf("  Status: %s\n", 
                dev->probed ? "probed" : "not probed");
        kprintf("\n");
    }
}

void platform_print_drivers(void) {
    kprintf("\n===== Platform Drivers (%d) =====\n", g_driver_count);
    
    for (int i = 0; i < g_driver_count; i++) {
        platform_driver_t *drv = g_drivers[i];
        if (!drv) continue;
        
        kprintf("Driver: %s\n", drv->name);
        
        if (drv->pci_ids) {
            kprintf("  PCI IDs: ");
            const platform_pci_id_t *id = drv->pci_ids;
            while (id->vendor_id != PCI_ID_END) {
                kprintf("%04x:%04x ", id->vendor_id, id->device_id);
                id++;
            }
            kprintf("\n");
        }
        
        if (drv->compatible) {
            kprintf("  Compatible: ");
            const char **compat = drv->compatible;
            while (*compat != COMPATIBLE_END) {
                kprintf("%s ", *compat);
                compat++;
            }
            kprintf("\n");
        }
        
        kprintf("\n");
    }
}
#else
/* ARM64 stubs - no kprintf available */
void platform_print_devices(void) {
    /* Not implemented for ARM64 */
}

void platform_print_drivers(void) {
    /* Not implemented for ARM64 */
}
#endif /* !ARCH_ARM64 */
